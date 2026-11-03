#include "namberry/Compressor.h"

#include <cmath>
#include <vector>

namespace namberry {

void Compressor::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = static_cast<float>(spec.sampleRate);
    reset();
}

void Compressor::reset()
{
    env_ = 0.0f;
    sidechainLp_ = 0.0f;
}

void Compressor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();
    if (bypass_ || numChannels == 0 || numSamples == 0)
        return;

    const float threshLin = std::pow (10.0f, thresholdDb_ / 20.0f);
    const float makeupLin = std::pow (10.0f, makeupGainDb_ / 20.0f);
    const float attackCoeff  = std::exp (-1.0f / (juce::jmax (0.1f, attackMs_)  * 0.001f * sampleRate_));
    const float releaseCoeff = std::exp (-1.0f / (juce::jmax (0.1f, releaseMs_) * 0.001f * sampleRate_));

    // Tight mode: derive the detector signal from a low-passed copy (low end).
    const float scLpCoeff = std::exp (-2.0f * juce::MathConstants<float>::pi * 150.0f / sampleRate_);

    for (int i = 0; i < numSamples; ++i)
    {
        // Detector input: max abs across channels (or low-passed sum for Tight).
        float detector = 0.0f;
        if (tightMode_)
        {
            float mix = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                mix += buffer.getSample (ch, i);
            mix /= static_cast<float>(numChannels);
            sidechainLp_ = scLpCoeff * sidechainLp_ + (1.0f - scLpCoeff) * mix;
            detector = std::abs (sidechainLp_);
        }
        else
        {
            for (int ch = 0; ch < numChannels; ++ch)
                detector = juce::jmax (detector, std::abs (buffer.getSample (ch, i)));
        }

        // Attack/release envelope follower.
        const float coeff = (detector > env_) ? attackCoeff : releaseCoeff;
        env_ = coeff * env_ + (1.0f - coeff) * detector;

        // Gain computer (hard knee), dB domain for stability.
        float gainDb = 0.0f;
        if (env_ > threshLin && ratio_ > 1.0f)
        {
            const float envDb = 20.0f * std::log10 (juce::jmax (1.0e-6f, env_));
            gainDb = thresholdDb_ + (envDb - thresholdDb_) / ratio_ - envDb;
        }
        const float gainLin = std::pow (10.0f, gainDb / 20.0f) * makeupLin;

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer (ch)[i] *= gainLin;
    }
}

void Compressor::setAttackMs (float attackMs)       { attackMs_ = juce::jlimit (0.1f, 1000.0f, attackMs); }
void Compressor::setReleaseMs (float releaseMs)     { releaseMs_ = juce::jlimit (0.1f, 1000.0f, releaseMs); }
void Compressor::setThresholdDb (float thresholdDb) { thresholdDb_ = juce::jlimit (-60.0f, 0.0f, thresholdDb); }
void Compressor::setRatio (float ratio)             { ratio_ = juce::jlimit (1.0f, 20.0f, ratio); }
void Compressor::setMakeupGainDb (float gainDb)     { makeupGainDb_ = juce::jlimit (-60.0f, 60.0f, gainDb); }
void Compressor::setTightMode (bool enabled)        { tightMode_ = enabled; }

Compressor::Params Compressor::getParams() const
{
    return { attackMs_, releaseMs_, thresholdDb_, ratio_, makeupGainDb_, tightMode_, bypass_ };
}

void Compressor::setParams (const Params& p)
{
    setAttackMs (p.attackMs);
    setReleaseMs (p.releaseMs);
    setThresholdDb (p.thresholdDb);
    setRatio (p.ratio);
    setMakeupGainDb (p.makeupGainDb);
    setTightMode (p.tightMode);
    setBypass (p.bypass);
}

} // namespace namberry
