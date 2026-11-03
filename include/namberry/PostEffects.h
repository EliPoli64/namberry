#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <vector>

namespace namberry {

class PostEffects
{
public:
    PostEffects() = default;
    ~PostEffects() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void reset();

    void setBypass (bool bypassed) { bypass_ = bypassed; }
    bool isBypassed() const { return bypass_; }

    void setChorusMix (float value);
    void setChorusRate (float value);
    void setChorusDepth (float value);
    void setChorusBypass (bool value) { chorusBypass_ = value; }

    void setFlangerMix (float value);
    void setFlangerRate (float value);
    void setFlangerDepth (float value);
    void setFlangerFeedback (float value);
    void setFlangerBypass (bool value) { flangerBypass_ = value; }

    void setPhaserMix (float value);
    void setPhaserRate (float value);
    void setPhaserDepth (float value);
    void setPhaserBypass (bool value) { phaserBypass_ = value; }

    void setDelayMix (float value);
    void setDelayTimeMs (float value);
    void setDelayFeedback (float value);
    void setDelayBypass (bool value) { delayBypass_ = value; }

    void setReverbMix (float value);
    void setReverbDecay (float value);
    void setReverbWidth (float value);
    void setReverbBypass (bool value) { reverbBypass_ = value; }

private:
    double sampleRate_ = 44100.0;
    bool bypass_ = false;

    bool chorusBypass_ = true;
    float chorusMix_ = 0.2f;
    float chorusRate_ = 1.5f;
    float chorusDepth_ = 0.35f;

    bool flangerBypass_ = true;
    float flangerMix_ = 0.2f;
    float flangerRate_ = 0.8f;
    float flangerDepth_ = 0.5f;
    float flangerFeedback_ = 0.35f;

    bool phaserBypass_ = true;
    float phaserMix_ = 0.2f;
    float phaserRate_ = 0.7f;
    float phaserDepth_ = 0.5f;

    bool delayBypass_ = true;
    float delayMix_ = 0.25f;
    float delayTimeMs_ = 200.0f;
    float delayFeedback_ = 0.4f;

    bool reverbBypass_ = true;
    float reverbMix_ = 0.2f;
    float reverbDecay_ = 0.5f;
    float reverbWidth_ = 0.7f;

    std::vector<float> delayBufferL_;
    std::vector<float> delayBufferR_;
    size_t delayWriteIndex_ = 0;
    size_t maxDelaySamples_ = 0;
    float lfoPhase_ = 0.0f;

    static float applyFeedbackDelay (float input, float& delayedSample, size_t readIndex,
                                    size_t writeIndex, std::vector<float>& buffer,
                                    float mix, float feedback, float maxDelaySamples,
                                    float timeMs, float sampleRate);
    static float softClip (float x);
};

} // namespace namberry
