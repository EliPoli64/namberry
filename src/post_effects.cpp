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
    room_.prepare (spec);

    delayLine_.setMaximumDelayInSamples (static_cast<int> (maxDelaySamples_));
    delayLine_.prepare (spec);

    reverb_.prepare (spec);

    updateReverb();

    reset();
}

void PostEffects::reset()
{
    chorus_.reset();
    flanger_.reset();
    phaser_.reset();
    room_.reset();
    delayLine_.reset();
    reverb_.reset();
}

//==============================================================================
void PostEffects::setChorusMix (float value)      { chorus_.setMixPercent (value); }
void PostEffects::setChorusRate (float value)     { chorus_.setRate (juce::jlimit (0.1f, 5.0f, value)); }
void PostEffects::setChorusDepth (float value)    { chorus_.setDepthPercent (value); }
void PostEffects::setChorusFeedback (float value) { chorus_.setFeedbackPercent (value); }
void PostEffects::setChorusDelay (float value)    { chorus_.setBaseDelayMs (value); }

void PostEffects::setFlangerMix (float value)      { flanger_.setMixPercent (value); }
void PostEffects::setFlangerRate (float value)     { flanger_.setRate (juce::jlimit (0.05f, 10.0f, value)); }
void PostEffects::setFlangerDepth (float value)    { flanger_.setDepthPercent (value); }
void PostEffects::setFlangerManual (float value)   { flanger_.setBaseDelayMs (value); }
void PostEffects::setFlangerFeedback (float value) { flanger_.setFeedbackPercent (juce::jlimit (-95.0f, 95.0f, value)); }

void PostEffects::setPhaserMix (float value)      { phaser_.setMixPercent (value); }
void PostEffects::setPhaserRate (float value)     { phaser_.setRate (juce::jlimit (0.05f, 8.0f, value)); }
void PostEffects::setPhaserDepth (float value)    { phaser_.setDepthPercent (value); }
void PostEffects::setPhaserFeedback (float value) { phaser_.setFeedbackPercent (value); }
void PostEffects::setPhaserStages (int stages)    { phaser_.setStages (stages); }

void PostEffects::setRoomSize (float value)  { room_.setSize (clamp01 (value)); }
void PostEffects::setRoomBlend (float value) { room_.setBlend (clamp01 (value)); }

void PostEffects::setDelayMix (float value)      { delayMix_ = clamp01 (value); }
void PostEffects::setDelayTimeMs (float value)   { delayTimeMs_ = juce::jlimit (10.0f, 2000.0f, value); }
void PostEffects::setDelayFeedback (float value) { delayFeedback_ = clampM1P1 (value); }

void PostEffects::setReverbMix (float value)   { reverbMix_ = clamp01 (value); updateReverb(); }
void PostEffects::setReverbDecay (float value) { reverbDecay_ = clamp01 (value); updateReverb(); }
void PostEffects::setReverbWidth (float value) { reverbWidth_ = clamp01 (value); updateReverb(); }

//==============================================================================
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
void PostEffects::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    if (bypass_ || buffer.getNumSamples() <= 0 || buffer.getNumChannels() == 0)
        return;

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);

    // Modulation effects.
    if (! chorusBypass_)
        chorus_.processBlock (buffer);

    if (! flangerBypass_)
        flanger_.processBlock (buffer);

    if (! phaserBypass_)
        phaser_.processBlock (buffer);

    // Short delay-based room. The low fixed feedback adds density and a short
    // decay, while the fixed damping keeps it from turning into a bright,
    // long reverb.
    room_.processBlock (buffer, midi);

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
