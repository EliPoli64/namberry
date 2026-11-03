#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace namberry {

/// Digital capo: models a physical capo by transposing pitch up by N semitones.
/// NOTE: audio transposition is a TODO; currently a pass-through control module.
class DigitalCapo
{
public:
    DigitalCapo() = default;
    ~DigitalCapo() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void reset();

    void setFretPosition (int fret);
    int getFretPosition() const { return fretPosition_; }

    void setMix (float mix);
    float getMix() const { return mix_; }

    void setBypass (bool bypassed) { bypass_ = bypassed; }
    bool isBypassed() const { return bypass_; }

    struct Params { int fretPosition = 0; float mix = 0.0f; bool bypass = false; };
    Params getParams() const { return { fretPosition_, mix_, bypass_ }; }
    void setParams (const Params& p);

private:
    double sampleRate_ = 44100.0;
    int fretPosition_ = 0;
    float mix_ = 0.0f;
    bool bypass_ = false;
};

} // namespace namberry
