#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>

namespace namberry {

/// Two parallel stereo convolution reverb slots for the Post FX pedalboard.
///
/// Each slot loads a full stereo IR and has its own smoothed Mix control and a
/// tail Cutoff that trims long IRs with a short fade. The two wet results are
/// parallel sends crossfaded against the dry signal by the higher Mix, so a Mix
/// at 100 % leaves no dry signal. A local Level knob applies a smoothed boost
/// or cut to the wet sends only. There is no Blend knob. With no IR loaded, or
/// while bypassed, the stage is an exact passthrough.
class ReverbIrLoader
{
public:
    static constexpr int kNumSlots = 2;
    static constexpr float kDefaultCutoffSeconds = 3.0f;
    static constexpr float kMinLevelDb = -24.0f;
    static constexpr float kMaxLevelDb = 12.0f;
    static constexpr float kDefaultLevelDb = 0.0f;

    ReverbIrLoader();

    void prepare (const juce::dsp::ProcessSpec& spec);
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void reset();

    /// Load a reverb IR (.wav/.aiff) into one slot. A failed load leaves the
    /// previous IR in place so a bad file cannot silence the tail mid-playback.
    /// The current Cutoff is applied to the IR before it reaches Convolution.
    bool loadIr (int slot, const juce::File& irFile);

    void setMix (int slot, float mix);
    float getMix (int slot) const;

    /// Local wet-output boost/cut in dB. This does not touch the global master.
    void setLevelDb (float db);
    float getLevelDb() const;

    /// Stores a new tail length. Returns true when the value actually changed
    /// so the caller can schedule applyPendingCutoffChanges() off the audio
    /// thread. No file I/O happens here.
    bool setCutoff (int slot, float seconds);
    float getCutoff (int slot) const;

    /// Must be called on the message thread. Reloads any slot whose Cutoff has
    /// changed since its IR was last prepared.
    void applyPendingCutoffChanges();

    void setBypass (bool bypassed);
    bool isBypassed() const { return bypass_; }

    bool isLoaded (int slot) const;
    juce::String getLoadedFileName (int slot) const;
    juce::File getLoadedFile (int slot) const;

private:
    struct Slot
    {
        juce::dsp::Convolution convolution;
        juce::AudioBuffer<float> wet;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixGain;
        juce::File irFile;
        float mix = 0.25f;
        bool loaded = false;
    };

    bool reloadWithCutoff (int slot, const juce::File& file, float seconds);

    std::array<Slot, kNumSlots> slots_;
    juce::AudioBuffer<float> dry_;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> dryGain_;
    juce::AudioFormatManager formatManager_;

    std::array<std::atomic<float>, kNumSlots> cutoffSeconds_ {
        kDefaultCutoffSeconds, kDefaultCutoffSeconds
    };
    std::array<float, kNumSlots> appliedCutoff_ {
        kDefaultCutoffSeconds, kDefaultCutoffSeconds
    };
    std::atomic<float> levelGain_ { 1.0f };

    double sampleRate_ = 44100.0;
    int maxBlockSize_ = 512;
    int numChannels_ = 2;
    bool bypass_ = true;
};

} // namespace namberry
