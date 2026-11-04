#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <vector>

namespace namberry {

/// Short delay-based room. This is deliberately not a long reverb: a network
/// of short, damped delay taps with a low fixed feedback gives the tone a
/// noticeable sense of space without a long tail.
class Room
{
public:
    Room() = default;
    ~Room() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    void setSize (float size);   // 0..1
    void setBlend (float blend); // 0..1
    void setBypass (bool bypassed);

    bool isBypassed() const { return bypass_; }

private:
    static constexpr int kNumTaps = 8;

    float readTap (int channel, int writeIndex, float delaySamples) const;

    std::array<std::vector<float>, 2> buffers_;
    std::array<int, 2> writeIndex_ {};
    std::array<float, 2> lowpassState_ {};

    int bufferSize_ = 0;
    double sampleRate_ = 44100.0;
    float lowpassCoeff_ = 0.2f;

    float size_ = 0.45f;
    float blend_ = 0.5f;
    bool bypass_ = true;

    juce::SmoothedValue<float> sizeSmoothed_;
    juce::SmoothedValue<float> blendSmoothed_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Room)
};

} // namespace namberry
