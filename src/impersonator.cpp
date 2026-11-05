#include "namberry/Impersonator.h"

#include <cmath>

namespace namberry {

namespace {
float dbToGain (float db) { return juce::Decibels::decibelsToGain (db); }
}

//==============================================================================
void Impersonator::FilterChain::prepare (const juce::dsp::ProcessSpec& spec)
{
    for (auto& channel : filters)
        for (auto& filter : channel)
            filter.prepare (spec);
}

void Impersonator::FilterChain::reset()
{
    for (auto& channel : filters)
        for (auto& filter : channel)
            filter.reset();
}

float Impersonator::FilterChain::processSample (int channel, float sample) noexcept
{
    for (auto& filter : filters[static_cast<size_t> (channel)])
        sample = filter.processSample (sample);

    return sample;
}

//==============================================================================
void Impersonator::updateCoefficients()
{
    using Coefficients = juce::dsp::IIR::Coefficients<float>;

    const auto sr = sampleRate_;

    // Bridge -> neck: add body, remove bridge bite and fizz.
    const auto b2nHpf      = Coefficients::makeHighPass (sr, 80.0, 0.707f);
    const auto b2nLowShelf = Coefficients::makeLowShelf (
        sr, 180.0, 0.707f, dbToGain (4.5f));
    const auto b2nLowMid   = Coefficients::makePeakFilter (
        sr, 450.0, 0.8f, dbToGain (2.0f));
    const auto b2nUpperMid = Coefficients::makePeakFilter (
        sr, 3000.0, 0.9f, dbToGain (-4.0f));
    const auto b2nHighShelf = Coefficients::makeHighShelf (
        sr, 4000.0, 0.707f, dbToGain (-8.0f));
    const auto b2nLpf      = Coefficients::makeLowPass (sr, 5000.0, 0.707f);

    for (int ch = 0; ch < 2; ++ch)
    {
        bridgeToNeck_.filters[static_cast<size_t> (ch)][0].coefficients = b2nHpf;
        bridgeToNeck_.filters[static_cast<size_t> (ch)][1].coefficients = b2nLowShelf;
        bridgeToNeck_.filters[static_cast<size_t> (ch)][2].coefficients = b2nLowMid;
        bridgeToNeck_.filters[static_cast<size_t> (ch)][3].coefficients = b2nUpperMid;
        bridgeToNeck_.filters[static_cast<size_t> (ch)][4].coefficients = b2nHighShelf;
        bridgeToNeck_.filters[static_cast<size_t> (ch)][5].coefficients = b2nLpf;
    }

    // Neck -> bridge: tighten lows, remove boxiness, restore bite.
    const auto n2bHpf      = Coefficients::makeHighPass (sr, 130.0, 0.707f);
    const auto n2bLowShelf = Coefficients::makeLowShelf (
        sr, 180.0, 0.707f, 1.0f); // flat shelf keeps the chain structure uniform
    const auto n2bLowMid   = Coefficients::makePeakFilter (
        sr, 400.0, 0.8f, dbToGain (-5.0f));
    const auto n2bUpperMid = Coefficients::makePeakFilter (
        sr, 3000.0, 0.9f, dbToGain (4.0f));
    const auto n2bHighShelf = Coefficients::makeHighShelf (
        sr, 5500.0, 0.707f, dbToGain (3.0f));
    const auto n2bLpf      = Coefficients::makeLowPass (
        sr, juce::jmin (20000.0, sr * 0.45), 0.707f);

    for (int ch = 0; ch < 2; ++ch)
    {
        neckToBridge_.filters[static_cast<size_t> (ch)][0].coefficients = n2bHpf;
        neckToBridge_.filters[static_cast<size_t> (ch)][1].coefficients = n2bLowShelf;
        neckToBridge_.filters[static_cast<size_t> (ch)][2].coefficients = n2bLowMid;
        neckToBridge_.filters[static_cast<size_t> (ch)][3].coefficients = n2bUpperMid;
        neckToBridge_.filters[static_cast<size_t> (ch)][4].coefficients = n2bHighShelf;
        neckToBridge_.filters[static_cast<size_t> (ch)][5].coefficients = n2bLpf;
    }
}

//==============================================================================
void Impersonator::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;

    bridgeToNeck_.prepare (spec);
    neckToBridge_.prepare (spec);
    updateCoefficients();

    modeSmoothed_.reset (sampleRate_, 0.020);
    levelSmoothed_.reset (sampleRate_, 0.020);

    modeSmoothed_.setCurrentAndTargetValue (
        targetMode_ == Mode::bridgeToNeck ? 0.0f : 1.0f);
    levelSmoothed_.setCurrentAndTargetValue (dbToGain (outputLevelDb_));

    reset();
}

void Impersonator::reset()
{
    bridgeToNeck_.reset();
    neckToBridge_.reset();

    modeSmoothed_.setCurrentAndTargetValue (
        targetMode_ == Mode::bridgeToNeck ? 0.0f : 1.0f);
    levelSmoothed_.setCurrentAndTargetValue (dbToGain (outputLevelDb_));
}

//==============================================================================
void Impersonator::setMode (Mode mode)
{
    targetMode_ = mode;
    modeSmoothed_.setTargetValue (mode == Mode::bridgeToNeck ? 0.0f : 1.0f);
}

void Impersonator::setBypass (bool bypassed)
{
    if (bypassed == bypass_)
        return;

    bypass_ = bypassed;

    // Clear the IIR state on both directions so re-enabling never replays
    // stale filter tails.
    reset();
}

void Impersonator::setOutputLevelDb (float db)
{
    outputLevelDb_ = juce::jlimit (-12.0f, 12.0f, db);
    levelSmoothed_.setTargetValue (dbToGain (outputLevelDb_));
}

//==============================================================================
void Impersonator::processBlock (juce::AudioBuffer<float>& buffer,
                                  juce::MidiBuffer& /*midi*/)
{
    if (bypass_ || buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (2, buffer.getNumChannels());

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const float mode = modeSmoothed_.getNextValue();
        const float level = levelSmoothed_.getNextValue();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float dry = buffer.getSample (channel, sample);
            const float toNeck = bridgeToNeck_.processSample (channel, dry);
            const float toBridge = neckToBridge_.processSample (channel, dry);
            const float wet = toNeck + (toBridge - toNeck) * mode;

            buffer.setSample (channel, sample, wet * level);
        }
    }
}

} // namespace namberry
