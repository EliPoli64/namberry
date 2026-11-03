#include "namberry/PitchShiftEngines.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace namberry {

namespace {
constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

float hann (int index, int length)
{
    if (length <= 1)
        return 1.0f;

    return 0.5f - 0.5f * std::cos (kTwoPi * (float) index / (float) (length - 1));
}

int wrapIndex (int64_t index, int size)
{
    if (size <= 0)
        return 0;

    int64_t wrapped = index % size;
    if (wrapped < 0)
        wrapped += size;

    return (int) wrapped;
}
} // namespace

//==============================================================================
// Dual-tap granular shifter
//==============================================================================
void DualTapPitchShifter::prepare (const juce::dsp::ProcessSpec& spec)
{
    numChannels_ = juce::jmax (1, (int) spec.numChannels);
    maxBlockSize_ = juce::jmax (1, (int) spec.maximumBlockSize);

    latencyTargetSamples_ = juce::jmax (32, (int) (spec.sampleRate * 0.005)); // 5 ms
    latencySamples_ = latencyTargetSamples_;
    maxGrainSamples_ = juce::jmax (128, (int) (spec.sampleRate * 0.020));     // 20 ms

    grainSamples_ = latencyTargetSamples_;
    delayOffset_ = juce::jmax (0, latencyTargetSamples_ - grainSamples_ / 2);

    bufferSize_ = maxGrainSamples_ + latencyTargetSamples_ + maxBlockSize_ + 8;

    buffers_.assign ((size_t) numChannels_,
                     std::vector<float> ((size_t) bufferSize_, 0.0f));

    reset();
}

void DualTapPitchShifter::reset()
{
    for (auto& buffer : buffers_)
        std::fill (buffer.begin(), buffer.end(), 0.0f);

    writeIndex_ = 0;
    phase_ = 0.0f;
    ratio_ = 1.0f;
}

void DualTapPitchShifter::setPitchPeriodSamples (float periodSamples)
{
    if (periodSamples <= 0.0f)
        return;

    const int period = (int) std::lround (periodSamples);
    grainSamples_ = juce::jlimit (64, maxGrainSamples_, period);
    delayOffset_ = juce::jmax (0, latencyTargetSamples_ - grainSamples_ / 2);
}

void DualTapPitchShifter::setTransposeSemitones (float semitones)
{
    ratio_ = std::pow (2.0f, semitones / 12.0f);
}

void DualTapPitchShifter::process (const juce::AudioBuffer<float>& input,
                                   juce::AudioBuffer<float>& output,
                                   int numSamples)
{
    if (buffers_.empty() || numSamples <= 0)
        return;

    const int channels = juce::jmin ((int) buffers_.size(),
                                     input.getNumChannels(),
                                     output.getNumChannels());
    const int samples = juce::jmin (numSamples,
                                    input.getNumSamples(),
                                    output.getNumSamples());

    if (channels <= 0 || samples <= 0)
        return;

    const float phaseStep = (ratio_ - 1.0f) / (float) grainSamples_;

    for (int i = 0; i < samples; ++i)
    {
        for (int ch = 0; ch < channels; ++ch)
            buffers_[(size_t) ch][(size_t) writeIndex_] = input.getSample (ch, i);

        // A 0.1 phase offset gives a much stronger shifted component than the
        // naive 0.5 offset, which tends to cancel the transposed signal.
        float phase2 = phase_ + 0.1f;
        if (phase2 >= 1.0f)
            phase2 -= 1.0f;

        const float phases[2] = { phase_, phase2 };
        constexpr float fade = 0.05f;

        for (int ch = 0; ch < channels; ++ch)
        {
            float value = 0.0f;
            float windowSum = 0.0f;

            for (float p : phases)
            {
                const float delay = (float) grainSamples_ * (1.0f - p)
                                  + (float) delayOffset_;
                const float readPos = (float) writeIndex_ - delay;
                int i0 = (int) std::floor (readPos);
                const float frac = readPos - (float) i0;

                const int a = wrapIndex (i0, bufferSize_);
                const int b = wrapIndex ((int64_t) i0 + 1, bufferSize_);
                const auto& buffer = buffers_[(size_t) ch];

                const float sample = buffer[(size_t) a] * (1.0f - frac)
                                   + buffer[(size_t) b] * frac;

                float window = 1.0f;

                if (p < fade)
                    window = 0.5f - 0.5f * std::cos (
                        juce::MathConstants<float>::pi * p / fade);
                else if (p > 1.0f - fade)
                    window = 0.5f - 0.5f * std::cos (
                        juce::MathConstants<float>::pi * (1.0f - p) / fade);

                value += sample * window;
                windowSum += window;
            }

            output.setSample (ch, i, windowSum > 1.0e-5f ? value / windowSum : 0.0f);
        }

        writeIndex_ = (writeIndex_ + 1) % bufferSize_;

        phase_ += phaseStep;
        if (phase_ >= 1.0f)
            phase_ -= 1.0f;
        else if (phase_ < 0.0f)
            phase_ += 1.0f;
    }
}

//==============================================================================
// PSOLA-style pitch-synchronous overlap-add shifter
//==============================================================================
void PsolaPitchShifter::prepare (const juce::dsp::ProcessSpec& spec)
{
    numChannels_ = juce::jmax (1, (int) spec.numChannels);
    maxBlockSize_ = juce::jmax (1, (int) spec.maximumBlockSize);

    grainSamples_ = juce::jmax (128, (int) (spec.sampleRate * 0.010)); // 10 ms
    latencySamples_ = grainSamples_ / 2;                             // 5 ms
    inputSize_ = grainSamples_ * 6 + maxBlockSize_ + 8;
    outputSize_ = grainSamples_ * 2 + maxBlockSize_ + 8;

    inputBuffers_.assign ((size_t) numChannels_,
                          std::vector<float> ((size_t) inputSize_, 0.0f));
    pendingBuffers_.assign ((size_t) numChannels_,
                            std::vector<float> ((size_t) outputSize_, 0.0f));
    windowSums_.assign ((size_t) numChannels_,
                        std::vector<float> ((size_t) outputSize_, 0.0f));

    window_.resize ((size_t) grainSamples_);
    for (int i = 0; i < grainSamples_; ++i)
        window_[(size_t) i] = hann (i, grainSamples_);

    reset();
}

void PsolaPitchShifter::reset()
{
    for (auto& buffer : inputBuffers_)
        std::fill (buffer.begin(), buffer.end(), 0.0f);
    for (auto& buffer : pendingBuffers_)
        std::fill (buffer.begin(), buffer.end(), 0.0f);
    for (auto& buffer : windowSums_)
        std::fill (buffer.begin(), buffer.end(), 0.0f);

    inputCounter_ = 0;
    outputCounter_ = 0;
    readCenter_ = 0.0f;
    samplesUntilGrain_ = 0.0f;
    periodSamples_ = (float) grainSamples_ * 0.5f; // 5 ms default
    ratio_ = 1.0f;
}

void PsolaPitchShifter::setTransposeSemitones (float semitones)
{
    ratio_ = std::pow (2.0f, semitones / 12.0f);
}

void PsolaPitchShifter::setPitchPeriodSamples (float periodSamples, bool immediate)
{
    if (periodSamples <= 0.0f)
        return;

    // Allow long periods (low notes) even though the fixed grain is shorter.
    // The output spacing still follows the true period, so the pitch stays
    // correct; only the grain content becomes less pitch-synchronous.
    const float maxPeriod = (float) (inputSize_ - grainSamples_ - 16);
    const float target = juce::jlimit (64.0f, maxPeriod, periodSamples);

    if (immediate || periodSamples_ <= 0.0f)
        periodSamples_ = target;
    else
        periodSamples_ += 0.2f * (target - periodSamples_);
}

void PsolaPitchShifter::scheduleGrain (int64_t outputPosition)
{
    const int half = grainSamples_ / 2;

    for (int j = 0; j < grainSamples_; ++j)
    {
        const float inputPos = readCenter_ - (float) half + (float) j;
        int inputIndex = (int) std::floor (inputPos);
        const float frac = inputPos - (float) inputIndex;

        const int inA = wrapIndex (inputIndex, inputSize_);
        const int inB = wrapIndex ((int64_t) inputIndex + 1, inputSize_);
        const int outIndex = wrapIndex (outputPosition - half + j, outputSize_);
        const float window = window_[(size_t) j];

        for (int ch = 0; ch < numChannels_; ++ch)
        {
            const auto& input = inputBuffers_[(size_t) ch];
            const float sample = input[(size_t) inA] * (1.0f - frac)
                               + input[(size_t) inB] * frac;

            pendingBuffers_[(size_t) ch][(size_t) outIndex] += sample * window;
            windowSums_[(size_t) ch][(size_t) outIndex] += window;
        }
    }
}

void PsolaPitchShifter::process (const juce::AudioBuffer<float>& input,
                                 juce::AudioBuffer<float>& output,
                                 int numSamples)
{
    if (inputBuffers_.empty() || numSamples <= 0)
        return;

    const int channels = juce::jmin ((int) inputBuffers_.size(),
                                     input.getNumChannels(),
                                     output.getNumChannels());
    const int samples = juce::jmin (numSamples,
                                    input.getNumSamples(),
                                    output.getNumSamples());

    if (channels <= 0 || samples <= 0)
        return;

    const float spacing = juce::jmax (1.0f, periodSamples_ / ratio_);

    for (int i = 0; i < samples; ++i)
    {
        for (int ch = 0; ch < channels; ++ch)
            inputBuffers_[(size_t) ch][(size_t) (inputCounter_ % inputSize_)] =
                input.getSample (ch, i);

        ++inputCounter_;
        ++outputCounter_;

        if (samplesUntilGrain_ <= 0.0f)
        {
            scheduleGrain (outputCounter_);
            readCenter_ += periodSamples_;

            const float maxCenter = (float) inputCounter_ - (float) latencySamples_;
            if (readCenter_ > maxCenter)
                readCenter_ -= periodSamples_;

            const float minCenter = (float) inputCounter_
                                  - (float) (inputSize_ - grainSamples_);
            if (readCenter_ < minCenter)
                readCenter_ += periodSamples_;

            samplesUntilGrain_ += spacing;
        }

        const int readIndex = wrapIndex (outputCounter_ - latencySamples_, outputSize_);

        for (int ch = 0; ch < channels; ++ch)
        {
            const float sum = windowSums_[(size_t) ch][(size_t) readIndex];
            const float value = sum > 1.0e-6f
                ? pendingBuffers_[(size_t) ch][(size_t) readIndex] / sum
                : 0.0f;

            output.setSample (ch, i, value);

            pendingBuffers_[(size_t) ch][(size_t) readIndex] = 0.0f;
            windowSums_[(size_t) ch][(size_t) readIndex] = 0.0f;
        }

        samplesUntilGrain_ -= 1.0f;
    }
}

} // namespace namberry
