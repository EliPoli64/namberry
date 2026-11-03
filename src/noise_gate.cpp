#include "namberry/NoiseGate.h"

#include <cmath>

namespace namberry {

void NoiseGate::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    reset();
}

void NoiseGate::reset()
{
    env_ = 0.0f;
    gain_ = 0.0f;
}

void NoiseGate::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    if (bypass_ || buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0)
        return;

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    const float threshLin = std::pow (10.0f, thresholdDb_ / 20.0f);
    const float attackCoeff  = std::exp (-1.0f / (juce::jmax (0.1f, attackMs_)  * 0.001f * static_cast<float>(sampleRate_)));
    const float releaseCoeff = std::exp (-1.0f / (juce::jmax (0.1f, releaseMs_) * 0.001f * static_cast<float>(sampleRate_)));

    for (int i = 0; i < numSamples; ++i)
    {
        // Linked detector: peak across channels.
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            peak = juce::jmax (peak, std::abs (buffer.getSample (ch, i)));

        // Envelope follower.
        const float coeff = (peak > env_) ? attackCoeff : releaseCoeff;
        env_ = coeff * env_ + (1.0f - coeff) * peak;

        // Target gain: open (1) above threshold, closed (0) below.
        const float target = (env_ >= threshLin) ? 1.0f : 0.0f;
        // Smooth the gate gain to avoid clicks.
        const float gCoeff = (target > gain_) ? attackCoeff : releaseCoeff;
        gain_ = gCoeff * gain_ + (1.0f - gCoeff) * target;

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer (ch)[i] *= gain_;
    }
}

void NoiseGate::setThresholdDb (float thresholdDb) { thresholdDb_ = juce::jlimit (-100.0f, 0.0f, thresholdDb); }
void NoiseGate::setAttackMs (float attackMs)       { attackMs_ = juce::jlimit (0.1f, 1000.0f, attackMs); }
void NoiseGate::setReleaseMs (float releaseMs)     { releaseMs_ = juce::jlimit (0.1f, 2000.0f, releaseMs); }

void NoiseGate::setParams (const Params& p)
{
    setThresholdDb (p.thresholdDb);
    setAttackMs (p.attackMs);
    setReleaseMs (p.releaseMs);
    bypass_ = p.bypass;
}

} // namespace namberry
