#include "namberry/Room.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace namberry {

namespace {

// Sparse early reflections. These are deliberately not evenly spaced so the
// room does not read as a simple multi-tap delay.
constexpr float kEarlyDelaysMs[] = {
    3.1f, 5.3f, 8.9f, 13.3f, 18.7f, 23.9f
};

constexpr float kEarlyGains[] = {
    0.62f, 0.48f, 0.36f, 0.27f, 0.19f, 0.13f
};

// Mutually prime-ish FDN line lengths. The Size macro scales all of them.
constexpr float kFdnBaseDelaysMs[] = {
    3.7f, 4.9f, 6.1f, 7.3f, 8.9f, 10.7f, 12.7f, 14.9f
};

constexpr float kDiffuserDelaysMs[] = {
    4.7f, 3.9f, 3.1f, 2.3f
};

// Size macro endpoints. The low end is deliberately a very short
// doubler/stereoizer; the high end is a small 0.3 s room, not a reverb.
constexpr float kPredelayMinMs = 3.0f;
constexpr float kPredelayMaxMs = 10.0f;
constexpr float kFdnScaleMin = 0.6f;
constexpr float kFdnScaleMax = 1.8f;
constexpr float kErScaleMin = 0.35f;
constexpr float kErScaleMax = 1.0f;
constexpr float kRt60MinSeconds = 0.025f;
constexpr float kRt60MaxSeconds = 0.30f;
constexpr float kDampingMaxHz = 12000.0f;
constexpr float kDampingMinHz = 4000.0f;

constexpr float kErLevel = 0.5f;
// Calibrated so the wet path has roughly the same RMS as the dry signal.
constexpr float kWetTrim = 2.2f;
constexpr float kDiffuserGain = 0.6f;
constexpr float kDcBlockerPole = 0.995f;
constexpr float kInvSqrt8 = 0.3535533905932737622f;
constexpr float kHalfPi = juce::MathConstants<float>::halfPi;

// Orthogonal injection and extraction patterns for the 8x8 FDN. The patterns
// keep the left and right channels decorrelated while preserving energy.
constexpr std::array<float, 8> kInputL  = { 1.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
constexpr std::array<float, 8> kInputR  = { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f };
constexpr std::array<float, 8> kOutputL = { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f };
constexpr std::array<float, 8> kOutputR = { 1.0f, 1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f };

float sizeToPredelayMs (float size)
{
    return kPredelayMinMs + (kPredelayMaxMs - kPredelayMinMs) * size;
}

float sizeToFdnScale (float size)
{
    return kFdnScaleMin + (kFdnScaleMax - kFdnScaleMin) * size;
}

float sizeToErScale (float size)
{
    return kErScaleMin + (kErScaleMax - kErScaleMin) * size;
}

float sizeToRt60 (float size)
{
    return kRt60MinSeconds
         + (kRt60MaxSeconds - kRt60MinSeconds) * std::pow (size, 1.5f);
}

float sizeToDampingHz (float size)
{
    return kDampingMaxHz * std::pow (kDampingMinHz / kDampingMaxHz, size);
}

} // namespace

//==============================================================================
void Room::DelayLine::prepare (int samples)
{
    maxSamples = juce::jmax (4, samples);
    buffer.assign (static_cast<size_t> (maxSamples), 0.0f);
    writeIndex = 0;
}

void Room::DelayLine::reset()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writeIndex = 0;
}

float Room::DelayLine::read (float delaySamples) const
{
    const float delay = juce::jlimit (1.0f, static_cast<float> (maxSamples - 2),
                                      delaySamples);
    const int whole = static_cast<int> (std::floor (delay));
    const float fraction = delay - static_cast<float> (whole);

    int index0 = writeIndex - whole;
    index0 %= maxSamples;
    if (index0 < 0)
        index0 += maxSamples;

    int index1 = index0 - 1;
    if (index1 < 0)
        index1 += maxSamples;

    const float sample0 = buffer[static_cast<size_t> (index0)];
    const float sample1 = buffer[static_cast<size_t> (index1)];
    return sample0 + (sample1 - sample0) * fraction;
}

void Room::DelayLine::write (float sample)
{
    buffer[static_cast<size_t> (writeIndex)] = sample;
    ++writeIndex;
    if (writeIndex >= maxSamples)
        writeIndex = 0;
}

//==============================================================================
void Room::Allpass::prepare (double sampleRate, float delayMs, float g)
{
    delaySamples = juce::jmax (1, static_cast<int> (std::round (sampleRate * delayMs * 0.001)));
    gain = g;

    for (auto& channelBuffer : buffer)
        channelBuffer.assign (static_cast<size_t> (delaySamples), 0.0f);

    writeIndex[0] = 0;
    writeIndex[1] = 0;
}

void Room::Allpass::reset()
{
    for (auto& channelBuffer : buffer)
        std::fill (channelBuffer.begin(), channelBuffer.end(), 0.0f);

    writeIndex[0] = 0;
    writeIndex[1] = 0;
}

float Room::Allpass::processSample (int channel, float input)
{
    const auto c = static_cast<size_t> (channel);
    const int index = writeIndex[c];
    const float delayed = buffer[c][static_cast<size_t> (index)];

    const float output = delayed - gain * input;
    buffer[c][static_cast<size_t> (index)] = input + gain * output;

    ++writeIndex[c];
    if (writeIndex[c] >= delaySamples)
        writeIndex[c] = 0;

    return output;
}

//==============================================================================
void Room::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;

    // Enough history for the maximum pre-delay plus the maximum early
    // reflection offset and stereo spread.
    historySize_ = juce::jmax (16, static_cast<int> (std::ceil (sampleRate_ * 0.10)));
    for (auto& channelHistory : history_)
        channelHistory.assign (static_cast<size_t> (historySize_), 0.0f);
    historyWriteIndex_ = 0;

    const float maxFdnDelayMs = kFdnBaseDelaysMs[7] * kFdnScaleMax + 2.0f;
    const int maxFdnSamples = static_cast<int> (
        std::ceil (sampleRate_ * maxFdnDelayMs * 0.001)) + 4;

    for (auto& line : fdnLines_)
        line.prepare (maxFdnSamples);

    for (int i = 0; i < kNumDiffusers; ++i)
        diffusers_[static_cast<size_t> (i)].prepare (sampleRate_,
                                                      kDiffuserDelaysMs[i],
                                                      kDiffuserGain);

    for (auto& filter : wetHighPass_)
        filter.prepare (spec);
    for (auto& filter : wetMid_)
        filter.prepare (spec);
    for (auto& filter : wetLowPass_)
        filter.prepare (spec);

    sizeSmoothed_.reset (sampleRate_, 0.15);
    blendSmoothed_.reset (sampleRate_, 0.03);

    updateWetTone();
    updateMacroCoefficients (size_);
    reset();
}

void Room::updateWetTone()
{
    const auto highPass = juce::dsp::IIR::Coefficients<float>::makeHighPass (
        sampleRate_, 90.0, 0.707f);
    const auto mid = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
        sampleRate_, 1200.0, 0.8f, juce::Decibels::decibelsToGain (3.0f));
    const auto lowPass = juce::dsp::IIR::Coefficients<float>::makeLowPass (
        sampleRate_, 12000.0, 0.707f);

    for (auto& filter : wetHighPass_)
        filter.coefficients = highPass;
    for (auto& filter : wetMid_)
        filter.coefficients = mid;
    for (auto& filter : wetLowPass_)
        filter.coefficients = lowPass;
}

void Room::updateMacroCoefficients (float size)
{
    const float s = juce::jlimit (0.0f, 1.0f, size);
    const float fdnScale = sizeToFdnScale (s);
    const float rt60 = juce::jmax (0.1f, sizeToRt60 (s));
    const float dampingHz = sizeToDampingHz (s);
    const float sampleRate = static_cast<float> (sampleRate_);

    const float dampingCoeff = 1.0f - std::exp (
        -juce::MathConstants<float>::twoPi * dampingHz / sampleRate);

    float gSquaredSum = 0.0f;

    for (int line = 0; line < kNumLines; ++line)
    {
        const float delaySeconds = kFdnBaseDelaysMs[line] * fdnScale * 0.001f;
        const float feedback = std::pow (10.0f, -3.0f * delaySeconds / rt60);
        const float clamped = juce::jlimit (0.0f, 0.9995f, feedback);

        fdnFeedback_[static_cast<size_t> (line)] = clamped;
        fdnDampingCoeff_[static_cast<size_t> (line)] = dampingCoeff;
        gSquaredSum += clamped * clamped;
    }

    // Normalise the FDN input so a longer decay does not simply get louder.
    const float gRms = std::sqrt (gSquaredSum / static_cast<float> (kNumLines));
    fdnInputTrim_ = std::sqrt (juce::jmax (0.0f, 1.0f - gRms * gRms));
}

void Room::reset()
{
    for (auto& channelHistory : history_)
        std::fill (channelHistory.begin(), channelHistory.end(), 0.0f);
    historyWriteIndex_ = 0;

    for (auto& line : fdnLines_)
        line.reset();

    fdnDampingState_.fill (0.0f);
    dcPreviousInput_.fill (0.0f);
    dcPreviousOutput_.fill (0.0f);

    for (auto& diffuser : diffusers_)
        diffuser.reset();

    for (auto& filter : wetHighPass_)
        filter.reset();
    for (auto& filter : wetMid_)
        filter.reset();
    for (auto& filter : wetLowPass_)
        filter.reset();

    sizeSmoothed_.setCurrentAndTargetValue (size_);
    blendSmoothed_.setCurrentAndTargetValue (blend_);
}

//==============================================================================
void Room::setSize (float size)
{
    size_ = juce::jlimit (0.0f, 1.0f, size);
    sizeSmoothed_.setTargetValue (size_);
}

void Room::setBlend (float blend)
{
    blend_ = juce::jlimit (0.0f, 1.0f, blend);
    blendSmoothed_.setTargetValue (blend_);
}

//==============================================================================
float Room::readHistory (int channel, float delaySamples) const
{
    const float delay = juce::jlimit (1.0f, static_cast<float> (historySize_ - 2),
                                      delaySamples);
    const int whole = static_cast<int> (std::floor (delay));
    const float fraction = delay - static_cast<float> (whole);

    int index0 = historyWriteIndex_ - whole;
    index0 %= historySize_;
    if (index0 < 0)
        index0 += historySize_;

    int index1 = index0 - 1;
    if (index1 < 0)
        index1 += historySize_;

    const auto& channelHistory = history_[static_cast<size_t> (channel)];
    const float sample0 = channelHistory[static_cast<size_t> (index0)];
    const float sample1 = channelHistory[static_cast<size_t> (index1)];

    return sample0 + (sample1 - sample0) * fraction;
}

float Room::processDcBlock (int channel, float sample)
{
    const auto c = static_cast<size_t> (channel);
    const float output = sample - dcPreviousInput_[c]
                       + kDcBlockerPole * dcPreviousOutput_[c];
    dcPreviousInput_[c] = sample;
    dcPreviousOutput_[c] = output;
    return output;
}

//==============================================================================
void Room::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (2, buffer.getNumChannels());

    if (numSamples <= 0 || numChannels <= 0 || historySize_ < 4)
        return;

    const float wetTarget = blend_;
    const float wetCurrent = blendSmoothed_.getCurrentValue();

    if (wetTarget <= 0.0f && wetCurrent <= 1.0e-6f)
        return;

    const float sampleRate = static_cast<float> (sampleRate_);
    updateMacroCoefficients (sizeSmoothed_.getCurrentValue());

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        const float size = sizeSmoothed_.getNextValue();
        const float mix = blendSmoothed_.getNextValue();
        const float dryGain = std::cos (kHalfPi * mix);
        const float wetGain = std::sin (kHalfPi * mix) * kWetTrim;

        const float dryL = buffer.getSample (0, sampleIndex);
        const float dryR = numChannels > 1 ? buffer.getSample (1, sampleIndex) : dryL;

        history_[0][static_cast<size_t> (historyWriteIndex_)] = dryL;
        history_[1][static_cast<size_t> (historyWriteIndex_)] = dryR;
        historyWriteIndex_ = (historyWriteIndex_ + 1) % historySize_;

        const float predelaySamples = sizeToPredelayMs (size) * 0.001f * sampleRate;
        const float erScale = sizeToErScale (size);
        const float erDirect = 0.65f + 0.2f * size;
        const float erCross = 1.0f - erDirect;

        float wetL = 0.0f;
        float wetR = 0.0f;

        for (int tap = 0; tap < kNumERTaps; ++tap)
        {
            const float tapSamples = kEarlyDelaysMs[tap] * erScale * 0.001f * sampleRate;
            const float spreadSamples = 0.0008f * static_cast<float> (tap + 1) * sampleRate;
            const float delayL = predelaySamples + tapSamples + spreadSamples;
            const float delayR = predelaySamples + tapSamples - spreadSamples;

            const float sourceL = readHistory (0, delayL);
            const float sourceR = readHistory (1, delayR);
            const float gain = kEarlyGains[tap] * kErLevel;

            wetL += sourceL * erDirect * gain + sourceR * erCross * gain;
            wetR += sourceR * erDirect * gain + sourceL * erCross * gain;
        }

        float preL = readHistory (0, predelaySamples);
        float preR = readHistory (1, predelaySamples);

        // Input diffusion. This keeps the FDN from sounding like a handful of
        // distinct echoes.
        for (auto& diffuser : diffusers_)
        {
            preL = diffuser.processSample (0, preL);
            preR = diffuser.processSample (1, preR);
        }

        preL = processDcBlock (0, preL);
        preR = processDcBlock (1, preR);

        const float fdnScale = sizeToFdnScale (size);

        std::array<float, kNumLines> v {};

        for (int line = 0; line < kNumLines; ++line)
        {
            const float delaySamples = juce::jmax (
                1.0f, kFdnBaseDelaysMs[line] * fdnScale * 0.001f * sampleRate);
            const float delayed = fdnLines_[static_cast<size_t> (line)].read (delaySamples);

            float& dampingState = fdnDampingState_[static_cast<size_t> (line)];
            dampingState += fdnDampingCoeff_[static_cast<size_t> (line)]
                          * (delayed - dampingState);

            v[static_cast<size_t> (line)] = dampingState
                                          * fdnFeedback_[static_cast<size_t> (line)];
        }

        // Fast Walsh-Hadamard transform: the 8x8 orthogonal feedback matrix.
        for (int step = 1; step < kNumLines; step <<= 1)
        {
            for (int base = 0; base < kNumLines; base += step << 1)
            {
                for (int offset = 0; offset < step; ++offset)
                {
                    const float a = v[static_cast<size_t> (base + offset)];
                    const float b = v[static_cast<size_t> (base + offset + step)];
                    v[static_cast<size_t> (base + offset)] = a + b;
                    v[static_cast<size_t> (base + offset + step)] = a - b;
                }
            }
        }

        for (auto& value : v)
            value *= kInvSqrt8;

        float fdnL = 0.0f;
        float fdnR = 0.0f;

        for (int line = 0; line < kNumLines; ++line)
        {
            fdnL += v[static_cast<size_t> (line)] * kOutputL[static_cast<size_t> (line)];
            fdnR += v[static_cast<size_t> (line)] * kOutputR[static_cast<size_t> (line)];
        }

        fdnL *= kInvSqrt8;
        fdnR *= kInvSqrt8;

        for (int line = 0; line < kNumLines; ++line)
        {
            const float injected = (preL * kInputL[static_cast<size_t> (line)]
                                  + preR * kInputR[static_cast<size_t> (line)])
                                 * kInvSqrt8 * fdnInputTrim_;

            fdnLines_[static_cast<size_t> (line)].write (
                v[static_cast<size_t> (line)] + injected);
        }

        wetL += fdnL;
        wetR += fdnR;

        // Keep the room mid-focused: remove sub-bass and extreme fizz, then
        // lift the presence region that reads as "room" in a mix.
        wetL = wetHighPass_[0].processSample (wetL);
        wetL = wetMid_[0].processSample (wetL);
        wetL = wetLowPass_[0].processSample (wetL);

        wetR = wetHighPass_[1].processSample (wetR);
        wetR = wetMid_[1].processSample (wetR);
        wetR = wetLowPass_[1].processSample (wetR);

        // Small sizes are deliberately wider: this is what makes the doubler
        // setting feel bigger on headphones. The width gain compensates the
        // level change so the overall loudness stays roughly constant.
        const float width = 1.7f - 0.7f * size;
        const float widthGain = std::sqrt (2.0f / (1.0f + width * width));
        const float midSignal = 0.5f * (wetL + wetR);
        const float sideSignal = 0.5f * (wetL - wetR) * width;
        wetL = (midSignal + sideSignal) * widthGain;
        wetR = (midSignal - sideSignal) * widthGain;

        buffer.setSample (0, sampleIndex, dryL * dryGain + wetL * wetGain);

        if (numChannels > 1)
            buffer.setSample (1, sampleIndex, dryR * dryGain + wetR * wetGain);
    }
}

} // namespace namberry
