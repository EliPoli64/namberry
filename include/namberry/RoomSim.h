#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <vector>

namespace namberry {

/// Short stereo early-reflection room for the end of the signal chain.
///
/// A single amount control (0..1) fades in two slightly offset filtered delay
/// lines that repeat the mono-derived guitar with low feedback. Zero is an
/// exact passthrough and leaves the internal state untouched. Dry stays at
/// unity so adding space never makes the guitar quieter.
class RoomSim
{
public:
    RoomSim() = default;
    ~RoomSim() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void processBlock (juce::AudioBuffer<float>& buffer);

    void setAmount (float amount); // 0..1, 0 = exact passthrough

private:
    static constexpr float kDelayLeftMs = 28.0f;
    static constexpr float kDelayRightMs = 34.0f;
    static constexpr float kMinFeedback = 0.03f;
    static constexpr float kMaxFeedback = 0.15f;
    static constexpr float kWetHighPassHz = 120.0f;
    static constexpr float kWetLowPassHz = 6000.0f;
    static constexpr float kFilterQ = 0.707f;
    static constexpr float kMaxWetGain = 0.8f;
    static constexpr float kSmoothingMs = 30.0f;

    struct DelayLine
    {
        std::vector<float> samples;
        int length = 1;
        int writeIndex = 0;
    };

    void clearState();

    double sampleRate_ = 44100.0;
    float targetAmount_ = 0.0f;
    float targetFeedback_ = 0.0f;
    float targetWetGain_ = 0.0f;

    float smoothedFeedback_ = 0.0f;
    float smoothedWetGain_ = 0.0f;
    float feedbackStep_ = 0.0f;
    float wetGainStep_ = 0.0f;

    std::array<DelayLine, 2> lines_ {};
    std::array<juce::dsp::IIR::Filter<float>, 2> highPass_ {};
    std::array<juce::dsp::IIR::Filter<float>, 2> lowPass_ {};
};

} // namespace namberry
