#include "namberry/ChorusEnsemble.h"

#include <algorithm>
#include <cmath>

namespace namberry {

namespace {
constexpr int kNumVoices = 3;
constexpr float kPhases[kNumVoices] = {
    0.0f,
    1.0f / 3.0f,
    2.0f / 3.0f
};
constexpr float kPans[kNumVoices] = { 0.08f, 0.5f, 0.92f };
constexpr float kVoiceGain = 2.0f / 3.0f; // pan weights sum to 1.5 per side
}

//==============================================================================
void ChorusEnsemble::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = juce::jmax (1.0, spec.sampleRate);

    juce::dsp::ProcessSpec monoSpec = spec;
    monoSpec.numChannels = 1;

    const int maxDelaySamples = static_cast<int> (
        std::ceil (kMaxDelayMs * 0.001 * sampleRate_)) + 8;
    delayLine_.setMaximumDelayInSamples (maxDelaySamples);
    delayLine_.prepare (monoSpec);

    slowLfo_.prepare (sampleRate_);
    fastLfo_.prepare (sampleRate_);

    for (auto& filter : highPass_)
    {
        filter.setType (juce::dsp::FirstOrderTPTFilterType::highpass);
        filter.prepare (monoSpec);
        filter.setCutoffFrequency (kToneHighPassHz);
    }

    for (auto& filter : lowPass_)
    {
        filter.setType (juce::dsp::FirstOrderTPTFilterType::lowpass);
        filter.prepare (monoSpec);
        filter.setCutoffFrequency (kToneLowPassHz);
    }

    baseDelaySmoothed_.reset (sampleRate_, 0.02);
    depthSmoothed_.reset (sampleRate_, 0.02);
    feedbackSmoothed_.reset (sampleRate_, 0.02);
    mixSmoothed_.reset (sampleRate_, 0.02);

    reset();
}

void ChorusEnsemble::reset()
{
    delayLine_.reset();
    slowLfo_.reset();
    fastLfo_.reset();

    for (auto& filter : highPass_)
        filter.reset();

    for (auto& filter : lowPass_)
        filter.reset();

    lastWetMono_ = 0.0f;

    baseDelaySmoothed_.setCurrentAndTargetValue (baseDelayMs_);
    depthSmoothed_.setCurrentAndTargetValue (depth_);
    feedbackSmoothed_.setCurrentAndTargetValue (feedback_);
    mixSmoothed_.setCurrentAndTargetValue (mix_);
}

//==============================================================================
void ChorusEnsemble::setRate (float hz)
{
    const float rate = juce::jlimit (0.1f, 5.0f, hz);
    slowLfo_.setRate (rate);
    fastLfo_.setRate (juce::jlimit (2.0f, 8.0f, rate * kFastRateMultiplier));
}

void ChorusEnsemble::setBaseDelayMs (float ms)
{
    baseDelayMs_ = juce::jlimit (5.0f, 30.0f, ms);
    baseDelaySmoothed_.setTargetValue (baseDelayMs_);
}

void ChorusEnsemble::setDepthPercent (float percent)
{
    depth_ = juce::jlimit (0.0f, 1.0f, percent * 0.01f);
    depthSmoothed_.setTargetValue (depth_);
}

void ChorusEnsemble::setFeedbackPercent (float percent)
{
    // Chorus feedback is a subtle resonant colouring, not a long tail. The
    // injected signal is additionally scaled by 0.5 in processBlock().
    feedback_ = juce::jlimit (0.0f, 0.7f, percent * 0.01f);
    feedbackSmoothed_.setTargetValue (feedback_);
}

void ChorusEnsemble::setMixPercent (float percent)
{
    mix_ = juce::jlimit (0.0f, 0.5f, percent * 0.01f);
    mixSmoothed_.setTargetValue (mix_);
}

//==============================================================================
void ChorusEnsemble::processBlock (juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (2, buffer.getNumChannels());

    if (numSamples <= 0 || numChannels <= 0)
        return;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float mono = buffer.getSample (0, sample);

        if (numChannels > 1)
            mono = 0.5f * (mono + buffer.getSample (1, sample));

        const float baseDelay = baseDelaySmoothed_.getNextValue();
        const float depth = depthSmoothed_.getNextValue();
        const float feedback = feedbackSmoothed_.getNextValue();
        const float mix = mixSmoothed_.getNextValue();

        float slow[kNumVoices] {};
        float fast[kNumVoices] {};

        for (int voice = 0; voice < kNumVoices; ++voice)
        {
            slow[voice] = slowLfo_.getValue (kPhases[voice]);
            fast[voice] = fastLfo_.getValue (kPhases[voice]);
        }

        slowLfo_.advance();
        fastLfo_.advance();

        float wetLeft = 0.0f;
        float wetRight = 0.0f;
        float tapSum = 0.0f;

        for (int voice = 0; voice < kNumVoices; ++voice)
        {
            const float modulationMs = depth
                                     * (slow[voice] * kSlowDepthMs
                                        + fast[voice] * kFastDepthMs);
            const float delayMs = juce::jlimit (
                0.5f, kMaxDelayMs, baseDelay + modulationMs);
            const float delaySamples = delayMs * 0.001f * static_cast<float> (sampleRate_);

            // The first tap advances the shared read pointer; the others are
            // extra taps from the same buffer position.
            float tap = delayLine_.popSample (0, delaySamples, voice == 0);
            tap = highPass_[static_cast<size_t> (voice)].processSample (0, tap);
            tap = lowPass_[static_cast<size_t> (voice)].processSample (0, tap);
            tap /= 1.0f + 0.2f * std::abs (tap); // gentle BBD-style saturation

            const float pan = kPans[voice];
            wetLeft += tap * (1.0f - pan) * kVoiceGain;
            wetRight += tap * pan * kVoiceGain;
            tapSum += tap;
        }

        // Feedback uses the previous averaged voice, so no same-sample loop.
        delayLine_.pushSample (0, mono + feedback * lastWetMono_ * 0.5f);
        lastWetMono_ = tapSum / static_cast<float> (kNumVoices);

        const float dryLeft = buffer.getSample (0, sample);
        const float wetOutLeft = wetLeft * mix * kWetGain;

        if (numChannels == 1)
        {
            buffer.setSample (0, sample,
                              dryLeft * (1.0f - mix)
                              + 0.5f * (wetLeft + wetRight) * mix * kWetGain);
        }
        else
        {
            const float dryRight = buffer.getSample (1, sample);
            buffer.setSample (0, sample, dryLeft * (1.0f - mix) + wetOutLeft);
            buffer.setSample (1, sample,
                              dryRight * (1.0f - mix) + wetRight * mix * kWetGain);
        }
    }
}

} // namespace namberry
