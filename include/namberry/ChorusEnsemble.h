#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>

#include "namberry/ModulationEffects.h"

namespace namberry {

/// Three-voice stereo chorus in the style of BBD-based ensemble pedals.
///
/// One mono delay buffer is read at three independently modulated taps. A slow
/// wide LFO and a faster shallow LFO are summed for each voice, with the voices
/// phase-spread by 120 degrees and panned across the stereo field. The wet path
/// is high-passed and low-passed before a mild soft clip to emulate BBD tone
/// shaping. Existing chorus parameters map to Rate, Depth, Feedback, Delay and
/// Mix; the ensemble layout itself is fixed and needs no new state.
class ChorusEnsemble
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void processBlock (juce::AudioBuffer<float>& buffer);

    void setRate (float hz);
    void setBaseDelayMs (float ms);
    void setDepthPercent (float percent);
    void setFeedbackPercent (float percent);
    void setMixPercent (float percent);

private:
    static constexpr int kNumVoices = 3;
    static constexpr float kMaxDelayMs = 40.0f;
    static constexpr float kSlowDepthMs = 3.2f;
    static constexpr float kFastDepthMs = 1.0f;
    static constexpr float kFastRateMultiplier = 6.0f;
    static constexpr float kToneHighPassHz = 120.0f;
    static constexpr float kToneLowPassHz = 5500.0f;
    static constexpr float kWetGain = 2.0f;

    double sampleRate_ = 44100.0;

    ModulationLfo slowLfo_ { ModulationLfo::Waveform::sine };
    ModulationLfo fastLfo_ { ModulationLfo::Waveform::sine };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> delayLine_;

    std::array<juce::dsp::FirstOrderTPTFilter<float>, kNumVoices> highPass_ {};
    std::array<juce::dsp::FirstOrderTPTFilter<float>, kNumVoices> lowPass_ {};

    juce::SmoothedValue<float> baseDelaySmoothed_;
    juce::SmoothedValue<float> depthSmoothed_;
    juce::SmoothedValue<float> feedbackSmoothed_;
    juce::SmoothedValue<float> mixSmoothed_;

    float baseDelayMs_ = 15.0f;
    float depth_ = 0.5f;
    float feedback_ = 0.15f;
    float mix_ = 0.5f;
    float lastWetMono_ = 0.0f;
};

} // namespace namberry
