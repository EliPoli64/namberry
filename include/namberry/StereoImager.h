#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <vector>

namespace namberry {

/// Mid/side stereo imager with a short decorrelation stage.
///
/// Width 0.5 is neutral, 0 collapses the signal to mono and 1 widens it. Mix
/// 0 is an exact passthrough. The decorrelation only runs when widening, so
/// narrowing stays a clean mid/side gain change rather than a phasey one.
class StereoImager
{
public:
    StereoImager() = default;
    ~StereoImager() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void processBlock (juce::AudioBuffer<float>& buffer);

    void setWidth (float width); // 0..1, 0.5 neutral
    void setMix (float mix);     // 0..1, 0 dry

private:
    struct Allpass
    {
        std::vector<float> buffer;
        int writeIndex = 0;
        float gain = 0.6f;

        void prepare (double sampleRate, float delayMs, float g);
        void reset();
        float processSample (float input);
    };

    static constexpr int kNumDecorrelators = 3;

    std::array<Allpass, kNumDecorrelators> decorrelators_;
    juce::dsp::IIR::Filter<float> sideHighPass_;

    double sampleRate_ = 44100.0;
    float width_ = 0.5f;
    float mix_ = 0.0f;

    juce::SmoothedValue<float> widthSmoothed_;
    juce::SmoothedValue<float> mixSmoothed_;
};

} // namespace namberry
