#include "namberry/RoomSim.h"

#include <algorithm>
#include <cmath>

namespace namberry {

//==============================================================================
void RoomSim::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = juce::jmax (1.0, spec.sampleRate);

    const float delayTimesMs[2] = { kDelayLeftMs, kDelayRightMs };

    for (size_t channel = 0; channel < lines_.size(); ++channel)
    {
        auto& line = lines_[channel];
        line.length = juce::jmax (1, static_cast<int> (
            std::round (sampleRate_ * delayTimesMs[channel] * 0.001)));
        line.samples.assign (static_cast<size_t> (line.length), 0.0f);
        line.writeIndex = 0;
    }

    for (auto& filter : highPass_)
        filter.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (
            sampleRate_, kWetHighPassHz, kFilterQ);

    for (auto& filter : lowPass_)
        filter.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (
            sampleRate_, kWetLowPassHz, kFilterQ);

    targetFeedback_ = kMinFeedback
                    + (kMaxFeedback - kMinFeedback) * juce::jlimit (0.0f, 1.0f, targetAmount_);
    targetWetGain_ = kMaxWetGain * std::sin (
        0.5f * juce::jlimit (0.0f, 1.0f, targetAmount_)
        * static_cast<float> (juce::MathConstants<double>::pi));

    const float rampSamples = static_cast<float> (juce::jmax (
        1.0, sampleRate_ * kSmoothingMs * 0.001));
    feedbackStep_ = juce::jmax (1.0e-8f, (kMaxFeedback - kMinFeedback) / rampSamples);
    wetGainStep_ = juce::jmax (1.0e-8f, kMaxWetGain / rampSamples);

    reset();
}

void RoomSim::clearState()
{
    for (auto& line : lines_)
    {
        std::fill (line.samples.begin(), line.samples.end(), 0.0f);
        line.writeIndex = 0;
    }

    for (auto& filter : highPass_)
        filter.reset();

    for (auto& filter : lowPass_)
        filter.reset();
}

void RoomSim::reset()
{
    smoothedFeedback_ = targetFeedback_;
    smoothedWetGain_ = targetWetGain_;
    clearState();
}

void RoomSim::setAmount (float amount)
{
    const bool wasSilent = targetAmount_ <= 0.0f;

    targetAmount_ = juce::jlimit (0.0f, 1.0f, amount);
    targetFeedback_ = kMinFeedback + (kMaxFeedback - kMinFeedback) * targetAmount_;
    targetWetGain_ = kMaxWetGain * std::sin (
        0.5f * targetAmount_ * static_cast<float> (juce::MathConstants<double>::pi));

    // A stale tail must not reappear when the room is turned back on.
    if (wasSilent && targetAmount_ > 0.0f)
        reset();
}

//==============================================================================
void RoomSim::processBlock (juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels == 0)
        return;

    // Let the repeats ring out while the wet gain ramps down, then stop
    // updating the state entirely. setAmount() clears it on the next enable.
    if (targetAmount_ <= 0.0f && smoothedWetGain_ <= 1.0e-6f)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        float mono = 0.0f;
        for (int channel = 0; channel < numChannels; ++channel)
            mono += buffer.getSample (channel, i);

        mono /= static_cast<float> (numChannels);

        float wet[2] = { 0.0f, 0.0f };

        for (size_t channel = 0; channel < lines_.size(); ++channel)
        {
            auto& line = lines_[channel];
            int readIndex = line.writeIndex - line.length;

            while (readIndex < 0)
                readIndex += line.length;

            const float delayed = line.samples[static_cast<size_t> (readIndex)];
            const float filtered = lowPass_[channel].processSample (
                highPass_[channel].processSample (delayed));

            // The delayed copy is the dominant sound; feedback only adds a
            // couple of quiet, quickly decaying room repeats.
            line.samples[static_cast<size_t> (line.writeIndex)] =
                mono + smoothedFeedback_ * filtered;

            if (++line.writeIndex >= line.length)
                line.writeIndex = 0;

            wet[channel] = filtered * smoothedWetGain_;
        }

        if (numChannels == 1)
        {
            buffer.addSample (0, i, 0.5f * (wet[0] + wet[1]));
        }
        else
        {
            buffer.addSample (0, i, wet[0]);
            buffer.addSample (1, i, wet[1]);

            for (int channel = 2; channel < numChannels; ++channel)
                buffer.addSample (channel, i, 0.5f * (wet[0] + wet[1]));
        }

        smoothedFeedback_ += juce::jlimit (
            -feedbackStep_, feedbackStep_, targetFeedback_ - smoothedFeedback_);
        smoothedWetGain_ += juce::jlimit (
            -wetGainStep_, wetGainStep_, targetWetGain_ - smoothedWetGain_);
    }
}

} // namespace namberry
