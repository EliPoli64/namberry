#include "namberry/Room.h"

#include <cmath>
#include <iterator>

namespace namberry {

namespace {

// Prime-ish early reflections for the generated room impulse responses.
constexpr float kEarlyDelays[] = {
    0.0071f, 0.0113f, 0.0179f, 0.0233f, 0.0311f, 0.0437f
};

constexpr float kEarlyGains[] = {
    0.62f, 0.48f, 0.36f, 0.27f, 0.19f, 0.13f
};

constexpr float kTailLevel = 0.32f;
constexpr float kDampingHz = 6500.0f;
constexpr float kRoomWetGain = 2.0f;

} // namespace

//==============================================================================
juce::AudioBuffer<float> Room::makeRoomImpulse (double sampleRate,
                                                float durationSeconds,
                                                float rt60Seconds,
                                                float earlyScale,
                                                int seed)
{
    const int length = juce::jmax (1, static_cast<int> (std::round (sampleRate * durationSeconds)));
    juce::AudioBuffer<float> impulse (2, length);
    impulse.clear();

    // Early reflections: deliberately sparse, with a little stereo offset and
    // cross-feed so the room has width without being two unrelated mono rooms.
    for (int tap = 0; tap < static_cast<int> (std::size (kEarlyDelays)); ++tap)
    {
        const float baseDelay = kEarlyDelays[tap] * earlyScale;
        const float gain = kEarlyGains[tap];

        for (int channel = 0; channel < 2; ++channel)
        {
            const float spread = (channel == 0 ? 1.0f : -1.0f)
                               * 0.0008f * static_cast<float> (tap + 1);
            const int sample = static_cast<int> (std::round ((baseDelay + spread) * sampleRate));

            if (sample <= 0 || sample >= length)
                continue;

            const int other = 1 - channel;
            impulse.setSample (channel, sample,
                               impulse.getSample (channel, sample) + gain * 0.82f);
            impulse.setSample (other, sample,
                               impulse.getSample (other, sample) + gain * 0.18f);
        }
    }

    // A short, damped diffuse tail makes the result read as a real room rather
    // than a handful of discrete echoes.
    const int tailStart = juce::jlimit (1, length - 1,
                                        static_cast<int> (std::round (sampleRate * 0.012f * earlyScale)));
    const float lowpassCoeff = 1.0f - std::exp (
        -2.0f * juce::MathConstants<float>::pi * kDampingHz
        / static_cast<float> (sampleRate));

    juce::Random randomLeft (seed);
    juce::Random randomRight (seed + 137);

    float lowpassLeft = 0.0f;
    float lowpassRight = 0.0f;

    for (int sample = tailStart; sample < length; ++sample)
    {
        const float time = static_cast<float> (sample) / static_cast<float> (sampleRate);
        const float envelope = std::exp (-6.907755f * time / rt60Seconds);

        lowpassLeft += lowpassCoeff
                     * ((randomLeft.nextFloat() * 2.0f - 1.0f) - lowpassLeft);
        lowpassRight += lowpassCoeff
                      * ((randomRight.nextFloat() * 2.0f - 1.0f) - lowpassRight);

        impulse.addSample (0, sample, lowpassLeft * envelope * kTailLevel);
        impulse.addSample (1, sample, lowpassRight * envelope * kTailLevel);
    }

    return impulse;
}

//==============================================================================
void Room::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;

    const int wetChannels = juce::jmax (1, static_cast<int> (spec.numChannels));
    const int maxBlockSize = juce::jmax (1, static_cast<int> (spec.maximumBlockSize));

    smallWet_.setSize (wetChannels, maxBlockSize, false, false, true);
    largeWet_.setSize (wetChannels, maxBlockSize, false, false, true);

    auto smallImpulse = makeRoomImpulse (sampleRate_, 0.34f, 0.24f, 0.75f, 0x51a7);
    auto largeImpulse = makeRoomImpulse (sampleRate_, 0.72f, 0.58f, 1.35f, 0x2b3d);

    smallRoom_.loadImpulseResponse (std::move (smallImpulse), sampleRate_,
                                    juce::dsp::Convolution::Stereo::yes,
                                    juce::dsp::Convolution::Trim::no,
                                    juce::dsp::Convolution::Normalise::yes);
    largeRoom_.loadImpulseResponse (std::move (largeImpulse), sampleRate_,
                                    juce::dsp::Convolution::Stereo::yes,
                                    juce::dsp::Convolution::Trim::no,
                                    juce::dsp::Convolution::Normalise::yes);

    smallRoom_.prepare (spec);
    largeRoom_.prepare (spec);

    sizeSmoothed_.reset (sampleRate_, 0.05);
    blendSmoothed_.reset (sampleRate_, 0.03);

    reset();
}

void Room::reset()
{
    smallRoom_.reset();
    largeRoom_.reset();
    smallWet_.clear();
    largeWet_.clear();

    sizeSmoothed_.setCurrentAndTargetValue (size_);
    blendSmoothed_.setCurrentAndTargetValue (0.0f);
    blendSmoothed_.setTargetValue (blend_);
}

//==============================================================================
void Room::setSize (float size)
{
    size_ = juce::jlimit (0.0f, 1.0f, size);
    sizeSmoothed_.setTargetValue (size_);
}

void Room::setBlend (float blend)
{
    blend_ = juce::jlimit (0.0f, 1.0f, blend);
    blendSmoothed_.setTargetValue (blend_);
}

//==============================================================================
void Room::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (2, buffer.getNumChannels());

    if (numSamples <= 0 || numChannels <= 0 || smallWet_.getNumChannels() == 0)
        return;

    // At 0 % mix the room is inaudible; skip the convolutions entirely.
    if (blend_ <= 0.0f && blendSmoothed_.getCurrentValue() <= 1.0e-6f)
        return;

    const int wetChannels = juce::jmin (numChannels, smallWet_.getNumChannels());

    for (int channel = 0; channel < wetChannels; ++channel)
    {
        const int sourceChannel = juce::jmin (channel, buffer.getNumChannels() - 1);
        smallWet_.copyFrom (channel, 0, buffer, sourceChannel, 0, numSamples);
        largeWet_.copyFrom (channel, 0, buffer, sourceChannel, 0, numSamples);
    }

    auto smallBlock = juce::dsp::AudioBlock<float> (smallWet_).getSubBlock (0, (size_t) numSamples);
    auto largeBlock = juce::dsp::AudioBlock<float> (largeWet_).getSubBlock (0, (size_t) numSamples);

    juce::dsp::ProcessContextReplacing<float> smallContext (smallBlock);
    juce::dsp::ProcessContextReplacing<float> largeContext (largeBlock);

    smallRoom_.process (smallContext);
    largeRoom_.process (largeContext);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const float size = sizeSmoothed_.getNextValue();
        const float blend = blendSmoothed_.getNextValue();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float dry = buffer.getSample (channel, sample);
            const float wet = (smallWet_.getSample (channel, sample) * (1.0f - size)
                             + largeWet_.getSample (channel, sample) * size)
                             * kRoomWetGain;

            buffer.setSample (channel, sample, dry + blend * (wet - dry));
        }
    }
}

} // namespace namberry
