#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace namberry {

/// Downward expander / noise gate with threshold, attack and release.
class NoiseGate
{
public:
    NoiseGate() = default;
    ~NoiseGate() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void reset();

    void setThresholdDb (float thresholdDb);
    void setAttackMs (float attackMs);
    void setReleaseMs (float releaseMs);
    void setBypass (bool bypassed) { bypass_ = bypassed; }
    bool isBypassed() const { return bypass_; }

    struct Params { float thresholdDb = -60.0f; float attackMs = 1.0f; float releaseMs = 100.0f; bool bypass = false; };
    Params getParams() const { return { thresholdDb_, attackMs_, releaseMs_, bypass_ }; }
    void setParams (const Params& p);

private:
    double sampleRate_ = 44100.0;
    float thresholdDb_ = -60.0f;
    float attackMs_ = 1.0f;
    float releaseMs_ = 100.0f;
    bool bypass_ = false;

    float env_ = 0.0f;
    float gain_ = 0.0f;
};

} // namespace namberry
