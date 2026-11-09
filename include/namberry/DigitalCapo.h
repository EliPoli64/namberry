#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "namberry/PitchShifter.h"

namespace namberry {

/// Shared pitch stage for the digital capo and drop tuner.
///
/// The stage selects between a low-latency and a quality engine mode. Both
/// modes use the same streaming wrapper and report a fixed latency to the
/// host; the dry path is delayed to match the wet path and is mono-summed so
/// it stays dead centre.
class DigitalCapo
{
public:
    DigitalCapo() = default;
    ~DigitalCapo() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void reset();

    void setQualityMode (PitchShifter::QualityMode quality);
    PitchShifter::QualityMode getQualityMode() const noexcept { return quality_; }

    void setSemitones (float semitones);
    float getSemitones() const { return semitones_; }

    void setTone (float tone);
    float getTone() const noexcept { return tone_; }

    void setMix (float mix);
    float getMix() const { return mix_; }

    void setBypass (bool bypassed) { bypass_ = bypassed; }
    bool isBypassed() const { return bypass_; }

    /// Latency introduced by the Stretch stage when it is active, in samples.
    int getLatencySamples() const noexcept { return latencySamples_; }

    /// Latency currently reported to the host: zero while pitch shifting is
    /// inactive, full Stretch latency while it is active.
    int getReportedLatencySamples() const noexcept
    {
        return active_ ? latencySamples_ : 0;
    }

    bool isActive() const noexcept { return active_; }

    struct Params { float semitones = 0.0f; float tone = 1.0f; float mix = 1.0f; bool bypass = false; };
    Params getParams() const { return { semitones_, tone_, mix_, bypass_ }; }
    void setParams (const Params& p);

private:
    PitchShifter stretch_;

    juce::AudioBuffer<float> wet_;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay_;
    juce::SmoothedValue<float> mixSmoothed_;
    juce::SmoothedValue<float> semitoneSmoothed_;

    PitchShifter::QualityMode quality_ = PitchShifter::QualityMode::lowLatency;
    double sampleRate_ = 44100.0;
    int maxBlockSize_ = 512;
    int latencySamples_ = 0;

    float semitones_ = 0.0f;
    float tone_ = 1.0f;
    float mix_ = 1.0f;
    bool bypass_ = false;
    bool active_ = false;
    bool prepared_ = false;
    int activationSamplesRemaining_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DigitalCapo)
};

} // namespace namberry
