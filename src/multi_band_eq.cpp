#include "namberry/MultiBandEQ.h"

#include <cmath>

namespace namberry {

void MultiBandEQ::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;

    // Make room for one filter instance per band per channel.
    const auto numChannels = static_cast<size_t>(juce::jmax (1u, spec.numChannels));
    for (auto& band : filters_)
    {
        band.clear();
        band.reserve (numChannels);
        for (size_t c = 0; c < numChannels; ++c)
            band.push_back (std::make_unique<juce::dsp::IIR::Filter<float>>());
    }

    for (int b = 0; b < 3; ++b)
        updateCoefficients (b);
}

void MultiBandEQ::reset()
{
    for (auto& band : filters_)
        for (auto& f : band)
            if (f != nullptr)
                f->reset();
}

void MultiBandEQ::updateCoefficients (int bandIdx)
{
    const auto& bp = bandData_[static_cast<size_t>(bandIdx)];
    const float freq = juce::jlimit (20.0f, static_cast<float>(sampleRate_ * 0.49), bp.frequency);
    const float gain = juce::jlimit (-24.0f, 24.0f, bp.gainDb);
    const float q    = juce::jlimit (0.1f, 10.0f, bp.q);

    juce::dsp::IIR::Coefficients<float>::Ptr coeffs;
    switch (bandIdx)
    {
        case 0: coeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf  (sampleRate_, freq, q, juce::Decibels::decibelsToGain (gain)); break;
        case 1: coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sampleRate_, freq, q, juce::Decibels::decibelsToGain (gain)); break;
        default: coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sampleRate_, freq, q, juce::Decibels::decibelsToGain (gain)); break;
    }

    for (auto& f : filters_[static_cast<size_t>(bandIdx)])
        if (f != nullptr)
            f->coefficients = coeffs;
}

void MultiBandEQ::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    if (bypass_ || buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0)
        return;

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    for (int b = 0; b < 3; ++b)
    {
        auto& band = filters_[static_cast<size_t>(b)];
        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (ch >= static_cast<int>(band.size()))
                continue;

            auto& filter = band[static_cast<size_t>(ch)];
            if (filter == nullptr)
                continue;
            float* data = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
                data[i] = filter->processSample (data[i]);
        }
    }
}

void MultiBandEQ::setBandParameters (int bandIdx, float frequency, float gainDb, float q)
{
    if (bandIdx < 0 || bandIdx > 2)
        return;

    auto& bp = bandData_[static_cast<size_t>(bandIdx)];
    bp.frequency = juce::jlimit (20.0f, 20000.0f, frequency);
    bp.gainDb    = juce::jlimit (-24.0f, 24.0f, gainDb);
    bp.q         = juce::jlimit (0.1f, 10.0f, q);

    updateCoefficients (bandIdx);
}

MultiBandEQ::BandParams MultiBandEQ::getBandParameters (int bandIdx) const
{
    if (bandIdx < 0 || bandIdx > 2)
        return { 1000.0f, 0.0f, 1.0f };
    return bandData_[static_cast<size_t>(bandIdx)];
}

} // namespace namberry
