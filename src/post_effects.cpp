#include "namberry/PostEffects.h"

#include <cmath>

namespace namberry {

namespace {
float clamp01 (float x) { return juce::jlimit (0.0f, 1.0f, x); }
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

    auto toneSpec = spec;
    toneSpec.numChannels = 2;
    delayToneFilter_.prepare (toneSpec);
    delayToneFilter_.setType (juce::dsp::FirstOrderTPTFilterType::lowpass);
    delayToneFilter_.setCutoffFrequency (juce::jmin (800.0f,
                                                     static_cast<float> (spec.sampleRate * 0.45)));

    delayToneSmoothed_.reset (spec.sampleRate, 0.02);
    delayFuzzSmoothed_.reset (spec.sampleRate, 0.02);

    reverb_.prepare (spec);

    updateReverb();

    reset();
}

void PostEffects::reset()
{
    chorus_.reset();
    flanger_.reset();
    phaser_.reset();
    delayLine_.reset();
    delayToneFilter_.reset();
    delayToneSmoothed_.setCurrentAndTargetValue (delayTone_);
    delayFuzzSmoothed_.setCurrentAndTargetValue (delayFuzz_);
    reverb_.reset();
}

//==============================================================================
void PostEffects::setChorusMix (float value)      { chorus_.setMixPercent (value); }
void PostEffects::setChorusRate (float value)     { chorus_.setRate (juce::jlimit (0.1f, 5.0f, value)); }
void PostEffects::setChorusDepth (float value)    { chorus_.setDepthPercent (value); }
void PostEffects::setChorusFeedback (float value) { chorus_.setFeedbackPercent (value); }
void PostEffects::setChorusDelay (float value)    { chorus_.setBaseDelayMs (value); }
void PostEffects::setChorusStereo (bool value)    { chorus_.setStereo (value); }

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

void PostEffects::setDelayMix (float value)      { delayMix_ = clamp01 (value); }
void PostEffects::setDelayTimeMs (float value)   { delayTimeMs_ = juce::jlimit (10.0f, 2000.0f, value); }

void PostEffects::setDelayFeedback (float value)
{
    // User range 0..10. Zero leaves one repeat; 10 is an almost endless tail
    // without allowing an unbounded feedback loop.
    const float amount = juce::jlimit (0.0f, 10.0f, value) * 0.1f;
    delayFeedback_ = juce::jlimit (0.0f, 0.999f, amount * 0.999f);
}

void PostEffects::setDelayPingPong (bool value)
{
    if (value == delayPingPong_)
        return;

    // Cross-fed left/right state is meaningless in the other mode.
    delayPingPong_ = value;
    delayLine_.reset();
}

void PostEffects::setDelayTone (float value)
{
    delayTone_ = juce::jlimit (-1.0f, 1.0f, value * 0.01f);
    delayToneSmoothed_.setTargetValue (delayTone_);
}

void PostEffects::setDelayFuzz (float value)
{
    delayFuzz_ = clamp01 (value * 0.01f);
    delayFuzzSmoothed_.setTargetValue (delayFuzz_);
}

void PostEffects::setReverbMix (float value)   { reverbMix_ = clamp01 (value); updateReverb(); }
void PostEffects::setReverbDecay (float value) { reverbDecay_ = clamp01 (value); updateReverb(); }
void PostEffects::setReverbWidth (float value) { reverbWidth_ = clamp01 (value); updateReverb(); }

//==============================================================================
void PostEffects::updateReverb()
{
    juce::Reverb::Parameters params;
    params.roomSize = juce::jlimit (0.0f, 1.0f, reverbDecay_);
    params.damping = 0.4f;

    // juce::Reverb scales dry by 2 and wet by 3 internally. Compensate here so
    // a 20 % mix does not make the dry guitar 1.6x louder.
    params.wetLevel = reverbMix_ / 3.0f;
    params.dryLevel = 0.5f * (1.0f - reverbMix_);
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
        chorus_.processBlock (buffer);

    if (! flangerBypass_)
        flanger_.processBlock (buffer);

    if (! phaserBypass_)
        phaser_.processBlock (buffer);

    // Feedback delay. Ping-pong folds the input to mono and bounces the echo
    // between left and right; mono mode keeps a single centred repeat. Tone
    // tilts the repeats between dark and bright, fuzz adds tape-like
    // saturation. Both are shaped on the delay-line input so every repeat
    // compounds the character.
    if (! delayBypass_)
    {
        const float delaySamples = juce::jlimit (1.0f, maxDelaySamples_ - 2.0f,
                                                 delayTimeMs_ * 0.001f * static_cast<float> (sampleRate_));
        const bool pingPong = delayPingPong_ && numChannels > 1;

        for (int i = 0; i < numSamples; ++i)
        {
            const float tone = delayToneSmoothed_.getNextValue();
            const float fuzz = delayFuzzSmoothed_.getNextValue();

            // Negative tone keeps the lows, positive keeps the highs; 0 is flat.
            const float lowGain = 1.0f - juce::jmax (0.0f, tone);
            const float highGain = 1.0f + juce::jmin (0.0f, tone);

            const float drive = 1.0f + fuzz * 5.0f;
            const float driveComp = 1.0f / drive;

            auto shape = [fuzz, drive, driveComp] (float x)
            {
                const float saturated = std::tanh (x * drive) * driveComp;
                return x + fuzz * (saturated - x);
            };

            auto toneShape = [this, lowGain, highGain] (int channel, float x)
            {
                const float low = delayToneFilter_.processSample (channel, x);
                return low * lowGain + (x - low) * highGain;
            };

            float mono = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                mono += buffer.getSample (ch, i);

            mono /= static_cast<float> (numChannels);

            if (pingPong)
            {
                const float leftTap = delayLine_.popSample (0, delaySamples, true);
                const float rightTap = delayLine_.popSample (1, delaySamples, true);

                // Right repeats the previous left echo; left receives the mono
                // input plus the right echo scaled by feedback. One feedback
                // gain is applied per round trip (two delay times).
                delayLine_.pushSample (0, toneShape (0, shape (mono + rightTap * delayFeedback_)));
                delayLine_.pushSample (1, toneShape (1, shape (leftTap)));

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float wet = (ch % 2 == 0) ? leftTap : rightTap;
                    const float dry = buffer.getSample (ch, i);
                    buffer.setSample (ch, i, dry * (1.0f - delayMix_) + wet * delayMix_);
                }
            }
            else
            {
                const float tap = delayLine_.popSample (0, delaySamples, true);
                delayLine_.pushSample (0, toneShape (0, shape (mono + tap * delayFeedback_)));

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float dry = buffer.getSample (ch, i);
                    buffer.setSample (ch, i, dry * (1.0f - delayMix_) + tap * delayMix_);
                }
            }
        }
    }

    // Algorithmic reverb.
    if (! reverbBypass_)
        reverb_.process (context);
}

} // namespace namberry
