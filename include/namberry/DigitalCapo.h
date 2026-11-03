#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <memory>

namespace namberry {

class PitchShifter;

/// Digital capo: transposes pitch by N semitones (-12..+12, 0 = no shift).
/// Negative values tune down, positive values act like a capo.
class DigitalCapo
{
public:
    DigitalCapo();
    ~DigitalCapo();

    void prepare (const juce::dsp::ProcessSpec& spec);
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void reset();

    void setFretPosition (int fret);
    int getFretPosition() const { return fretPosition_; }

    void setMix (float mix);
    float getMix() const { return mix_; }

    void setBypass (bool bypassed) { bypass_ = bypassed; }
    bool isBypassed() const { return bypass_; }

    /// Latency introduced by the pitch shifter, in samples. The processor
    /// reports this to the host.
    int getLatencySamples() const noexcept { return latencySamples_; }
    bool isActive() const noexcept { return active_; }

    struct Params { int fretPosition = 0; float mix = 1.0f; bool bypass = false; };
    Params getParams() const { return { fretPosition_, mix_, bypass_ }; }
    void setParams (const Params& p);

private:
    std::unique_ptr<PitchShifter> shifter_;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay_;
    juce::AudioBuffer<float> wetBuffer_;

    juce::SmoothedValue<float> mixSmoothed_;
    juce::SmoothedValue<float> semitoneSmoothed_;

    double sampleRate_ = 44100.0;
    int maxBlockSize_ = 512;
    int latencySamples_ = 0;
    int fretPosition_ = 0;
    float mix_ = 1.0f;
    bool bypass_ = false;
    bool active_ = false;
    bool prepared_ = false;
    int activationSamplesRemaining_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DigitalCapo)
};

} // namespace namberry
