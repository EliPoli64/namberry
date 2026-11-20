#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>

namespace namberry {

/// Electric-to-acoustic simulator. Four fixed modal-EQ curves approximate the
/// body of different acoustic guitars and crossfade on mode switches; Top,
/// Body and Piezo knobs crossfade additive parallel sections so continuous
/// automation stays click-free. A mono envelope adds a brief pick attack and
/// low-mid bloom to the Top and Body sections, so plucks settle like a steel
/// string instead of sustaining like a magnetic pickup. Level is a manual trim
/// with no automatic makeup. The pedal is bypassed by default.
class AcousticSim
{
public:
    AcousticSim() = default;
    ~AcousticSim() = default;

    enum class Mode { standard = 0, jumbo, enhanced, piezo };

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    void setMode (Mode mode);
    Mode getMode() const noexcept { return targetMode_; }

    void setTop (float amount);      // 0..1
    void setBody (float amount);     // 0..1
    void setPiezo (float amount);    // 0..1
    void setOutputLevelDb (float db);

    void setBypass (bool bypassed);
    bool isBypassed() const noexcept { return bypass_; }

private:
    static constexpr int numModes = 4;
    static constexpr int numSections = 7;

    /// A fixed-length chain of biquads. Unused tail sections are kept at
    /// unity so every mode and section shares one topology.
    struct FilterChain
    {
        std::array<std::array<juce::dsp::IIR::Filter<float>, numSections>, 2> filters;

        void prepare (const juce::dsp::ProcessSpec& spec);
        void reset();
        float processSample (int channel, float sample) noexcept;
    };

    void updateModeCoefficients();
    void updateSectionCoefficients();
    float processPiezoSection (int channel, float sample) noexcept;

    static float modeWeight (Mode mode, int index) noexcept;
    static float onePoleCoefficient (double sampleRate, double timeMs) noexcept;

    std::array<FilterChain, numModes> modeChains_;
    FilterChain bodyChain_;
    FilterChain topChain_;
    FilterChain piezoChain_;

    double sampleRate_ = 44100.0;
    Mode targetMode_ = Mode::standard;
    bool bypass_ = true;

    /// Crossfade weights for jumbo, enhanced and piezo; standard is the
    /// remainder, so the four weights always sum to one.
    std::array<juce::SmoothedValue<float>, 3> modeWeights_;
    juce::SmoothedValue<float> topAmount_;
    juce::SmoothedValue<float> bodyAmount_;
    juce::SmoothedValue<float> piezoAmount_;
    juce::SmoothedValue<float> levelSmoothed_;

    /// One-pole envelope coefficients derived once in prepare(). The fast
    /// follower tracks the pick attack; the slow one tracks sustained level.
    float fastAttackCoeff_ = 0.0f;
    float fastReleaseCoeff_ = 0.0f;
    float slowAttackCoeff_ = 0.0f;
    float slowReleaseCoeff_ = 0.0f;
    float fastEnvelope_ = 0.0f;
    float slowEnvelope_ = 0.0f;

    static constexpr float piezoDrive = 1.5f;
    static constexpr float bodyTransientAmount = 0.25f;
    static constexpr float topTransientAmount = 0.60f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AcousticSim)
};

} // namespace namberry
