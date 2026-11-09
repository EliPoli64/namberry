#include "namberry/DigitalCapo.h"

#include <array>

namespace namberry {

//==============================================================================
void DigitalCapo::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    maxBlockSize_ = juce::jmax (1, static_cast<int> (spec.maximumBlockSize));

    stretch_.prepare (spec, quality_);
    latencySamples_ = juce::jmax (1, stretch_.getLatencySamples());

    dryDelay_.setMaximumDelayInSamples (stretch_.getMaxLatencySamples()
                                        + maxBlockSize_ + 8);
    dryDelay_.prepare (spec);
    dryDelay_.reset();

    const int channels = juce::jmax (1, static_cast<int> (spec.numChannels));

    // Preallocate for the largest host block we are told about. The pitch
    // engine accepts arbitrary sizes, so this buffer is only a wet scratch
    // area and is never resized from processBlock().
    wet_.setSize (channels, juce::jmax (maxBlockSize_, 8192));
    wet_.clear();

    mixSmoothed_.reset (spec.sampleRate, 0.020);
    mixSmoothed_.setCurrentAndTargetValue (0.0f);

    semitoneSmoothed_.reset (spec.sampleRate, 0.020);
    semitoneSmoothed_.setCurrentAndTargetValue (semitones_);

    active_ = false;
    activationSamplesRemaining_ = 0;
    prepared_ = true;
}

void DigitalCapo::reset()
{
    stretch_.reset();
    dryDelay_.reset();

    mixSmoothed_.setCurrentAndTargetValue (0.0f);
    semitoneSmoothed_.setCurrentAndTargetValue (semitones_);

    active_ = false;
    activationSamplesRemaining_ = 0;
    wet_.clear();
}

//==============================================================================
void DigitalCapo::processBlock (juce::AudioBuffer<float>& buffer,
                                juce::MidiBuffer& /*midi*/)
{
    if (! prepared_ || buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0)
        return;

    const int numChannels = std::min (8, std::min (buffer.getNumChannels(),
                                                  wet_.getNumChannels()));
    const int numSamples = buffer.getNumSamples();

    const bool wantActive = ! bypass_ && semitones_ != 0.0f && mix_ > 0.0f;
    const bool wasActive = active_;

    semitoneSmoothed_.setTargetValue (semitones_);
    semitoneSmoothed_.skip (numSamples);
    stretch_.setTransposeSemitones (semitoneSmoothed_.getCurrentValue());

    if (wantActive)
    {
        if (! wasActive)
        {
            stretch_.reset();
            active_ = true;
            activationSamplesRemaining_ = latencySamples_;
        }

        if (activationSamplesRemaining_ > 0)
        {
            activationSamplesRemaining_ = juce::jmax (0, activationSamplesRemaining_ - numSamples);
            mixSmoothed_.setTargetValue (0.0f);
        }
        else
        {
            mixSmoothed_.setTargetValue (mix_);
        }
    }
    else
    {
        mixSmoothed_.setTargetValue (0.0f);

        if (! wasActive)
            activationSamplesRemaining_ = 0;
    }

    const bool runStretch = active_
                         && (wantActive || mixSmoothed_.getCurrentValue() > 0.0001f);

    const int chunkSize = wet_.getNumSamples();

    for (int offset = 0; offset < numSamples; offset += chunkSize)
    {
        const int chunk = juce::jmin (chunkSize, numSamples - offset);

        if (runStretch)
        {
            std::array<const float*, 8> inputPointers {};
            std::array<float*, 8> wetPointers {};

            for (int ch = 0; ch < numChannels; ++ch)
            {
                inputPointers[static_cast<size_t> (ch)] = buffer.getReadPointer (ch) + offset;
                wetPointers[static_cast<size_t> (ch)] = wet_.getWritePointer (ch);
            }

            stretch_.process (inputPointers.data(), wetPointers.data(), chunk);
        }

        for (int i = 0; i < chunk; ++i)
        {
            const int sampleIndex = offset + i;
            const float mix = mixSmoothed_.getNextValue();

            // The dry path is always mono and panned dead centre.
            float dryInput = 0.0f;

            for (int ch = 0; ch < numChannels; ++ch)
                dryInput += buffer.getSample (ch, sampleIndex);

            dryInput /= static_cast<float> (numChannels);

            dryDelay_.pushSample (0, dryInput);
            const float delayedDry = dryDelay_.popSample (0, static_cast<float> (latencySamples_));

            // The latency compensation delay is only audible while pitch shifting
            // is active. In inactive states pass the dry signal immediately.
            const float dry = active_ ? delayedDry : dryInput;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float wet = runStretch ? wet_.getSample (ch, i) : 0.0f;
                buffer.setSample (ch, sampleIndex, dry + (wet - dry) * mix);
            }
        }
    }

    if (! wantActive && mixSmoothed_.getCurrentValue() <= 0.0001f)
    {
        active_ = false;
        activationSamplesRemaining_ = 0;
    }
}

void DigitalCapo::setQualityMode (PitchShifter::QualityMode quality)
{
    if (quality == quality_)
        return;

    quality_ = quality;
    stretch_.setQualityMode (quality);
    latencySamples_ = juce::jmax (1, stretch_.getLatencySamples());

    if (active_)
    {
        stretch_.reset();
        activationSamplesRemaining_ = latencySamples_;
    }
}

//==============================================================================
void DigitalCapo::setSemitones (float semitones)
{
    semitones_ = juce::jlimit (-12.0f, 12.0f, semitones);
}

void DigitalCapo::setTone (float tone)
{
    tone_ = juce::jlimit (0.0f, 1.0f, tone);
    stretch_.setTone (tone_);
}

void DigitalCapo::setMix (float mix)
{
    mix_ = juce::jlimit (0.0f, 1.0f, mix);
}

void DigitalCapo::setParams (const Params& p)
{
    semitones_ = juce::jlimit (-12.0f, 12.0f, p.semitones);
    tone_ = juce::jlimit (0.0f, 1.0f, p.tone);
    mix_ = juce::jlimit (0.0f, 1.0f, p.mix);
    bypass_ = p.bypass;
}

} // namespace namberry
