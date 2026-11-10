#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>

namespace namberry {

/// Two-slot IR loader. A single blend control crossfades between the two
/// loaded cabinets: 0 = IR A, 1 = IR B.
class IrLoader
{
public:
    static constexpr int kNumSlots = 2;

    IrLoader();
    ~IrLoader() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void reset();

    /// Load an IR (.wav/.aiff) into a slot (0 = A, 1 = B).
    bool loadIr (int slot, const juce::File& irFile);

    /// Crossfade between the loaded IRs: 0 = slot A, 1 = slot B.
    void setBlend (float blend);
    float getBlend() const { return blend_; }

    void setBypass (bool bypassed) { bypass_ = bypassed; }
    bool isBypassed() const { return bypass_; }

    bool isSlotLoaded (int slot) const;
    juce::String getLoadedFileName (int slot) const;
    juce::File getLoadedFile (int slot) const;

private:
    std::array<juce::dsp::Convolution, kNumSlots> convolutions_;
    std::array<bool, kNumSlots> loaded_ {};
    std::array<juce::File, kNumSlots> irFiles_ {};

    juce::AudioBuffer<float> dry_;
    std::array<juce::AudioBuffer<float>, kNumSlots> wet_;

    float blend_ = 0.0f;
    double sampleRate_ = 44100.0;
    int maxBlockSize_ = 512;
    int numChannels_ = 2;
    bool bypass_ = false;
};

} // namespace namberry
