#include "namberry/PostAmpEQ.h"

#include <algorithm>
#include <cmath>

namespace namberry {

namespace {

double clampFrequency (float frequency, double sampleRate)
{
    const double nyquistLimit = juce::jmax (20.0, sampleRate * 0.49);
    return juce::jlimit (20.0, nyquistLimit, static_cast<double> (frequency));
}

float clampQ (float q)
{
    return juce::jlimit (0.1f, 10.0f, q);
}

float clampGainDb (float gainDb)
{
    return juce::jlimit (-24.0f, 24.0f, gainDb);
}

bool sameValue (float a, float b)
{
    return juce::approximatelyEqual (a, b);
}

} // namespace

//==============================================================================
PostAmpEQ::BandType PostAmpEQ::bandTypeFromInt (int value)
{
    switch (value)
    {
        case static_cast<int> (BandType::HighPass): return BandType::HighPass;
        case static_cast<int> (BandType::Band):     return BandType::Band;
        default:                                    return BandType::LowPass;
    }
}

int PostAmpEQ::bandTypeToInt (BandType type)
{
    return static_cast<int> (type);
}

//==============================================================================
PostAmpEQ::PostAmpEQ (const Settings& settings)
    : bands_ (settings.bands),
      highPassFrequency_ (settings.highPassFrequency),
      highPassQ_ (settings.highPassQ),
      lowPassFrequency_ (settings.lowPassFrequency),
      lowPassQ_ (settings.lowPassQ)
{
}

//==============================================================================
void PostAmpEQ::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;

    const auto numChannels = static_cast<size_t> (juce::jmax (1u, spec.numChannels));

    for (auto& band : filters_)
    {
        band.clear();
        band.reserve (numChannels);

        for (size_t channel = 0; channel < numChannels; ++channel)
            band.push_back (std::make_unique<juce::dsp::IIR::Filter<float>>());
    }

    highPassFilters_.clear();
    lowPassFilters_.clear();
    highPassFilters_.reserve (numChannels);
    lowPassFilters_.reserve (numChannels);

    for (size_t channel = 0; channel < numChannels; ++channel)
    {
        highPassFilters_.push_back (std::make_unique<juce::dsp::IIR::Filter<float>>());
        lowPassFilters_.push_back (std::make_unique<juce::dsp::IIR::Filter<float>>());
    }

    for (int band = 0; band < kNumBands; ++band)
        updateBandCoefficients (band);

    updatePassCoefficients();
}

void PostAmpEQ::reset()
{
    for (auto& band : filters_)
        for (auto& filter : band)
            if (filter != nullptr)
                filter->reset();

    for (auto& filter : highPassFilters_)
        if (filter != nullptr)
            filter->reset();

    for (auto& filter : lowPassFilters_)
        if (filter != nullptr)
            filter->reset();
}

//==============================================================================
void PostAmpEQ::setBandParameters (int bandIdx, float frequency, float gainDb,
                                   float q, BandType type)
{
    if (bandIdx < 0 || bandIdx >= kNumBands)
        return;

    const BandParams next {
        static_cast<float> (clampFrequency (frequency, sampleRate_)),
        clampGainDb (gainDb),
        clampQ (q),
        type
    };

    auto& current = bands_[static_cast<size_t> (bandIdx)];

    if (current.type == next.type
        && sameValue (current.frequency, next.frequency)
        && sameValue (current.gainDb, next.gainDb)
        && sameValue (current.q, next.q))
        return;

    current = next;
    updateBandCoefficients (bandIdx);
}

void PostAmpEQ::setHighPassParameters (float frequencyHz, float q)
{
    const auto frequency = static_cast<float> (clampFrequency (frequencyHz, sampleRate_));
    const auto nextQ = clampQ (q);

    if (sameValue (frequency, highPassFrequency_) && sameValue (nextQ, highPassQ_))
        return;

    highPassFrequency_ = frequency;
    highPassQ_ = nextQ;
    updatePassCoefficients();
}

void PostAmpEQ::setLowPassParameters (float frequencyHz, float q)
{
    const auto frequency = static_cast<float> (clampFrequency (frequencyHz, sampleRate_));
    const auto nextQ = clampQ (q);

    if (sameValue (frequency, lowPassFrequency_) && sameValue (nextQ, lowPassQ_))
        return;

    lowPassFrequency_ = frequency;
    lowPassQ_ = nextQ;
    updatePassCoefficients();
}

PostAmpEQ::BandParams PostAmpEQ::getBandParameters (int bandIdx) const
{
    if (bandIdx < 0 || bandIdx >= kNumBands)
        return { 1000.0f, 0.0f, 1.0f, BandType::Band };

    return bands_[static_cast<size_t> (bandIdx)];
}

//==============================================================================
juce::dsp::IIR::Coefficients<float>::Ptr PostAmpEQ::makeBandCoefficients (
    double sampleRate, const BandParams& band)
{
    const auto frequency = clampFrequency (band.frequency, sampleRate);
    const auto gain = juce::Decibels::decibelsToGain (clampGainDb (band.gainDb));
    const auto q = clampQ (band.q);

    switch (band.type)
    {
        case BandType::LowPass:
            return juce::dsp::IIR::Coefficients<float>::makeLowShelf (
                sampleRate, frequency, q, gain);

        case BandType::HighPass:
            return juce::dsp::IIR::Coefficients<float>::makeHighShelf (
                sampleRate, frequency, q, gain);

        case BandType::Band:
        default:
            return juce::dsp::IIR::Coefficients<float>::makePeakFilter (
                sampleRate, frequency, q, gain);
    }
}

juce::dsp::IIR::Coefficients<float>::Ptr PostAmpEQ::makeHighPassCoefficients (
    float frequency, float q, double sampleRate)
{
    return juce::dsp::IIR::Coefficients<float>::makeHighPass (
        sampleRate, clampFrequency (frequency, sampleRate), clampQ (q));
}

juce::dsp::IIR::Coefficients<float>::Ptr PostAmpEQ::makeLowPassCoefficients (
    float frequency, float q, double sampleRate)
{
    return juce::dsp::IIR::Coefficients<float>::makeLowPass (
        sampleRate, clampFrequency (frequency, sampleRate), clampQ (q));
}

void PostAmpEQ::updateBandCoefficients (int bandIdx)
{
    if (bandIdx < 0 || bandIdx >= kNumBands)
        return;

    const auto coefficients = makeBandCoefficients (
        sampleRate_, bands_[static_cast<size_t> (bandIdx)]);

    for (auto& filter : filters_[static_cast<size_t> (bandIdx)])
        if (filter != nullptr)
            filter->coefficients = coefficients;
}

void PostAmpEQ::updatePassCoefficients()
{
    const auto highPass = makeHighPassCoefficients (
        highPassFrequency_, highPassQ_, sampleRate_);
    const auto lowPass = makeLowPassCoefficients (
        lowPassFrequency_, lowPassQ_, sampleRate_);

    for (auto& filter : highPassFilters_)
        if (filter != nullptr)
            filter->coefficients = highPass;

    for (auto& filter : lowPassFilters_)
        if (filter != nullptr)
            filter->coefficients = lowPass;
}

//==============================================================================
void PostAmpEQ::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    if (bypass_ || buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0)
        return;

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    for (int channel = 0; channel < numChannels; ++channel)
    {
        if (channel >= static_cast<int> (highPassFilters_.size()))
            continue;

        auto* filter = highPassFilters_[static_cast<size_t> (channel)].get();
        if (filter == nullptr)
            continue;

        auto* data = buffer.getWritePointer (channel);
        for (int sample = 0; sample < numSamples; ++sample)
            data[sample] = filter->processSample (data[sample]);
    }

    for (int band = 0; band < kNumBands; ++band)
    {
        auto& bandFilters = filters_[static_cast<size_t> (band)];

        for (int channel = 0; channel < numChannels; ++channel)
        {
            if (channel >= static_cast<int> (bandFilters.size()))
                continue;

            auto* filter = bandFilters[static_cast<size_t> (channel)].get();
            if (filter == nullptr)
                continue;

            auto* data = buffer.getWritePointer (channel);
            for (int sample = 0; sample < numSamples; ++sample)
                data[sample] = filter->processSample (data[sample]);
        }
    }

    for (int channel = 0; channel < numChannels; ++channel)
    {
        if (channel >= static_cast<int> (lowPassFilters_.size()))
            continue;

        auto* filter = lowPassFilters_[static_cast<size_t> (channel)].get();
        if (filter == nullptr)
            continue;

        auto* data = buffer.getWritePointer (channel);
        for (int sample = 0; sample < numSamples; ++sample)
            data[sample] = filter->processSample (data[sample]);
    }
}

//==============================================================================
void PostAmpEQ::getResponseDb (const Settings& settings, double sampleRate,
                               const double* frequencies, float* outDb, int numFrequencies)
{
    if (frequencies == nullptr || outDb == nullptr || numFrequencies <= 0)
        return;

    const double safeSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    std::vector<double> magnitudes (static_cast<size_t> (numFrequencies), 1.0);
    std::vector<double> scratch (static_cast<size_t> (numFrequencies), 1.0);

    auto accumulate = [&] (const juce::dsp::IIR::Coefficients<float>::Ptr& coefficients)
    {
        if (coefficients == nullptr)
            return;

        coefficients->getMagnitudeForFrequencyArray (
            frequencies, scratch.data(), static_cast<size_t> (numFrequencies), safeSampleRate);

        for (int i = 0; i < numFrequencies; ++i)
            magnitudes[static_cast<size_t> (i)] *= scratch[static_cast<size_t> (i)];
    };

    for (int band = 0; band < kNumBands; ++band)
        accumulate (makeBandCoefficients (
            safeSampleRate, settings.bands[static_cast<size_t> (band)]));

    accumulate (makeHighPassCoefficients (settings.highPassFrequency,
                                          settings.highPassQ, safeSampleRate));
    accumulate (makeLowPassCoefficients (settings.lowPassFrequency,
                                         settings.lowPassQ, safeSampleRate));

    for (int i = 0; i < numFrequencies; ++i)
        outDb[i] = juce::Decibels::gainToDecibels (
            static_cast<float> (magnitudes[static_cast<size_t> (i)]), -120.0f);
}

float PostAmpEQ::getResponseDb (const Settings& settings, double sampleRate, double frequency)
{
    float result = 0.0f;
    getResponseDb (settings, sampleRate, &frequency, &result, 1);
    return result;
}

} // namespace namberry
