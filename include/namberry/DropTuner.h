#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <vector>

namespace namberry {

/// Pitch detector for the tuner display and the shared pitch stage.
/// Uses a long analysis window (independent of the host block size) and
/// first-peak autocorrelation so low notes and drop tunings are measured
/// reliably.
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
    float getDetectedPeriodSamples() const { return (float) detectedPeriodSamples_; }
    float getDetectionConfidence() const { return confidence_; }

    void setReferenceFrequency (float hz);
    float getReferenceFrequency() const { return referenceHz_; }
    float getTargetFrequency() const { return targetFreqHz_; }
    float getCentsDeviation() const { return centsDeviation_; }

    struct Params { float tuningOffset = 0.0f; bool bypass = false; };
    Params getParams() const;
    void setParams (const Params& p);

private:
    void runDetection();
    void pushMonoSample (float sample);
    int frequencyToMidi (float hz) const;
    static juce::String midiToName (int midiNote);

    double sampleRate_ = 44100.0;
    float tuningOffsetSemitones_ = 0.0f;
    float referenceHz_ = 440.0f;
    bool bypass_ = false;

    int analysisSize_ = 4096;
    int decimation_ = 4;
    int detectionHop_ = 256;
    std::vector<float> analysis_;
    std::vector<float> detectionScratch_;
    std::vector<float> correlation_;
    int analysisWrite_ = 0;
    int analysisFilled_ = 0;
    int samplesSinceDetection_ = 0;

    double detectedPeriodSamples_ = 0.0;
    float detectedFreqHz_ = 0.0f;
    float targetFreqHz_ = 0.0f;
    float centsDeviation_ = 0.0f;
    int detectedMidiNote_ = -1;
    juce::String detectedNoteName_ { "---" };
    float confidence_ = 0.0f;

    std::array<float, 5> periodHistory_ {};
    int periodHistoryCount_ = 0;
    int periodHistoryIndex_ = 0;
};

} // namespace namberry
