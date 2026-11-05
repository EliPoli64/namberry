#include "namberry/DigitalCapo.h"

namespace namberry {

//==============================================================================
void DigitalCapo::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    maxBlockSize_ = juce::jmax (1, static_cast<int> (spec.maximumBlockSize));

    stretch_.prepare (spec);
    latencySamples_ = juce::jmax (1, stretch_.getLatencySamples());

    dryDelay_.setMaximumDelayInSamples (latencySamples_ + maxBlockSize_ + 8);
    dryDelay_.prepare (spec);
    dryDelay_.reset();

    const int channels = juce::jmax (1, static_cast<int> (spec.numChannels));
    wet_.setSize (channels, maxBlockSize_);
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

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    if (wet_.getNumChannels() != numChannels || wet_.getNumSamples() < numSamples)
    {
        wet_.setSize (numChannels, numSamples, false, false, true);
        wet_.clear();
    }

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

    if (runStretch)
        stretch_.process (buffer, wet_, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const float mix = mixSmoothed_.getNextValue();

        // The dry path is always mono and panned dead centre.
        float dryInput = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
            dryInput += buffer.getSample (ch, i);

        dryInput /= static_cast<float> (numChannels);

        dryDelay_.pushSample (0, dryInput);
        const float delayedDry = dryDelay_.popSample (0, static_cast<float> (latencySamples_));

        // The latency compensation delay is only audible while pitch shifting
        // is active. Inactive states pass the dry signal immediately.
        const float dry = active_ ? delayedDry : dryInput;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float wet = runStretch ? wet_.getSample (ch, i) : 0.0f;
            buffer.setSample (ch, i, dry + (wet - dry) * mix);
        }
    }

    if (! wantActive && mixSmoothed_.getCurrentValue() <= 0.0001f)
    {
        active_ = false;
        activationSamplesRemaining_ = 0;
    }
}

//==============================================================================
void DigitalCapo::setSemitones (float semitones)
{
    semitones_ = juce::jlimit (-12.0f, 12.0f, semitones);
}

void DigitalCapo::setMix (float mix)
{
    mix_ = juce::jlimit (0.0f, 1.0f, mix);
}

void DigitalCapo::setParams (const Params& p)
{
    semitones_ = juce::jlimit (-12.0f, 12.0f, p.semitones);
    mix_ = juce::jlimit (0.0f, 1.0f, p.mix);
    bypass_ = p.bypass;
}

} // namespace namberry
