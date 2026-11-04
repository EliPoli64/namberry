#include "namberry/ModulationEffects.h"

namespace namberry {

namespace {
float clamp01 (float x) { return juce::jlimit (0.0f, 1.0f, x); }
}

//==============================================================================
void ModulationLfo::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    rateSmoothed_.reset (sampleRate_, 0.02);
    reset();
}

void ModulationLfo::reset()
{
    phase_ = 0.0;
    rateSmoothed_.setCurrentAndTargetValue (rate_);
}

void ModulationLfo::setRate (float hz)
{
    rate_ = juce::jlimit (0.01f, 20.0f, hz);
    rateSmoothed_.setTargetValue (rate_);
}

float ModulationLfo::getValue (float phaseOffset) const noexcept
{
    double p = phase_ + static_cast<double> (phaseOffset);
    p -= std::floor (p);

    if (waveform_ == Waveform::triangle)
    {
        // Triangle in [-1, 1], starting at -1.
        return 1.0f - 4.0f * static_cast<float> (std::abs (p - 0.5));
    }

    return static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * p));
}

void ModulationLfo::advance() noexcept
{
    if (sampleRate_ <= 0.0)
        return;

    phase_ += static_cast<double> (rateSmoothed_.getNextValue()) / sampleRate_;
    phase_ -= std::floor (phase_);
}

//==============================================================================
ModulatedDelay::ModulatedDelay (float maxDelayMs,
                                ModulationLfo::Waveform waveform,
                                float phaseOffset,
                                float depthRangeMs)
    : maxDelayMs_ (juce::jlimit (1.0f, 100.0f, maxDelayMs)),
      phaseOffset_ (phaseOffset),
      depthRangeMs_ (juce::jmax (0.0f, depthRangeMs)),
      lfo_ (waveform)
{
}

void ModulatedDelay::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;

    const int maxDelaySamples = static_cast<int> (std::ceil (maxDelayMs_ * 0.001 * sampleRate_)) + 4;
    delayLine_.setMaximumDelayInSamples (maxDelaySamples);
    delayLine_.prepare (spec);

    lfo_.prepare (sampleRate_);

    baseDelaySmoothed_.reset (sampleRate_, 0.02);
    depthSmoothed_.reset (sampleRate_, 0.02);
    feedbackSmoothed_.reset (sampleRate_, 0.02);
    mixSmoothed_.reset (sampleRate_, 0.02);

    reset();
}

void ModulatedDelay::reset()
{
    delayLine_.reset();
    lfo_.reset();

    baseDelaySmoothed_.setCurrentAndTargetValue (baseDelayMs_);
    depthSmoothed_.setCurrentAndTargetValue (depth_);
    feedbackSmoothed_.setCurrentAndTargetValue (feedback_);
    mixSmoothed_.setCurrentAndTargetValue (mix_);
}

void ModulatedDelay::setRate (float hz)
{
    lfo_.setRate (juce::jlimit (0.01f, 20.0f, hz));
}

void ModulatedDelay::setBaseDelayMs (float ms)
{
    baseDelayMs_ = juce::jlimit (0.5f, maxDelayMs_, ms);
    baseDelaySmoothed_.setTargetValue (baseDelayMs_);
}

void ModulatedDelay::setDepthPercent (float percent)
{
    depth_ = clamp01 (percent * 0.01f);
    depthSmoothed_.setTargetValue (depth_);
}

void ModulatedDelay::setFeedbackPercent (float percent)
{
    feedback_ = juce::jlimit (-1.0f, 1.0f, percent * 0.01f);
    feedbackSmoothed_.setTargetValue (feedback_);
}

void ModulatedDelay::setMixPercent (float percent)
{
    mix_ = clamp01 (percent * 0.01f);
    mixSmoothed_.setTargetValue (mix_);
}

void ModulatedDelay::processBlock (juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (2, buffer.getNumChannels());

    if (numSamples <= 0 || numChannels <= 0)
        return;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const float lfoLeft = lfo_.getValue (0.0f);
        const float lfoRight = lfo_.getValue (phaseOffset_);
        lfo_.advance();

        const float baseDelay = baseDelaySmoothed_.getNextValue();
        const float depth = depthSmoothed_.getNextValue();
        const float feedback = feedbackSmoothed_.getNextValue();
        const float mix = mixSmoothed_.getNextValue();
        const float modulation = depth * depthRangeMs_;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float lfoValue = channel == 0 ? lfoLeft : lfoRight;
            const float delayMs = juce::jlimit (0.5f, maxDelayMs_, baseDelay + modulation * lfoValue);
            const float delaySamples = delayMs * 0.001f * static_cast<float> (sampleRate_);

            const float dry = buffer.getSample (channel, sample);
            const float delayed = delayLine_.popSample (channel, delaySamples, true);

            buffer.setSample (channel, sample,
                              dry * (1.0f - mix) + delayed * mix);

            delayLine_.pushSample (channel, dry + delayed * feedback);
        }
    }
}

//==============================================================================
void Phaser::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;

    for (auto& filter : filters_)
    {
        filter.setType (juce::dsp::FirstOrderTPTFilterType::allpass);
        filter.prepare (spec);
    }

    lfo_.prepare (sampleRate_);

    depthSmoothed_.reset (sampleRate_, 0.02);
    feedbackSmoothed_.reset (sampleRate_, 0.02);
    mixSmoothed_.reset (sampleRate_, 0.02);

    reset();
}

void Phaser::reset()
{
    for (auto& filter : filters_)
        filter.reset();

    lastOutput_.fill (0.0f);
    lfo_.reset();
    updateCounter_ = 0;
    stageDirty_ = false;

    depthSmoothed_.setCurrentAndTargetValue (depth_);
    feedbackSmoothed_.setCurrentAndTargetValue (feedback_);
    mixSmoothed_.setCurrentAndTargetValue (mix_);
}

void Phaser::setRate (float hz)
{
    lfo_.setRate (juce::jlimit (0.05f, 8.0f, hz));
}

void Phaser::setDepthPercent (float percent)
{
    depth_ = clamp01 (percent * 0.01f);
    depthSmoothed_.setTargetValue (depth_);
}

void Phaser::setFeedbackPercent (float percent)
{
    feedback_ = juce::jlimit (0.0f, 0.9f, percent * 0.01f);
    feedbackSmoothed_.setTargetValue (feedback_);
}

void Phaser::setMixPercent (float percent)
{
    mix_ = clamp01 (percent * 0.01f);
    mixSmoothed_.setTargetValue (mix_);
}

void Phaser::setStages (int stages)
{
    const int normalized = stages <= 4 ? 4
                         : stages <= 8 ? 8
                         : 12;

    if (normalized == activeStages_)
        return;

    activeStages_ = normalized;
    stageDirty_ = true;
}

void Phaser::processBlock (juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (2, buffer.getNumChannels());

    if (numSamples <= 0 || numChannels <= 0)
        return;

    if (stageDirty_)
    {
        for (auto& filter : filters_)
            filter.reset();

        lastOutput_.fill (0.0f);
        updateCounter_ = 0;
        stageDirty_ = false;
    }

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const float depth = depthSmoothed_.getNextValue();

        if (updateCounter_ == 0)
        {
            const float lfoValue = lfo_.getValue (0.0f);
            const float cutoff = 600.0f
                               * std::pow (2.0f, depth * 1.5f * lfoValue);

            for (int stage = 0; stage < activeStages_; ++stage)
                filters_[static_cast<size_t> (stage)].setCutoffFrequency (cutoff);
        }

        lfo_.advance();

        const float feedback = feedbackSmoothed_.getNextValue();
        const float mix = mixSmoothed_.getNextValue();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float dry = buffer.getSample (channel, sample);
            float wet = dry + feedback * lastOutput_[static_cast<size_t> (channel)];

            for (int stage = 0; stage < activeStages_; ++stage)
                wet = filters_[static_cast<size_t> (stage)].processSample (channel, wet);

            lastOutput_[static_cast<size_t> (channel)] = wet;
            buffer.setSample (channel, sample,
                              dry * (1.0f - mix) + wet * mix);
        }

        updateCounter_ = (updateCounter_ + 1) % coefficientUpdateInterval;
    }
}

} // namespace namberry
