#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cmath>

namespace namberry {

/// Shared low-frequency oscillator used by the modulation effects.
class ModulationLfo
{
public:
    enum class Waveform { sine, triangle };

    explicit ModulationLfo (Waveform waveform = Waveform::sine)
        : waveform_ (waveform)
    {
    }

    void prepare (double sampleRate);
    void reset();
    void setRate (float hz);

    float getValue (float phaseOffset) const noexcept;
    void advance() noexcept;

private:
    double sampleRate_ = 44100.0;
    double phase_ = 0.0;
    float rate_ = 1.0f;
    Waveform waveform_ = Waveform::sine;
    juce::SmoothedValue<float> rateSmoothed_;
};

//==============================================================================
/// Stereo fractional delay with LFO modulation, feedback and dry/wet mix.
/// Chorus and flanger are configured instances of this core.
class ModulatedDelay
{
public:
    ModulatedDelay (float maxDelayMs,
                    ModulationLfo::Waveform waveform,
                    float phaseOffset,
                    float depthRangeMs);

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void processBlock (juce::AudioBuffer<float>& buffer);

    void setRate (float hz);
    void setBaseDelayMs (float ms);
    void setDepthPercent (float percent);
    void setFeedbackPercent (float percent);
    void setMixPercent (float percent);

private:
    float maxDelayMs_ = 40.0f;
    float phaseOffset_ = 0.0f;
    float depthRangeMs_ = 5.0f;
    double sampleRate_ = 44100.0;

    ModulationLfo lfo_;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine_;

    juce::SmoothedValue<float> baseDelaySmoothed_;
    juce::SmoothedValue<float> depthSmoothed_;
    juce::SmoothedValue<float> feedbackSmoothed_;
    juce::SmoothedValue<float> mixSmoothed_;

    float baseDelayMs_ = 15.0f;
    float depth_ = 0.5f;
    float feedback_ = 0.0f;
    float mix_ = 0.5f;
};

//==============================================================================
/// Cascaded first-order all-pass phaser with selectable 4/8/12 stages.
class Phaser
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void processBlock (juce::AudioBuffer<float>& buffer);

    void setRate (float hz);
    void setDepthPercent (float percent);
    void setFeedbackPercent (float percent);
    void setMixPercent (float percent);
    void setStages (int stages);

private:
    static constexpr int maxStages = 12;
    static constexpr int coefficientUpdateInterval = 8;

    std::array<juce::dsp::FirstOrderTPTFilter<float>, maxStages> filters_;
    ModulationLfo lfo_ { ModulationLfo::Waveform::sine };

    juce::SmoothedValue<float> depthSmoothed_;
    juce::SmoothedValue<float> feedbackSmoothed_;
    juce::SmoothedValue<float> mixSmoothed_;

    std::array<float, 2> lastOutput_ {};
    double sampleRate_ = 44100.0;
    int activeStages_ = 8;
    int updateCounter_ = 0;
    bool stageDirty_ = false;

    float depth_ = 0.7f;
    float feedback_ = 0.3f;
    float mix_ = 0.5f;
};

} // namespace namberry
