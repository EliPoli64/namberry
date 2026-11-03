#include "namberry/PostEffects.h"

#include <cmath>

namespace namberry {

namespace {
float clamp01 (float x) { return juce::jlimit (0.0f, 1.0f, x); }
float clampM1P1 (float x) { return juce::jlimit (-1.0f, 1.0f, x); }
}

float PostEffects::softClip (float x)
{
    const float a = juce::jlimit (-1.0f, 1.0f, x);
    return std::tanh (a * 1.5f);
}

float PostEffects::applyFeedbackDelay (float input, float& delayedSample, size_t readIndex,
                                      size_t writeIndex, std::vector<float>& buffer,
                                      float mix, float feedback, float maxDelaySamples,
                                      float timeMs, float sampleRate)
{
    const float delaySamples = juce::jlimit (2.0f, maxDelaySamples, timeMs * 0.001f * sampleRate);
    const size_t wrapRead = static_cast<size_t> (readIndex) % buffer.size();
    const size_t wrapWrite = static_cast<size_t> (writeIndex) % buffer.size();

    const float dry = input;
    const float delayed = buffer[wrapRead];
    const float wet = delayed * mix + dry * (1.0f - mix);
    buffer[wrapWrite] = dry + delayed * feedback;
    delayedSample = wet;

    const size_t nextRead = (wrapRead + 1) % buffer.size();
    const size_t safeRead = juce::jlimit (0, static_cast<int> (buffer.size() - 1), static_cast<int> (wrapRead + static_cast<size_t> (delaySamples)));
    if (safeRead < buffer.size())
    {
        return buffer[safeRead];
    }
    return delayed;
}

void PostEffects::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    maxDelaySamples_ = static_cast<size_t> (spec.maximumBlockSize * 4 + 44100);
    delayBufferL_.assign (maxDelaySamples_ + 1, 0.0f);
    delayBufferR_.assign (maxDelaySamples_ + 1, 0.0f);
    delayWriteIndex_ = 0;
    lfoPhase_ = 0.0f;
}

void PostEffects::reset()
{
    std::fill (delayBufferL_.begin(), delayBufferL_.end(), 0.0f);
    std::fill (delayBufferR_.begin(), delayBufferR_.end(), 0.0f);
    delayWriteIndex_ = 0;
    lfoPhase_ = 0.0f;
}

void PostEffects::setChorusMix (float value) { chorusMix_ = clamp01 (value); }
void PostEffects::setChorusRate (float value) { chorusRate_ = juce::jlimit (0.1f, 10.0f, value); }
void PostEffects::setChorusDepth (float value) { chorusDepth_ = clamp01 (value); }

void PostEffects::setFlangerMix (float value) { flangerMix_ = clamp01 (value); }
void PostEffects::setFlangerRate (float value) { flangerRate_ = juce::jlimit (0.1f, 10.0f, value); }
void PostEffects::setFlangerDepth (float value) { flangerDepth_ = clamp01 (value); }
void PostEffects::setFlangerFeedback (float value) { flangerFeedback_ = clampM1P1 (value); }

void PostEffects::setPhaserMix (float value) { phaserMix_ = clamp01 (value); }
void PostEffects::setPhaserRate (float value) { phaserRate_ = juce::jlimit (0.1f, 10.0f, value); }
void PostEffects::setPhaserDepth (float value) { phaserDepth_ = clamp01 (value); }

void PostEffects::setDelayMix (float value) { delayMix_ = clamp01 (value); }
void PostEffects::setDelayTimeMs (float value) { delayTimeMs_ = juce::jlimit (10.0f, 2000.0f, value); }
void PostEffects::setDelayFeedback (float value) { delayFeedback_ = clampM1P1 (value); }

void PostEffects::setReverbMix (float value) { reverbMix_ = clamp01 (value); }
void PostEffects::setReverbDecay (float value) { reverbDecay_ = clamp01 (value); }
void PostEffects::setReverbWidth (float value) { reverbWidth_ = clamp01 (value); }

void PostEffects::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    if (bypass_ || buffer.getNumSamples() <= 0 || buffer.getNumChannels() == 0)
        return;

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const float phase = lfoPhase_;
        const float chorusMod = std::sin (phase) * chorusDepth_ * 0.01f;
        const float flangerMod = std::sin (phase * 1.5f) * flangerDepth_ * 0.008f;
        const float phaserMod = std::sin (phase * 0.8f) * phaserDepth_ * 0.02f;

        lfoPhase_ += (2.0f * juce::MathConstants<float>::pi * chorusRate_ / static_cast<float> (sampleRate_));

        float dryL = buffer.getSample (0, sample);
        float dryR = numChannels > 1 ? buffer.getSample (1, sample) : dryL;

        float wetL = dryL;
        float wetR = dryR;

        if (! chorusBypass_)
        {
            const float chorusDelay = 0.006f + chorusMod;
            const size_t readL = (delayWriteIndex_ + static_cast<size_t> (chorusDelay * sampleRate_)) % delayBufferL_.size();
            const size_t readR = (delayWriteIndex_ + static_cast<size_t> ((chorusDelay + 0.0015f) * sampleRate_)) % delayBufferR_.size();
            wetL += delayBufferL_[readL] * chorusMix_;
            wetR += delayBufferR_[readR] * chorusMix_;
            delayBufferL_[delayWriteIndex_] = dryL;
            delayBufferR_[delayWriteIndex_] = dryR;
        }

        if (! flangerBypass_)
        {
            const float delayTime = 0.003f + flangerMod;
            const size_t readL = (delayWriteIndex_ + static_cast<size_t> (delayTime * sampleRate_)) % delayBufferL_.size();
            const size_t readR = (delayWriteIndex_ + static_cast<size_t> ((delayTime + 0.0006f) * sampleRate_)) % delayBufferR_.size();
            const float fedL = delayBufferL_[readL] * flangerFeedback_ + dryL;
            const float fedR = delayBufferR_[readR] * flangerFeedback_ + dryR;
            wetL = dryL * (1.0f - flangerMix_) + fedL * flangerMix_;
            wetR = dryR * (1.0f - flangerMix_) + fedR * flangerMix_;
            delayBufferL_[delayWriteIndex_] = fedL;
            delayBufferR_[delayWriteIndex_] = fedR;
        }

        if (! phaserBypass_)
        {
            const float shift = 0.5f + phaserDepth_ * 0.5f + phaserMod;
            const float phaserMix = phaserMix_ * std::sin (phase * 2.0f * juce::MathConstants<float>::pi * phaserRate_ * 0.5f);
            wetL = dryL * (1.0f - phaserMix) + softClip (dryL + shift * dryR) * phaserMix;
            wetR = dryR * (1.0f - phaserMix) + softClip (dryR + shift * dryL) * phaserMix;
        }

        if (! delayBypass_)
        {
            const size_t delaySamples = juce::jlimit (2, static_cast<int> (delayBufferL_.size() - 1),
                                                   static_cast<int> (delayTimeMs_ * 0.001f * sampleRate_));
            const size_t readL = (delayWriteIndex_ + delaySamples) % delayBufferL_.size();
            const size_t readR = (delayWriteIndex_ + delaySamples + 1) % delayBufferR_.size();
            const float echoL = delayBufferL_[readL];
            const float echoR = delayBufferR_[readR];
            wetL = dryL * (1.0f - delayMix_) + echoL * delayMix_;
            wetR = dryR * (1.0f - delayMix_) + echoR * delayMix_;
            delayBufferL_[delayWriteIndex_] = dryL + echoL * delayFeedback_;
            delayBufferR_[delayWriteIndex_] = dryR + echoR * delayFeedback_;
        }

        if (! reverbBypass_)
        {
            const float decay = 0.2f + reverbDecay_ * 0.8f;
            const float width = 0.25f + reverbWidth_ * 0.75f;
            const float r1 = delayBufferL_[delayWriteIndex_];
            const float r2 = delayBufferR_[delayWriteIndex_];
            wetL = dryL * (1.0f - reverbMix_) + (r1 * (1.0f - width) + r2 * width) * reverbMix_ * decay;
            wetR = dryR * (1.0f - reverbMix_) + (r2 * (1.0f - width) + r1 * width) * reverbMix_ * decay;
            delayBufferL_[delayWriteIndex_] = wetL * 0.6f;
            delayBufferR_[delayWriteIndex_] = wetR * 0.6f;
        }

        buffer.setSample (0, sample, wetL);
        if (numChannels > 1)
            buffer.setSample (1, sample, wetR);

        delayWriteIndex_ = (delayWriteIndex_ + 1) % delayBufferL_.size();
    }
}

} // namespace namberry
