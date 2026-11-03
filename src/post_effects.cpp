#include "namberry/PostEffects.h"

#include <cmath>

namespace namberry {

namespace {
float clamp01 (float x) { return juce::jlimit (0.0f, 1.0f, x); }
float clampM1P1 (float x) { return juce::jlimit (-1.0f, 1.0f, x); }
}

//==============================================================================
void PostEffects::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    maxDelaySamples_ = static_cast<float> (juce::jmax (4.0, spec.sampleRate * 2.05));

    chorus_.prepare (spec);
    flanger_.prepare (spec);
    phaser_.prepare (spec);

    delayLine_.setMaximumDelayInSamples (static_cast<int> (maxDelaySamples_));
    delayLine_.prepare (spec);

    reverb_.prepare (spec);

    updateChorus();
    updateFlanger();
    updatePhaser();
    updateReverb();

    reset();
}

void PostEffects::reset()
{
    chorus_.reset();
    flanger_.reset();
    phaser_.reset();
    delayLine_.reset();
    reverb_.reset();
}

//==============================================================================
void PostEffects::setChorusMix (float value)   { chorusMix_ = clamp01 (value); updateChorus(); }
void PostEffects::setChorusRate (float value)  { chorusRate_ = juce::jlimit (0.05f, 20.0f, value); updateChorus(); }
void PostEffects::setChorusDepth (float value) { chorusDepth_ = clamp01 (value); updateChorus(); }

void PostEffects::setFlangerMix (float value)      { flangerMix_ = clamp01 (value); updateFlanger(); }
void PostEffects::setFlangerRate (float value)     { flangerRate_ = juce::jlimit (0.05f, 20.0f, value); updateFlanger(); }
void PostEffects::setFlangerDepth (float value)    { flangerDepth_ = clamp01 (value); updateFlanger(); }
void PostEffects::setFlangerFeedback (float value) { flangerFeedback_ = clampM1P1 (value); updateFlanger(); }

void PostEffects::setPhaserMix (float value)   { phaserMix_ = clamp01 (value); updatePhaser(); }
void PostEffects::setPhaserRate (float value)  { phaserRate_ = juce::jlimit (0.05f, 20.0f, value); updatePhaser(); }
void PostEffects::setPhaserDepth (float value) { phaserDepth_ = clamp01 (value); updatePhaser(); }

void PostEffects::setDelayMix (float value)      { delayMix_ = clamp01 (value); }
void PostEffects::setDelayTimeMs (float value)   { delayTimeMs_ = juce::jlimit (10.0f, 2000.0f, value); }
void PostEffects::setDelayFeedback (float value) { delayFeedback_ = clampM1P1 (value); }

void PostEffects::setReverbMix (float value)   { reverbMix_ = clamp01 (value); updateReverb(); }
void PostEffects::setReverbDecay (float value) { reverbDecay_ = clamp01 (value); updateReverb(); }
void PostEffects::setReverbWidth (float value) { reverbWidth_ = clamp01 (value); updateReverb(); }

//==============================================================================
void PostEffects::updateChorus()
{
    chorus_.setRate (chorusRate_);
    chorus_.setDepth (chorusDepth_);
    chorus_.setCentreDelay (7.0f);
    chorus_.setFeedback (0.0f);
    chorus_.setMix (chorusMix_);
}

void PostEffects::updateFlanger()
{
    // A short modulated delay with feedback is the classic flanger recipe.
    flanger_.setRate (flangerRate_);
    flanger_.setDepth (flangerDepth_);
    flanger_.setCentreDelay (2.5f);
    flanger_.setFeedback (flangerFeedback_);
    flanger_.setMix (flangerMix_);
}

void PostEffects::updatePhaser()
{
    phaser_.setRate (phaserRate_);
    phaser_.setDepth (phaserDepth_);
    phaser_.setCentreFrequency (600.0f);
    phaser_.setFeedback (0.2f);
    phaser_.setMix (phaserMix_);
}

void PostEffects::updateReverb()
{
    juce::Reverb::Parameters params;
    params.roomSize = juce::jlimit (0.0f, 1.0f, reverbDecay_);
    params.damping = 0.4f;
    params.wetLevel = reverbMix_;
    params.dryLevel = 1.0f - reverbMix_;
    params.width = juce::jlimit (0.0f, 1.0f, reverbWidth_);
    params.freezeMode = 0.0f;
    reverb_.setParameters (params);
}

//==============================================================================
void PostEffects::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    if (bypass_ || buffer.getNumSamples() <= 0 || buffer.getNumChannels() == 0)
        return;

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);

    // Modulation effects.
    if (! chorusBypass_)
        chorus_.process (context);

    if (! flangerBypass_)
        flanger_.process (context);

    if (! phaserBypass_)
        phaser_.process (context);

    // Feedback delay (stereo, independent per channel).
    if (! delayBypass_)
    {
        const float delaySamples = juce::jlimit (1.0f, maxDelaySamples_ - 2.0f,
                                                 delayTimeMs_ * 0.001f * static_cast<float> (sampleRate_));

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float dry = buffer.getSample (ch, i);
                const float delayed = delayLine_.popSample (ch, delaySamples, true);
                const float mixed = dry * (1.0f - delayMix_) + delayed * delayMix_;

                delayLine_.pushSample (ch, dry + delayed * delayFeedback_);
                buffer.setSample (ch, i, mixed);
            }
        }
    }

    // Algorithmic reverb.
    if (! reverbBypass_)
        reverb_.process (context);
}

} // namespace namberry
