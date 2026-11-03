#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "namberry/PitchShiftEngines.h"

namespace namberry {

/// Shared pitch stage for the digital capo and drop tuner.
/// Supports two low-latency engines (dual-tap and PSOLA) with a smooth toggle.
class DigitalCapo
{
public:
    enum class Engine { DualTap, Psola };

    DigitalCapo() = default;
    ~DigitalCapo() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void reset();

    void setSemitones (float semitones);
    float getSemitones() const { return semitones_; }

    void setMix (float mix);
    float getMix() const { return mix_; }

    void setBypass (bool bypassed) { bypass_ = bypassed; }
    bool isBypassed() const { return bypass_; }

    void setEngine (Engine engine) { engine_ = engine; }
    Engine getEngine() const { return engine_; }

    void setPitchPeriodSamples (float periodSamples);

    /// Latency introduced by the pitch engine, in samples.
    int getLatencySamples() const noexcept { return latencySamples_; }
    bool isActive() const noexcept { return active_; }

    struct Params { float semitones = 0.0f; float mix = 1.0f; bool bypass = false; };
    Params getParams() const { return { semitones_, mix_, bypass_ }; }
    void setParams (const Params& p);

private:
    DualTapPitchShifter dualTap_;
    PsolaPitchShifter psola_;

    juce::AudioBuffer<float> wetDual_;
    juce::AudioBuffer<float> wetPsola_;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay_;
    juce::SmoothedValue<float> mixSmoothed_;
    juce::SmoothedValue<float> semitoneSmoothed_;
    juce::SmoothedValue<float> engineMixSmoothed_;

    double sampleRate_ = 44100.0;
    int maxBlockSize_ = 512;
    int latencySamples_ = 0;
    float semitones_ = 0.0f;
    float mix_ = 1.0f;
    float pitchPeriodSamples_ = 0.0f;
    bool bypass_ = false;
    bool active_ = false;
    bool prepared_ = false;
    bool periodReady_ = false;
    int activationSamplesRemaining_ = 0;

    Engine engine_ = Engine::DualTap;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DigitalCapo)
};

} // namespace namberry
