#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "namberry/ModulationEffects.h"
#include "namberry/Room.h"

namespace namberry {

/// Post-amp effects: Chorus, Flanger, Phaser, short Room ambience, Delay and
/// Reverb.
///
/// The three modulation pedals use a shared custom LFO/delay framework; Room
/// is a custom short-delay ambience, and Delay/Reverb use JUCE DSP.
class PostEffects
{
public:
    PostEffects() = default;
    ~PostEffects() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void reset();

    void setBypass (bool bypassed)
    {
        if (bypassed && ! bypass_)
        {
            chorus_.reset();
            flanger_.reset();
            phaser_.reset();
            room_.reset();
            delayLine_.reset();
        }

        bypass_ = bypassed;
    }

    bool isBypassed() const { return bypass_; }

    void setChorusMix (float value);
    void setChorusRate (float value);
    void setChorusDepth (float value);
    void setChorusFeedback (float value);
    void setChorusDelay (float value);
    void setChorusBypass (bool value)
    {
        if (value && ! chorusBypass_)
            chorus_.reset();

        chorusBypass_ = value;
    }

    void setFlangerMix (float value);
    void setFlangerRate (float value);
    void setFlangerDepth (float value);
    void setFlangerManual (float value);
    void setFlangerFeedback (float value);
    void setFlangerBypass (bool value)
    {
        if (value && ! flangerBypass_)
            flanger_.reset();

        flangerBypass_ = value;
    }

    void setPhaserMix (float value);
    void setPhaserRate (float value);
    void setPhaserDepth (float value);
    void setPhaserFeedback (float value);
    void setPhaserStages (int stages);
    void setPhaserBypass (bool value)
    {
        if (value && ! phaserBypass_)
            phaser_.reset();

        phaserBypass_ = value;
    }

    void setRoomSize (float value);
    void setRoomBlend (float value);
    void setRoomBypass (bool value) { room_.setBypass (value); }

    void setDelayMix (float value);
    void setDelayTimeMs (float value);
    void setDelayFeedback (float value);
    void setDelayBypass (bool value)
    {
        if (value != delayBypass_)
        {
            delayBypass_ = value;

            // Switching the delay off discards its existing feedback tail.
            if (value)
                delayLine_.reset();
        }
    }

    void setReverbMix (float value);
    void setReverbDecay (float value);
    void setReverbWidth (float value);
    void setReverbBypass (bool value) { reverbBypass_ = value; }

private:
    void updateReverb();

    double sampleRate_ = 44100.0;
    bool bypass_ = false;

    bool chorusBypass_ = true;
    bool flangerBypass_ = true;
    bool phaserBypass_ = true;

    bool delayBypass_ = true;
    float delayMix_ = 0.25f;
    float delayTimeMs_ = 200.0f;
    float delayFeedback_ = 0.4f;

    bool reverbBypass_ = true;
    float reverbMix_ = 0.2f;
    float reverbDecay_ = 0.5f;
    float reverbWidth_ = 0.7f;

    ModulatedDelay chorus_ { 40.0f, ModulationLfo::Waveform::sine, 0.5f, 5.0f };
    ModulatedDelay flanger_ { 10.0f, ModulationLfo::Waveform::triangle, 0.0f, 2.0f };
    Phaser phaser_;
    Room room_;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine_;
    juce::dsp::Reverb reverb_;

    float maxDelaySamples_ = 1.0f;
};

} // namespace namberry
