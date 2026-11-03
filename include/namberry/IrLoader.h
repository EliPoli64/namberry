#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>

namespace namberry {

/// Four-slot IR loader with per-slot mix, pan and phase invert.
class IrLoader
{
public:
    IrLoader();
    ~IrLoader() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void reset();

    /// Load an IR (.wav/.aiff) into a slot (0-3).
    bool loadIr (int slot, const juce::File& irFile);

    void setSlotMix (int slot, float mix);
    void setSlotPan (int slot, float pan);
    void setSlotPhaseInvert (int slot, bool invert);
    void setBypass (bool bypassed) { bypass_ = bypassed; }
    bool isBypassed() const { return bypass_; }

    float getSlotMix (int slot) const;
    float getSlotPan (int slot) const;
    bool  getSlotPhaseInvert (int slot) const;
    bool  isSlotLoaded (int slot) const;
    juce::String getLoadedFileName (int slot) const;
    juce::File getLoadedFile (int slot) const;

private:
    static constexpr int kNumSlots = 4;

    std::array<juce::dsp::Convolution, kNumSlots> convolutions_;
    std::array<bool, kNumSlots> loaded_ {};
    std::array<float, kNumSlots> mix_ {};
    std::array<float, kNumSlots> pan_ {};
    std::array<bool, kNumSlots> phaseInvert_ {};
    std::array<juce::File, kNumSlots> irFiles_ {};

    juce::AudioBuffer<float> wet_[kNumSlots];
    double sampleRate_ = 44100.0;
    int maxBlockSize_ = 512;
    bool bypass_ = false;
};

} // namespace namberry
