#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>

namespace namberry {

/// Pickup-impersonation preamp. Two fixed EQ curves approximate the tonal
/// shift between bridge and neck pickup positions. The pedal is bypassed by
/// default and has no automatic level compensation; Level is manual.
class Impersonator
{
public:
    Impersonator() = default;
    ~Impersonator() = default;

    enum class Mode { bridgeToNeck, neckToBridge };

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    void setMode (Mode mode);
    Mode getMode() const noexcept { return targetMode_; }

    void setBypass (bool bypassed);
    bool isBypassed() const noexcept { return bypass_; }

    void setOutputLevelDb (float db);
    float getOutputLevelDb() const noexcept { return outputLevelDb_; }

private:
    static constexpr int numStages = 6;

    struct FilterChain
    {
        std::array<std::array<juce::dsp::IIR::Filter<float>, numStages>, 2> filters;

        void prepare (const juce::dsp::ProcessSpec& spec);
        void reset();
        float processSample (int channel, float sample) noexcept;
    };

    void updateCoefficients();

    FilterChain bridgeToNeck_;
    FilterChain neckToBridge_;

    double sampleRate_ = 44100.0;
    Mode targetMode_ = Mode::bridgeToNeck;
    bool bypass_ = true;
    float outputLevelDb_ = 0.0f;

    juce::SmoothedValue<float> modeSmoothed_;
    juce::SmoothedValue<float> levelSmoothed_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Impersonator)
};

} // namespace namberry
