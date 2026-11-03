#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <vector>

#include "signalsmith-stretch.h"

namespace namberry {

/// Thin wrapper around Signalsmith Stretch used for the digital capo.
///
/// The shifter is configured for a fixed channel count and sample rate in
/// prepare(); process() is real-time safe after that point (no allocations).
class PitchShifter
{
public:
    PitchShifter() = default;
    ~PitchShifter() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    void setTransposeSemitones (float semitones);

    int getLatencySamples() const noexcept { return latencySamples_; }
    int getNumChannels() const noexcept    { return numChannels_; }
    int getMaxBlockSize() const noexcept   { return maxBlockSize_; }

    /// input and output must not be the same buffer.
    void process (const juce::AudioBuffer<float>& input,
                  juce::AudioBuffer<float>& output,
                  int numSamples);

private:
    signalsmith::stretch::SignalsmithStretch<float> stretch_;

    int numChannels_ = 2;
    int maxBlockSize_ = 512;
    int latencySamples_ = 0;
    bool prepared_ = false;

    std::vector<float*> inputPointers_;
    std::vector<float*> outputPointers_;
};

} // namespace namberry
