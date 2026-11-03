#include "namberry/DigitalCapo.h"

namespace namberry {

//==============================================================================
void DigitalCapo::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    maxBlockSize_ = juce::jmax (1, (int) spec.maximumBlockSize);

    dualTap_.prepare (spec);
    psola_.prepare (spec);

    // Both engines are designed for 5 ms. Keep the dry path on the same
    // latency so the host PDC never changes when the engine is toggled.
    latencySamples_ = juce::jmax (dualTap_.getLatencySamples(),
                                  psola_.getLatencySamples());

    dryDelay_.setMaximumDelayInSamples (latencySamples_ + maxBlockSize_ + 8);
    dryDelay_.prepare (spec);
    dryDelay_.reset();

    const int channels = juce::jmax (1, (int) spec.numChannels);
    wetDual_.setSize (channels, maxBlockSize_);
    wetPsola_.setSize (channels, maxBlockSize_);
    wetDual_.clear();
    wetPsola_.clear();

    mixSmoothed_.reset (spec.sampleRate, 0.020);
    mixSmoothed_.setCurrentAndTargetValue (0.0f);

    semitoneSmoothed_.reset (spec.sampleRate, 0.020);
    semitoneSmoothed_.setCurrentAndTargetValue (semitones_);

    engineMixSmoothed_.reset (spec.sampleRate, 0.020);
    engineMixSmoothed_.setCurrentAndTargetValue (
        engine_ == Engine::Psola ? 1.0f : 0.0f);

    active_ = false;
    periodReady_ = false;
    activationSamplesRemaining_ = 0;
    prepared_ = true;
}

void DigitalCapo::reset()
{
    dualTap_.reset();
    psola_.reset();
    dryDelay_.reset();

    mixSmoothed_.setCurrentAndTargetValue (0.0f);
    semitoneSmoothed_.setCurrentAndTargetValue (semitones_);
    engineMixSmoothed_.setCurrentAndTargetValue (
        engine_ == Engine::Psola ? 1.0f : 0.0f);

    active_ = false;
    periodReady_ = false;
    activationSamplesRemaining_ = 0;
    wetDual_.clear();
    wetPsola_.clear();
}

//==============================================================================
void DigitalCapo::processBlock (juce::AudioBuffer<float>& buffer,
                                juce::MidiBuffer& /*midi*/)
{
    if (! prepared_ || buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0)
        return;

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    if (wetDual_.getNumChannels() != numChannels
        || wetDual_.getNumSamples() < numSamples)
    {
        wetDual_.setSize (numChannels, numSamples, false, false, true);
        wetPsola_.setSize (numChannels, numSamples, false, false, true);
    }

    const bool wantActive = ! bypass_
                         && semitones_ != 0.0f
                         && mix_ > 0.0f
                         && periodReady_;

    const bool wasActive = active_;
    const bool rising = wantActive && ! wasActive;

    semitoneSmoothed_.setTargetValue (semitones_);
    semitoneSmoothed_.skip (numSamples);
    const float engineSemitones = semitoneSmoothed_.getCurrentValue();

    dualTap_.setTransposeSemitones (engineSemitones);
    psola_.setTransposeSemitones (engineSemitones);

    if (pitchPeriodSamples_ > 0.0f)
    {
        psola_.setPitchPeriodSamples (pitchPeriodSamples_, rising);
        dualTap_.setPitchPeriodSamples (pitchPeriodSamples_);
    }

    engineMixSmoothed_.setTargetValue (engine_ == Engine::Psola ? 1.0f : 0.0f);

    if (wantActive)
    {
        if (! wasActive)
        {
            dualTap_.reset();
            psola_.reset();
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

    const bool runEngines = active_
                         && (wantActive || mixSmoothed_.getCurrentValue() > 0.0001f);

    if (runEngines)
    {
        dualTap_.process (buffer, wetDual_, numSamples);
        psola_.process (buffer, wetPsola_, numSamples);
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const float mix = mixSmoothed_.getNextValue();
        const float engineMix = engineMixSmoothed_.getNextValue();

        // The dry path is always mono and panned dead centre.
        float dryInput = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
            dryInput += buffer.getSample (ch, i);

        dryInput /= (float) numChannels;

        dryDelay_.pushSample (0, dryInput);
        const float dry = dryDelay_.popSample (0, (float) latencySamples_);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float wet = 0.0f;

            if (runEngines)
            {
                const float dual = wetDual_.getSample (ch, i);
                const float psola = wetPsola_.getSample (ch, i);
                wet = dual + (psola - dual) * engineMix;
            }

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

void DigitalCapo::setPitchPeriodSamples (float periodSamples)
{
    pitchPeriodSamples_ = juce::jmax (0.0f, periodSamples);
    periodReady_ = pitchPeriodSamples_ > 0.0f;
}

void DigitalCapo::setParams (const Params& p)
{
    semitones_ = juce::jlimit (-12.0f, 12.0f, p.semitones);
    mix_ = juce::jlimit (0.0f, 1.0f, p.mix);
    bypass_ = p.bypass;
}

} // namespace namberry
