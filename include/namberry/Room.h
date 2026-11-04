#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace namberry {

/// Stereo convolution room built from two generated room impulse responses.
/// Size crossfades between a small and a large room; Room Mix blends the wet
/// room with the dry signal. There is no bypass: at 0 % mix the stage is dry.
class Room
{
public:
    Room() = default;
    ~Room() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    void setSize (float size);   // 0..1, small to large
    void setBlend (float blend); // 0..1 dry/wet

private:
    static juce::AudioBuffer<float> makeRoomImpulse (double sampleRate,
                                                     float durationSeconds,
                                                     float rt60Seconds,
                                                     float earlyScale,
                                                     int seed);

    juce::dsp::Convolution smallRoom_;
    juce::dsp::Convolution largeRoom_;

    juce::AudioBuffer<float> smallWet_;
    juce::AudioBuffer<float> largeWet_;

    double sampleRate_ = 44100.0;
    float size_ = 0.45f;
    float blend_ = 0.0f;

    juce::SmoothedValue<float> sizeSmoothed_;
    juce::SmoothedValue<float> blendSmoothed_;
};

} // namespace namberry
