#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace namberry {

/// Feed-forward compressor with an optional low-band sidechain ("Tight" mode).
class Compressor
{
public:
    Compressor() = default;
    ~Compressor() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void reset();

    void setAttackMs (float attackMs);
    void setReleaseMs (float releaseMs);
    void setThresholdDb (float thresholdDb);
    void setRatio (float ratio);
    void setMakeupGainDb (float gainDb);
    void setTightMode (bool enabled);
    bool isTightMode() const { return tightMode_; }

    struct Params
    {
        float attackMs = 10.0f;
        float releaseMs = 100.0f;
        float thresholdDb = -20.0f;
        float ratio = 4.0f;
        float makeupGainDb = 0.0f;
        bool tightMode = false;
    };
    Params getParams() const;
    void setParams (const Params& p);

private:
    float sampleRate_ = 44100.0;
    float attackMs_ = 10.0f;
    float releaseMs_ = 100.0f;
    float thresholdDb_ = -20.0f;
    float ratio_ = 4.0f;
    float makeupGainDb_ = 0.0f;
    bool tightMode_ = false;

    float env_ = 0.0f;          // envelope detector state
    float sidechainLp_ = 0.0f;  // low-band sidechain state
};

} // namespace namberry
