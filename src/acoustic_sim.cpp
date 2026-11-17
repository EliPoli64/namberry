#include "namberry/AcousticSim.h"

#include <cmath>

namespace namberry {

namespace {
float dbToGain (float db) { return juce::Decibels::decibelsToGain (db); }
}

//==============================================================================
void AcousticSim::FilterChain::prepare (const juce::dsp::ProcessSpec& spec)
{
    for (auto& channel : filters)
        for (auto& filter : channel)
            filter.prepare (spec);
}

void AcousticSim::FilterChain::reset()
{
    for (auto& channel : filters)
        for (auto& filter : channel)
            filter.reset();
}

float AcousticSim::FilterChain::processSample (int channel, float sample) noexcept
{
    for (auto& filter : filters[static_cast<size_t> (channel)])
        sample = filter.processSample (sample);

    return sample;
}

//==============================================================================
float AcousticSim::modeWeight (Mode mode, int index) noexcept
{
    const int target = mode == Mode::jumbo    ? 0
                     : mode == Mode::enhanced ? 1
                     : mode == Mode::piezo    ? 2
                                              : -1;

    return index == target ? 1.0f : 0.0f;
}

void AcousticSim::updateModeCoefficients()
{
    using Coefficients = juce::dsp::IIR::Coefficients<float>;

    const auto sr = sampleRate_;

    // Unity sections keep every mode on the same 7-biquad topology.
    const auto transparentLpf = Coefficients::makeLowPass (
        sr, juce::jmin (20000.0, sr * 0.45), 0.7071);
    const auto flatPeak = Coefficients::makePeakFilter (sr, 1000.0, 1.0, 1.0);

    // Standard: balanced dreadnought — body around 220/450 Hz, mild honk dip.
    const Coefficients::Ptr standard[] = {
        Coefficients::makeHighPass (sr, 80.0, 0.7071),
        Coefficients::makeLowShelf (sr, 120.0, 0.7071, dbToGain (2.5f)),
        Coefficients::makePeakFilter (sr, 220.0, 1.2, dbToGain (3.0f)),
        Coefficients::makePeakFilter (sr, 450.0, 1.0, dbToGain (2.0f)),
        Coefficients::makePeakFilter (sr, 900.0, 1.0, dbToGain (-2.5f)),
        Coefficients::makeHighShelf (sr, 5000.0, 0.7071, dbToGain (3.0f)),
        transparentLpf
    };

    // Jumbo: warm and big-bottomed, softer top.
    const Coefficients::Ptr jumbo[] = {
        Coefficients::makeHighPass (sr, 70.0, 0.7071),
        Coefficients::makeLowShelf (sr, 110.0, 0.7071, dbToGain (4.0f)),
        Coefficients::makePeakFilter (sr, 190.0, 1.5, dbToGain (3.5f)),
        Coefficients::makePeakFilter (sr, 800.0, 1.0, dbToGain (-3.0f)),
        Coefficients::makePeakFilter (sr, 2000.0, 1.2, dbToGain (1.5f)),
        Coefficients::makeHighShelf (sr, 5500.0, 0.7071, dbToGain (1.5f)),
        Coefficients::makeLowPass (sr, 12000.0, 0.7071)
    };

    // Enhanced: bright, "recorded" acoustic with extra air.
    const Coefficients::Ptr enhanced[] = {
        Coefficients::makeHighPass (sr, 85.0, 0.7071),
        Coefficients::makeLowShelf (sr, 130.0, 0.7071, dbToGain (1.5f)),
        Coefficients::makePeakFilter (sr, 250.0, 1.0, dbToGain (2.0f)),
        Coefficients::makePeakFilter (sr, 1500.0, 1.0, dbToGain (3.5f)),
        Coefficients::makePeakFilter (sr, 8000.0, 1.0, dbToGain (2.0f)),
        Coefficients::makeHighShelf (sr, 6000.0, 0.7071, dbToGain (5.0f)),
        transparentLpf
    };

    // Piezo: thin, direct and quacky — the "cut through the mix" sound.
    const Coefficients::Ptr piezo[] = {
        Coefficients::makeHighPass (sr, 120.0, 0.7071),
        Coefficients::makeLowShelf (sr, 150.0, 0.7071, dbToGain (-2.0f)),
        Coefficients::makePeakFilter (sr, 500.0, 1.2, dbToGain (-3.0f)),
        Coefficients::makePeakFilter (sr, 2800.0, 1.4, dbToGain (5.0f)),
        flatPeak,
        Coefficients::makeHighShelf (sr, 4000.0, 0.7071, dbToGain (5.0f)),
        Coefficients::makeLowPass (sr, 11000.0, 0.7071)
    };

    const Coefficients::Ptr* tables[numModes] = { standard, jumbo, enhanced, piezo };

    for (int mode = 0; mode < numModes; ++mode)
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& filters = modeChains_[static_cast<size_t> (mode)]
                                        .filters[static_cast<size_t> (ch)];

            for (int section = 0; section < numSections; ++section)
                filters[static_cast<size_t> (section)].coefficients = tables[mode][section];
        }
}

void AcousticSim::updateSectionCoefficients()
{
    using Coefficients = juce::dsp::IIR::Coefficients<float>;

    const auto sr = sampleRate_;
    const auto unity = Coefficients::makePeakFilter (sr, 1000.0, 1.0, 1.0);

    // Body: modal soundboard resonances plus the electric-honk dip.
    const Coefficients::Ptr body[] = {
        Coefficients::makePeakFilter (sr, 95.0, 2.0, dbToGain (5.0f)),
        Coefficients::makePeakFilter (sr, 205.0, 1.8, dbToGain (4.5f)),
        Coefficients::makePeakFilter (sr, 430.0, 1.6, dbToGain (3.5f)),
        Coefficients::makePeakFilter (sr, 850.0, 1.0, dbToGain (-2.5f)),
        unity, unity, unity
    };

    // Top: brilliance and air above the magnetic-pickup roll-off.
    const Coefficients::Ptr top[] = {
        Coefficients::makeHighShelf (sr, 4500.0, 0.7071, dbToGain (6.0f)),
        Coefficients::makePeakFilter (sr, 8000.0, 1.0, dbToGain (2.5f)),
        unity, unity, unity, unity, unity
    };

    // Piezo: quack peak and shelf, plus the band split for the saturation.
    const Coefficients::Ptr piezo[] = {
        Coefficients::makePeakFilter (sr, 2800.0, 1.4, dbToGain (6.0f)),
        Coefficients::makeHighShelf (sr, 4000.0, 0.7071, dbToGain (3.0f)),
        Coefficients::makeHighPass (sr, 2000.0, 0.7071),
        Coefficients::makeLowPass (sr, 5000.0, 0.7071),
        unity, unity, unity
    };

    const Coefficients::Ptr* tables[] = { body, top, piezo };
    FilterChain* chains[] = { &bodyChain_, &topChain_, &piezoChain_ };

    for (int chain = 0; chain < 3; ++chain)
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& filters = chains[chain]->filters[static_cast<size_t> (ch)];

            for (int section = 0; section < numSections; ++section)
                filters[static_cast<size_t> (section)].coefficients = tables[chain][section];
        }
}

//==============================================================================
void AcousticSim::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;

    for (auto& chain : modeChains_)
        chain.prepare (spec);
    bodyChain_.prepare (spec);
    topChain_.prepare (spec);
    piezoChain_.prepare (spec);

    updateModeCoefficients();
    updateSectionCoefficients();

    for (auto& weight : modeWeights_)
        weight.reset (sampleRate_, 0.020);

    topAmount_.reset (sampleRate_, 0.015);
    bodyAmount_.reset (sampleRate_, 0.015);
    piezoAmount_.reset (sampleRate_, 0.015);
    levelSmoothed_.reset (sampleRate_, 0.020);

    reset();
}

void AcousticSim::reset()
{
    for (auto& chain : modeChains_)
        chain.reset();
    bodyChain_.reset();
    topChain_.reset();
    piezoChain_.reset();

    for (int i = 0; i < 3; ++i)
        modeWeights_[(size_t) i].setCurrentAndTargetValue (
            modeWeight (targetMode_, i));

    // Snap every smoothed value to its target so re-enabling starts clean.
    topAmount_.setCurrentAndTargetValue (topAmount_.getTargetValue());
    bodyAmount_.setCurrentAndTargetValue (bodyAmount_.getTargetValue());
    piezoAmount_.setCurrentAndTargetValue (piezoAmount_.getTargetValue());
    levelSmoothed_.setCurrentAndTargetValue (levelSmoothed_.getTargetValue());
}

//==============================================================================
void AcousticSim::setMode (Mode mode)
{
    targetMode_ = mode;

    for (int i = 0; i < 3; ++i)
        modeWeights_[(size_t) i].setTargetValue (modeWeight (mode, i));
}

void AcousticSim::setTop (float amount)
{
    topAmount_.setTargetValue (juce::jlimit (0.0f, 1.0f, amount));
}

void AcousticSim::setBody (float amount)
{
    bodyAmount_.setTargetValue (juce::jlimit (0.0f, 1.0f, amount));
}

void AcousticSim::setPiezo (float amount)
{
    piezoAmount_.setTargetValue (juce::jlimit (0.0f, 1.0f, amount));
}

void AcousticSim::setOutputLevelDb (float db)
{
    levelSmoothed_.setTargetValue (dbToGain (juce::jlimit (-12.0f, 12.0f, db)));
}

void AcousticSim::setBypass (bool bypassed)
{
    if (bypassed == bypass_)
        return;

    bypass_ = bypassed;

    // Clear the IIR state on both directions so re-enabling never replays
    // stale filter tails.
    reset();
}

//==============================================================================
float AcousticSim::processPiezoSection (int channel, float sample) noexcept
{
    auto& filters = piezoChain_.filters[static_cast<size_t> (channel)];

    // Quack peak and shelf first...
    float shaped = filters[0].processSample (sample);
    shaped = filters[1].processSample (shaped);

    // ...then split out the 2-5 kHz band and saturate only that band, so
    // hard strums pick up piezo-preamp harshness without turning the whole
    // signal into distortion.
    float band = filters[2].processSample (shaped);
    band = filters[3].processSample (band);

    const float saturated = std::tanh (band * piezoDrive) / piezoDrive;

    return shaped - band + saturated;
}

//==============================================================================
void AcousticSim::processBlock (juce::AudioBuffer<float>& buffer,
                                juce::MidiBuffer& /*midi*/)
{
    if (bypass_ || buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (2, buffer.getNumChannels());

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const float weightJumbo    = modeWeights_[0].getNextValue();
        const float weightEnhanced = modeWeights_[1].getNextValue();
        const float weightPiezo    = modeWeights_[2].getNextValue();
        const float weightStandard = 1.0f
            - (weightJumbo + weightEnhanced + weightPiezo);

        const float bodyAmt   = bodyAmount_.getNextValue();
        const float topAmt    = topAmount_.getNextValue();
        const float piezoAmt  = piezoAmount_.getNextValue();
        const float level     = levelSmoothed_.getNextValue();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float dry = buffer.getSample (channel, sample);

            float signal = weightStandard
                               * modeChains_[0].processSample (channel, dry)
                         + weightJumbo
                               * modeChains_[1].processSample (channel, dry)
                         + weightEnhanced
                               * modeChains_[2].processSample (channel, dry)
                         + weightPiezo
                               * modeChains_[3].processSample (channel, dry);

            // Each section crossfades toward its own EQ curve, so the knobs
            // move smoothly and an amount of zero stays bit-transparent.
            const float bodyWet = bodyChain_.processSample (channel, signal);
            signal += bodyAmt * (bodyWet - signal);

            const float topWet = topChain_.processSample (channel, signal);
            signal += topAmt * (topWet - signal);

            const float piezoWet = processPiezoSection (channel, signal);
            signal += piezoAmt * (piezoWet - signal);

            buffer.setSample (channel, sample, signal * level);
        }
    }
}

} // namespace namberry
