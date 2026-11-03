#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <cstdint>
#include <vector>

namespace namberry {

/// Time-domain dual-tap pitch shifter. Polyphonic, 5 ms latency, more
/// "whammy"-like artifacts than PSOLA on low notes.
class DualTapPitchShifter
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void setTransposeSemitones (float semitones);
    void setPitchPeriodSamples (float periodSamples);
    void process (const juce::AudioBuffer<float>& input,
                  juce::AudioBuffer<float>& output,
                  int numSamples);
    int getLatencySamples() const noexcept { return latencySamples_; }

private:
    int numChannels_ = 2;
    int maxBlockSize_ = 512;
    int grainSamples_ = 0;
    int maxGrainSamples_ = 0;
    int bufferSize_ = 0;
    int writeIndex_ = 0;
    int latencySamples_ = 0;
    int latencyTargetSamples_ = 0;
    int delayOffset_ = 0;

    float phase_ = 0.0f;
    float ratio_ = 1.0f;

    std::vector<std::vector<float>> buffers_;
    std::vector<float> window_;
};

/// Pitch-synchronous overlap-add shifter. Better on clean single notes,
/// less reliable on chords. Fixed 10 ms grain, 5 ms latency.
class PsolaPitchShifter
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void setTransposeSemitones (float semitones);
    void setPitchPeriodSamples (float periodSamples, bool immediate = false);
    void process (const juce::AudioBuffer<float>& input,
                  juce::AudioBuffer<float>& output,
                  int numSamples);
    int getLatencySamples() const noexcept { return latencySamples_; }

private:
    void scheduleGrain (int64_t outputPosition);

    int numChannels_ = 2;
    int maxBlockSize_ = 512;
    int grainSamples_ = 0;
    int inputSize_ = 0;
    int outputSize_ = 0;
    int latencySamples_ = 0;
    int64_t inputCounter_ = 0;
    int64_t outputCounter_ = 0;

    float periodSamples_ = 0.0f;
    float ratio_ = 1.0f;
    float readCenter_ = 0.0f;
    float samplesUntilGrain_ = 0.0f;

    std::vector<std::vector<float>> inputBuffers_;
    std::vector<std::vector<float>> pendingBuffers_;
    std::vector<std::vector<float>> windowSums_;
    std::vector<float> window_;
};

} // namespace namberry
