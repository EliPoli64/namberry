#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace namberry {

/// Post-amp effects: Chorus, Flanger, Phaser, Delay and Reverb.
///
/// Each effect owns its own state and is built on JUCE's DSP classes
/// (dsp::Chorus, dsp::Phaser, dsp::DelayLine, dsp::Reverb), so enabling one
/// effect never corrupts another.
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
    void updateChorus();
    void updateFlanger();
    void updatePhaser();
    void updateReverb();

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

    juce::dsp::Chorus<float> chorus_;
    juce::dsp::Chorus<float> flanger_;
    juce::dsp::Phaser<float> phaser_;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine_;
    juce::dsp::Reverb reverb_;

    float maxDelaySamples_ = 1.0f;
};

} // namespace namberry
