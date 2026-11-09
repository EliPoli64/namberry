#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <memory>

namespace namberry {

/// Streaming pitch shifter used by the digital capo.
///
/// The implementation is selected at prepare() time:
/// - Rubber Band's real-time shifter when the library is available at runtime.
/// - A self-contained Signalsmith Stretch fallback otherwise.
///
/// Both implementations are wrapped in the same fixed-size streaming layer so
/// arbitrary host block sizes and sample-rate changes cannot corrupt their
/// internal state. No heap allocation, locking or dynamic loading happens in
/// process().
class PitchShifter
{
public:
    enum class QualityMode
    {
        lowLatency, //!< Short window, minimum active latency.
        quality     //!< Longer window, improved low-note tracking.
    };

    PitchShifter();
    ~PitchShifter();

    void prepare (const juce::dsp::ProcessSpec& spec,
                  QualityMode quality = QualityMode::lowLatency);
    void reset();

    /// Changes the quality/latency mode. The change is applied atomically at
    /// the next process() boundary and the reported latency changes with it.
    void setQualityMode (QualityMode quality);
    QualityMode getQualityMode() const noexcept { return quality_; }

    void setTransposeSemitones (float semitones);
    void setTone (float tone);
    float getTone() const noexcept { return tone_; }

    int getLatencySamples() const noexcept;
    int getMaxLatencySamples() const noexcept;
    int getNumChannels() const noexcept { return numChannels_; }
    int getMaxBlockSize() const noexcept { return maxBlockSize_; }

    /// True when the runtime Rubber Band library is in use.
    bool isRubberBandAvailable() const noexcept;
    const char* getEngineName() const noexcept;

    /// input and output must not be the same buffer.
    void process (const juce::AudioBuffer<float>& input,
                  juce::AudioBuffer<float>& output,
                  int numSamples);

    /// Raw-pointer overload used for fixed-quantum streaming.
    void process (const float* const* input,
                  float* const* output,
                  int numSamples);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    QualityMode quality_ = QualityMode::lowLatency;
    int numChannels_ = 2;
    int maxBlockSize_ = 512;
    float tone_ = 1.0f;
    bool prepared_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchShifter)
};

} // namespace namberry
