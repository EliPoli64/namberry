#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <memory>

namespace namberry {

/// Lock-free spectrum tap for the editor's graphs.
///
/// The audio thread only copies mono-summed samples into a FIFO; all FFT work
/// happens on the message thread in update(). The editor calls update() from
/// its timer and only reads the finished magnitudes, so processBlock never
/// allocates, locks or transforms.
class SignalSpectrum
{
public:
    static constexpr int kFftOrder = 11;
    static constexpr int kFftSize = 1 << kFftOrder;
    static constexpr int kNumBins = kFftSize / 2;
    static constexpr int kRingSize = 1 << 14;

    SignalSpectrum();
    ~SignalSpectrum();

    void prepare (double sampleRate);
    void reset();

    /// Audio thread: append a block to the analysis FIFO. Newest samples are
    /// dropped when the message thread has not drained the FIFO yet.
    void pushBlock (const juce::AudioBuffer<float>& buffer) noexcept;

    /// Message thread: analyse the most recent window and refresh magnitudes.
    void update();

    int getNumBins() const noexcept { return kNumBins; }
    double getSampleRate() const noexcept { return sampleRate_; }
    float getBinFrequency (int bin) const noexcept;
    float getBinDecibels (int bin) const noexcept;
    bool hasSignal() const noexcept { return peakDb_ > -96.0f; }
    int getRevision() const noexcept { return revision_.load (std::memory_order_relaxed); }

private:
    void analyse (float* samples) noexcept;
    void release();

    juce::AbstractFifo fifo_ { kRingSize };
    std::array<float, kRingSize> ring_ {};
    std::array<float, kFftSize> windowed_ {};
    std::array<float, kFftSize * 2> fftData_ {};
    std::array<float, kNumBins> smoothedDb_ {};

    std::unique_ptr<juce::dsp::FFT> fft_;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window_;

    double sampleRate_ = 48000.0;
    float peakDb_ = -100.0f;
    int lastAvailable_ = 0;
    int starvedFrames_ = 0;
    std::atomic<int> revision_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SignalSpectrum)
};

} // namespace namberry
