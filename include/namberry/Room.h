#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <vector>

namespace namberry {

/// Short algorithmic ambience that ranges from a wide doubler/stereoizer to a
/// small 0.3 s room. Built from early reflections and an eight-line feedback
/// delay network.
///
/// Size is a macro control: it moves the pre-delay, early-reflection spacing,
/// FDN delay lengths, decay time, high-frequency damping and stereo width
/// together. Room Mix uses an equal-power dry/wet law. At 0 % the stage is
/// completely dry and no room processing is run.
class Room
{
public:
    Room() = default;
    ~Room() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    void setSize (float size);   // 0..1, small to large
    void setBlend (float blend); // 0..1 dry/wet

private:
    static constexpr int kNumLines = 8;
    static constexpr int kNumERTaps = 6;
    static constexpr int kNumDiffusers = 4;

    struct DelayLine
    {
        std::vector<float> buffer;
        int writeIndex = 0;
        int maxSamples = 1;

        void prepare (int samples);
        void reset();
        float read (float delaySamples) const;
        void write (float sample);
    };

    struct Allpass
    {
        std::array<std::vector<float>, 2> buffer;
        std::array<int, 2> writeIndex { 0, 0 };
        int delaySamples = 1;
        float gain = 0.6f;

        void prepare (double sampleRate, float delayMs, float g);
        void reset();
        float processSample (int channel, float input);
    };

    void updateWetTone();
    void updateMacroCoefficients (float size);

    float readHistory (int channel, float delaySamples) const;
    float processDcBlock (int channel, float sample);

    std::array<std::vector<float>, 2> history_;
    int historyWriteIndex_ = 0;
    int historySize_ = 1;

    std::array<DelayLine, kNumLines> fdnLines_;
    std::array<float, kNumLines> fdnDampingState_ {};
    std::array<float, kNumLines> fdnFeedback_ {};
    std::array<float, kNumLines> fdnDampingCoeff_ {};
    float fdnInputTrim_ = 1.0f;

    std::array<Allpass, kNumDiffusers> diffusers_;

    std::array<float, 2> dcPreviousInput_ {};
    std::array<float, 2> dcPreviousOutput_ {};

    std::array<juce::dsp::IIR::Filter<float>, 2> wetHighPass_;
    std::array<juce::dsp::IIR::Filter<float>, 2> wetMid_;
    std::array<juce::dsp::IIR::Filter<float>, 2> wetLowPass_;

    double sampleRate_ = 44100.0;
    float size_ = 0.45f;
    float blend_ = 0.0f;

    juce::SmoothedValue<float> sizeSmoothed_;
    juce::SmoothedValue<float> blendSmoothed_;
};

} // namespace namberry
