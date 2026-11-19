#include "namberry/StereoImager.h"

#include <algorithm>
#include <cmath>

namespace namberry {

namespace {

// A few milliseconds of allpass phase shift is enough to create side content
// from a mono-derived signal without an audible echo or a feedback tail.
constexpr float kDecorrelatorDelaysMs[] = { 0.7f, 1.3f, 2.1f };
constexpr float kDecorrelatorGain = 0.62f;

// Keep the low end centered so widening does not thin out the guitar.
constexpr float kSideHighPassHz = 140.0f;
constexpr float kSideHighPassQ = 0.707f;

// Width 0.5 maps to unity side gain; 1.0 doubles it.
constexpr float kMaxSideGain = 2.0f;

} // namespace

//==============================================================================
void StereoImager::Allpass::prepare (double sampleRate, float delayMs, float g)
{
    gain = g;
    const int samples = juce::jmax (
        1, static_cast<int> (std::round (sampleRate * delayMs * 0.001)));

    buffer.assign (static_cast<size_t> (samples), 0.0f);
    writeIndex = 0;
}

void StereoImager::Allpass::reset()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writeIndex = 0;
}

float StereoImager::Allpass::processSample (float input)
{
    const auto index = static_cast<size_t> (writeIndex);
    const float delayed = buffer[index];
    const float output = delayed - gain * input;
    buffer[index] = input + gain * output;

    ++writeIndex;
    if (writeIndex >= static_cast<int> (buffer.size()))
        writeIndex = 0;

    return output;
}

//==============================================================================
void StereoImager::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;

    for (int i = 0; i < kNumDecorrelators; ++i)
        decorrelators_[static_cast<size_t> (i)].prepare (
            sampleRate_, kDecorrelatorDelaysMs[i], kDecorrelatorGain);

    sideHighPass_.prepare (spec);
    sideHighPass_.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (
        sampleRate_, kSideHighPassHz, kSideHighPassQ);

    widthSmoothed_.reset (sampleRate_, 0.05);
    mixSmoothed_.reset (sampleRate_, 0.03);
    reset();
}

void StereoImager::reset()
{
    for (auto& allpass : decorrelators_)
        allpass.reset();

    sideHighPass_.reset();
    widthSmoothed_.setCurrentAndTargetValue (width_);
    mixSmoothed_.setCurrentAndTargetValue (mix_);
}

void StereoImager::setWidth (float width)
{
    width_ = juce::jlimit (0.0f, 1.0f, width);
    widthSmoothed_.setTargetValue (width_);
}

void StereoImager::setMix (float mix)
{
    mix_ = juce::jlimit (0.0f, 1.0f, mix);
    mixSmoothed_.setTargetValue (mix_);
}

//==============================================================================
void StereoImager::processBlock (juce::AudioBuffer<float>& buffer)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels < 2)
        return;

    const float mixTarget = mix_;
    const float mixCurrent = mixSmoothed_.getCurrentValue();

    if (mixTarget <= 0.0f && mixCurrent <= 1.0e-6f)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        const float mix = juce::jlimit (0.0f, 1.0f, mixSmoothed_.getNextValue());
        const float width = juce::jlimit (0.0f, 1.0f, widthSmoothed_.getNextValue());

        const float left = buffer.getSample (0, i);
        const float originalRight = buffer.getSample (1, i);

        float decorrelatedRight = originalRight;
        for (auto& allpass : decorrelators_)
            decorrelatedRight = allpass.processSample (decorrelatedRight);

        const float targetSideGain = kMaxSideGain * width;
        const float extraSide = juce::jmax (0.0f, targetSideGain - 1.0f);
        const float featureAmount = mix * extraSide;

        decorrelatedRight = originalRight
                          + (decorrelatedRight - originalRight) * featureAmount;

        const float mid = 0.5f * (left + decorrelatedRight);
        float side = 0.5f * (left - decorrelatedRight);

        const float filteredSide = sideHighPass_.processSample (side);
        side += (filteredSide - side) * featureAmount;

        const float sideGain = 1.0f + (targetSideGain - 1.0f) * mix;
        const float excess = juce::jmax (0.0f, sideGain - 1.0f);
        const float levelComp = 1.0f / std::sqrt (1.0f + 1.5f * excess * excess);

        buffer.setSample (0, i, (mid + side * sideGain) * levelComp);
        buffer.setSample (1, i, (mid - side * sideGain) * levelComp);
    }
}

} // namespace namberry
