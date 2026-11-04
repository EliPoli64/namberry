#include "namberry/Room.h"

#include <algorithm>
#include <cmath>

namespace namberry {

namespace {

// Prime-ish reflection times for a small-to-medium room. Scaling these by the
// size control keeps the reflection pattern irregular instead of producing a
// single obvious echo.
constexpr float kBaseDelays[] = {
    0.0071f, 0.0113f, 0.0179f, 0.0233f, 0.0311f, 0.0437f, 0.0593f, 0.0797f
};

constexpr float kTapGains[] = {
    0.36f, 0.29f, 0.23f, 0.18f, 0.14f, 0.09f, 0.06f, 0.03f
};

constexpr float kMaxDelaySeconds = 0.16f;
constexpr float kStereoSpreadSeconds = 0.0007f;
constexpr float kLowpassHz = 6500.0f;
constexpr float kFeedback = 0.30f;

} // namespace

//==============================================================================
void Room::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;

    const int tailSamples = static_cast<int> (std::ceil (sampleRate_ * kMaxDelaySeconds));
    const int maxBlockSamples = static_cast<int> (std::ceil (spec.maximumBlockSize));
    bufferSize_ = tailSamples + maxBlockSamples + 4;

    for (auto& buffer : buffers_)
        buffer.assign (static_cast<size_t> (bufferSize_), 0.0f);

    lowpassCoeff_ = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi
                                     * kLowpassHz / static_cast<float> (sampleRate_));

    sizeSmoothed_.reset (sampleRate_, 0.05);
    blendSmoothed_.reset (sampleRate_, 0.03);

    reset();
}

void Room::reset()
{
    for (auto& buffer : buffers_)
        std::fill (buffer.begin(), buffer.end(), 0.0f);

    writeIndex_.fill (0);
    lowpassState_.fill (0.0f);

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

void Room::setBypass (bool bypassed)
{
    if (bypassed == bypass_)
        return;

    bypass_ = bypassed;

    if (bypassed)
    {
        reset();
    }
    else
    {
        // Fade the room in from silence instead of jumping to the blend value.
        blendSmoothed_.setCurrentAndTargetValue (0.0f);
        blendSmoothed_.setTargetValue (blend_);
    }
}

//==============================================================================
float Room::readTap (int channel, int writeIndex, float delaySamples) const
{
    float readPosition = static_cast<float> (writeIndex) - delaySamples;

    while (readPosition < 0.0f)
        readPosition += static_cast<float> (bufferSize_);

    while (readPosition >= static_cast<float> (bufferSize_))
        readPosition -= static_cast<float> (bufferSize_);

    const int index0 = static_cast<int> (readPosition);
    const int index1 = (index0 + 1) % bufferSize_;
    const float fraction = readPosition - static_cast<float> (index0);

    const auto& buffer = buffers_[static_cast<size_t> (channel)];
    return buffer[static_cast<size_t> (index0)]
         + fraction * (buffer[static_cast<size_t> (index1)]
                       - buffer[static_cast<size_t> (index0)]);
}

//==============================================================================
void Room::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    if (bypass_ || bufferSize_ <= 0 || buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (2, buffer.getNumChannels());

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const float size = sizeSmoothed_.getNextValue();
        const float blend = blendSmoothed_.getNextValue();
        const float scale = 0.65f + 1.05f * size;

        std::array<int, 2> anchor {};

        // Write the current sample and remember the read anchor.
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const int write = writeIndex_[static_cast<size_t> (ch)];
            buffers_[static_cast<size_t> (ch)][static_cast<size_t> (write)] =
                buffer.getSample (ch, sample);
            anchor[static_cast<size_t> (ch)] = write;
            writeIndex_[static_cast<size_t> (ch)] = (write + 1) % bufferSize_;
        }

        float wetLeft = 0.0f;
        float wetRight = 0.0f;

        for (int tap = 0; tap < kNumTaps; ++tap)
        {
            const float baseDelay = kBaseDelays[tap] * scale;
            const float spread = (tap % 2 == 0 ? 1.0f : -1.0f)
                               * kStereoSpreadSeconds * static_cast<float> (tap + 1);

            const float leftDelaySamples = juce::jmax (1.0f, (baseDelay + spread) * static_cast<float> (sampleRate_));
            const float rightDelaySamples = juce::jmax (1.0f, (baseDelay - spread) * static_cast<float> (sampleRate_));

            const float leftTap = readTap (0, anchor[0], leftDelaySamples);
            const float rightTap = numChannels > 1
                ? readTap (1, anchor[1], rightDelaySamples)
                : leftTap;

            const float gain = kTapGains[tap];

            // A little cross-feed creates width without collapsing the room
            // into two unrelated mono reflections.
            wetLeft  += gain * (leftTap  * 0.80f + rightTap * 0.20f);
            wetRight += gain * (rightTap * 0.80f + leftTap  * 0.20f);
        }

        lowpassState_[0] += lowpassCoeff_ * (wetLeft  - lowpassState_[0]);
        lowpassState_[1] += lowpassCoeff_ * (wetRight - lowpassState_[1]);

        const float dryLeft  = buffer.getSample (0, sample);
        buffer.setSample (0, sample, dryLeft + blend * (lowpassState_[0] - dryLeft));

        if (numChannels > 1)
        {
            const float dryRight = buffer.getSample (1, sample);
            buffer.setSample (1, sample, dryRight + blend * (lowpassState_[1] - dryRight));
        }

        // A low amount of feedback turns the taps into a dense, short room
        // rather than an audible sequence of separate echoes. The fixed
        // low-pass in the loop keeps the tail from becoming bright or long.
        buffers_[0][static_cast<size_t> (anchor[0])] += lowpassState_[0] * kFeedback;

        if (numChannels > 1)
            buffers_[1][static_cast<size_t> (anchor[1])] += lowpassState_[1] * kFeedback;
    }
}

} // namespace namberry
