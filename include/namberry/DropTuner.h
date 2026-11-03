#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <vector>

namespace namberry {

/// Pitch detector / drop tuner. Detects the fundamental of the input and
/// reports the note; the tuning offset is exposed for downstream transposition.
class DropTuner
{
public:
    DropTuner() = default;
    ~DropTuner() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void reset();

    void setTuningOffsetSemitones (float offset);
    float getTuningOffsetSemitones() const { return tuningOffsetSemitones_; }

    void setBypass (bool bypassed) { bypass_ = bypassed; }
    bool isBypassed() const { return bypass_; }

    juce::String getDetectedNote() const { return detectedNoteName_; }
    float getDetectedFrequency() const { return detectedFreqHz_; }
    int getDetectedMidiNote() const { return detectedMidiNote_; }

    struct Params { float tuningOffset = 0.0f; bool bypass = false; };
    Params getParams() const;
    void setParams (const Params& p);

private:
    float detectFundamental (const std::vector<float>& x) const;
    static int frequencyToMidi (float hz);
    static juce::String midiToName (int midiNote);

    double sampleRate_ = 44100.0;
    float tuningOffsetSemitones_ = 0.0f;
    bool bypass_ = false;

    float detectedFreqHz_ = 0.0f;
    int detectedMidiNote_ = -1;
    juce::String detectedNoteName_ { "---" };
};

} // namespace namberry
