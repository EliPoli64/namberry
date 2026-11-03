#include "namberry/DropTuner.h"

#include <algorithm>
#include <cmath>

namespace namberry {

void DropTuner::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;

    analysisSize_ = juce::jmax (2048, (int) (spec.sampleRate * 0.080));
    decimation_ = juce::jmax (1, (int) std::lround (spec.sampleRate / 24000.0));
    detectionHop_ = juce::jmax (128, (int) (spec.sampleRate * 0.010));

    analysis_.assign ((size_t) analysisSize_, 0.0f);
    detectionScratch_.assign ((size_t) (analysisSize_ / decimation_ + 1), 0.0f);

    const int srDec = juce::jmax (1, (int) (sampleRate_ / decimation_));
    const int maxLag = juce::jmin (analysisSize_ / (2 * decimation_),
                                   (int) (srDec / 40.0));
    correlation_.assign ((size_t) maxLag + 2, 0.0f);

    reset();
}

void DropTuner::reset()
{
    std::fill (analysis_.begin(), analysis_.end(), 0.0f);

    analysisWrite_ = 0;
    analysisFilled_ = 0;
    samplesSinceDetection_ = 0;

    detectedPeriodSamples_ = 0.0;
    detectedFreqHz_ = 0.0f;
    targetFreqHz_ = 0.0f;
    centsDeviation_ = 0.0f;
    detectedMidiNote_ = -1;
    detectedNoteName_ = "---";
    confidence_ = 0.0f;

    periodHistoryCount_ = 0;
    periodHistoryIndex_ = 0;
    periodHistory_.fill (0.0f);
}

void DropTuner::pushMonoSample (float sample)
{
    analysis_[(size_t) analysisWrite_] = sample;
    analysisWrite_ = (analysisWrite_ + 1) % analysisSize_;

    if (analysisFilled_ < analysisSize_)
        ++analysisFilled_;
}

//==============================================================================
void DropTuner::processBlock (juce::AudioBuffer<float>& buffer,
                              juce::MidiBuffer& /*midi*/)
{
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();

    if (n <= 0 || channels <= 0)
        return;

    const float channelScale = 1.0f / (float) channels;

    for (int i = 0; i < n; ++i)
    {
        float mono = 0.0f;

        for (int ch = 0; ch < channels; ++ch)
            mono += buffer.getSample (ch, i);

        pushMonoSample (mono * channelScale);

        if (++samplesSinceDetection_ >= detectionHop_)
        {
            samplesSinceDetection_ = 0;

            if (analysisFilled_ >= analysisSize_)
                runDetection();
        }
    }

    juce::ignoreUnused (bypass_, tuningOffsetSemitones_);
}

//==============================================================================
void DropTuner::runDetection()
{
    const int dec = decimation_;
    const int m = analysisSize_ / dec;

    if (m < 64 || correlation_.size() < 4)
        return;

    // Read the ring chronologically and decimate with a small averaging filter.
    int readIndex = analysisWrite_;

    double mean = 0.0;

    for (int i = 0; i < m; ++i)
    {
        float sum = 0.0f;

        for (int j = 0; j < dec; ++j)
        {
            sum += analysis_[(size_t) readIndex];
            readIndex = (readIndex + 1) % analysisSize_;
        }

        const float value = sum / (float) dec;
        detectionScratch_[(size_t) i] = value;
        mean += value;
    }

    mean /= m;

    for (int i = 0; i < m; ++i)
        detectionScratch_[(size_t) i] -= (float) mean;

    const int srDec = juce::jmax (1, (int) (sampleRate_ / dec));
    const int minLag = juce::jmax (2, (int) (srDec / 1200.0));
    const int maxLag = juce::jmin (m / 2, (int) (srDec / 40.0));

    if (maxLag <= minLag + 2)
        return;

    if ((int) correlation_.size() < maxLag + 2)
        correlation_.resize ((size_t) maxLag + 2, 0.0f);

    std::fill (correlation_.begin(), correlation_.end(), 0.0f);

    float globalMax = -1.0f;

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double corr = 0.0;
        double norm1 = 0.0;
        double norm2 = 0.0;

        for (int i = 0; i + lag < m; ++i)
        {
            const double a = detectionScratch_[(size_t) i];
            const double b = detectionScratch_[(size_t) (i + lag)];

            corr += a * b;
            norm1 += a * a;
            norm2 += b * b;
        }

        const double norm = std::sqrt (norm1 * norm2);

        if (norm > 0.0)
        {
            const float value = (float) (corr / norm);
            correlation_[(size_t) lag] = value;
            globalMax = juce::jmax (globalMax, value);
        }
    }

    if (globalMax <= 0.0f)
        return;

    // First strong local maximum, not the global maximum: a periodic signal
    // correlates almost equally well at every multiple of its period.
    const float threshold = juce::jmax (0.5f, globalMax * 0.8f);
    int bestLag = 0;
    float bestValue = 0.0f;

    for (int lag = minLag + 1; lag < maxLag; ++lag)
    {
        const float value = correlation_[(size_t) lag];

        if (value >= threshold
            && value >= correlation_[(size_t) (lag - 1)]
            && value >= correlation_[(size_t) (lag + 1)])
        {
            bestLag = lag;
            bestValue = value;
            break;
        }
    }

    if (bestLag == 0)
    {
        // Fall back to the global peak if no local maximum crossed the threshold.
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            if (correlation_[(size_t) lag] > bestValue)
            {
                bestValue = correlation_[(size_t) lag];
                bestLag = lag;
            }
        }
    }

    if (bestLag <= 0 || bestValue < 0.3f)
        return;

    float delta = 0.0f;
    const float c0 = correlation_[(size_t) (bestLag - 1)];
    const float c1 = correlation_[(size_t) bestLag];
    const float c2 = correlation_[(size_t) (bestLag + 1)];
    const float denom = c0 - 2.0f * c1 + c2;

    if (std::abs (denom) > 1.0e-6f)
        delta = juce::jlimit (-1.0f, 1.0f, 0.5f * (c0 - c2) / denom);

    const double period = ((double) bestLag + delta) * dec;

    if (period <= 0.0)
        return;

    // Median-smooth the last few estimates; hold the previous value when the
    // detector is not confident.
    periodHistory_[(size_t) periodHistoryIndex_] = (float) period;
    periodHistoryIndex_ = (periodHistoryIndex_ + 1) % (int) periodHistory_.size();
    periodHistoryCount_ = juce::jmin (periodHistoryCount_ + 1,
                                      (int) periodHistory_.size());

    float sorted[5];
    for (int i = 0; i < periodHistoryCount_; ++i)
        sorted[i] = periodHistory_[(size_t) i];

    std::sort (sorted, sorted + periodHistoryCount_);
    const float median = sorted[periodHistoryCount_ / 2];

    detectedPeriodSamples_ = median;
    confidence_ = bestValue;

    detectedFreqHz_ = (float) (sampleRate_ / detectedPeriodSamples_);
    detectedMidiNote_ = frequencyToMidi (detectedFreqHz_);
    detectedNoteName_ = midiToName (detectedMidiNote_);

    if (detectedMidiNote_ >= 0)
    {
        targetFreqHz_ = referenceHz_ * std::pow (
            2.0f, (float) (detectedMidiNote_ - 69) / 12.0f);
        centsDeviation_ = 1200.0f * std::log2 (
            juce::jmax (1.0e-6f, detectedFreqHz_) / targetFreqHz_);
    }
    else
    {
        targetFreqHz_ = 0.0f;
        centsDeviation_ = 0.0f;
    }
}

void DropTuner::setReferenceFrequency (float hz)
{
    referenceHz_ = juce::jlimit (400.0f, 480.0f, hz);
}

//==============================================================================
int DropTuner::frequencyToMidi (float hz) const
{
    if (hz <= 0.0f) return -1;
    return (int) std::lround (69.0 + 12.0 * std::log2 (hz / referenceHz_));
}

juce::String DropTuner::midiToName (int midiNote)
{
    if (midiNote < 0) return "---";
    static const char* names[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    const int octave = (midiNote / 12) - 1;
    return juce::String (names[midiNote % 12]) + juce::String (octave);
}

void DropTuner::setTuningOffsetSemitones (float offset)
{
    tuningOffsetSemitones_ = juce::jlimit (-12.0f, 0.0f, offset);
}

DropTuner::Params DropTuner::getParams() const
{
    return { tuningOffsetSemitones_, bypass_ };
}

void DropTuner::setParams (const Params& p)
{
    tuningOffsetSemitones_ = juce::jlimit (-12.0f, 0.0f, p.tuningOffset);
    bypass_ = p.bypass;
}

} // namespace namberry
