#include "namberry/SignalSpectrum.h"

namespace namberry {

SignalSpectrum::SignalSpectrum() = default;

SignalSpectrum::~SignalSpectrum()
{
    // Make sure the FFT is deleted on a thread that is not the audio thread.
    fft_.reset();
    window_.reset();
}

void SignalSpectrum::prepare (double sampleRate)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;

    if (fft_ == nullptr)
        fft_ = std::make_unique<juce::dsp::FFT> (kFftOrder);

    if (window_ == nullptr)
        window_ = std::make_unique<juce::dsp::WindowingFunction<float>> (
            (size_t) kFftSize, juce::dsp::WindowingFunction<float>::hann);

    reset();
}

void SignalSpectrum::reset()
{
    fifo_.reset();
    smoothedDb_.fill (-100.0f);
    windowed_.fill (0.0f);
    fftData_.fill (0.0f);
    peakDb_ = -100.0f;
    lastAvailable_ = 0;
    starvedFrames_ = 0;
    revision_.fetch_add (1, std::memory_order_relaxed);
}

void SignalSpectrum::pushBlock (const juce::AudioBuffer<float>& buffer) noexcept
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0)
        return;

    const float* left = buffer.getReadPointer (0);
    const float* right = numChannels > 1 ? buffer.getReadPointer (1) : left;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo_.prepareToWrite (numSamples, start1, size1, start2, size2);

    for (int i = 0; i < size1; ++i)
        ring_[(size_t) (start1 + i)] = 0.5f * (left[i] + right[i]);

    for (int i = 0; i < size2; ++i)
        ring_[(size_t) (start2 + i)] = 0.5f * (left[size1 + i] + right[size1 + i]);

    fifo_.finishedWrite (size1 + size2);
}

void SignalSpectrum::update()
{
    if (fft_ == nullptr || window_ == nullptr)
        return;

    const int available = fifo_.getNumReady();

    if (available <= 0)
    {
        // Audio has stopped entirely (transport stopped, or no processing):
        // let the trace fall rather than freezing at its last level.
        release();
        return;
    }

    // With normal block sizes the FIFO usually holds less than one window;
    // wait for enough samples instead of releasing on every other UI tick.
    if (available < kFftSize)
    {
        // Only fall once the FIFO has stopped growing, which means audio is no
        // longer arriving (transport stopped, plugin idle).
        if (available <= lastAvailable_)
        {
            if (++starvedFrames_ > 30)
                release();
        }
        else
        {
            starvedFrames_ = 0;
        }

        lastAvailable_ = available;
        return;
    }

    lastAvailable_ = 0;
    starvedFrames_ = 0;

    // Drop older samples so the graph always shows the most recent audio.
    int discard = available - kFftSize;

    while (discard > 0)
    {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo_.prepareToRead (discard, start1, size1, start2, size2);
        fifo_.finishedRead (size1 + size2);
        discard -= size1 + size2;

        if (size1 + size2 == 0)
            break;
    }

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo_.prepareToRead (kFftSize, start1, size1, start2, size2);

    for (int i = 0; i < size1; ++i)
        windowed_[(size_t) i] = ring_[(size_t) (start1 + i)];

    for (int i = 0; i < size2; ++i)
        windowed_[(size_t) (size1 + i)] = ring_[(size_t) (start2 + i)];

    fifo_.finishedRead (size1 + size2);

    if (size1 + size2 < kFftSize)
        return;

    analyse (windowed_.data());
}

void SignalSpectrum::analyse (float* samples) noexcept
{
    window_->multiplyWithWindowingTable (samples, (size_t) kFftSize);

    std::fill (fftData_.begin(), fftData_.end(), 0.0f);
    std::copy (samples, samples + kFftSize, fftData_.begin());

    fft_->performFrequencyOnlyForwardTransform (fftData_.data(), true);

    // JUCE's Hann table is normalised to unity DC gain, so a full-scale sine
    // peak reads 0 dBFS after scaling the unscaled FFT by 2 / N.
    constexpr float scale = 2.0f / (float) kFftSize;
    float peak = -100.0f;

    for (int bin = 0; bin < kNumBins; ++bin)
    {
        const auto db = juce::Decibels::gainToDecibels (fftData_[(size_t) bin] * scale,
                                                        -100.0f);
        const auto previous = smoothedDb_[(size_t) bin];

        // Fast attack, slow release so the trace reads like a meter.
        const auto value = db > previous ? db : juce::jmax (db, previous - 2.0f);
        smoothedDb_[(size_t) bin] = value;
        peak = juce::jmax (peak, value);
    }

    peakDb_ = peak;
    revision_.fetch_add (1, std::memory_order_relaxed);
}

void SignalSpectrum::release()
{
    float peak = -100.0f;

    for (auto& value : smoothedDb_)
    {
        value = juce::jmax (-100.0f, value - 2.0f);
        peak = juce::jmax (peak, value);
    }

    peakDb_ = peak;
    revision_.fetch_add (1, std::memory_order_relaxed);
}

float SignalSpectrum::getBinFrequency (int bin) const noexcept
{
    return (float) ((double) juce::jlimit (0, kNumBins - 1, bin)
                    * sampleRate_ / (double) kFftSize);
}

float SignalSpectrum::getBinDecibels (int bin) const noexcept
{
    return smoothedDb_[(size_t) juce::jlimit (0, kNumBins - 1, bin)];
}

} // namespace namberry
