#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <NeuralAudio/NeuralModel.h>

#include <atomic>
#include <memory>
#include <vector>

namespace namberry {

/// Wrapper around NeuralAudio::NeuralModel for loading and running .nam models.
///
/// NAM models are mono. All input channels are summed to a single mono stream,
/// run through one model, and the result is copied to every output channel.
/// (Feeding L and R through the same stateful model separately distorts badly.)
class NamModelLoader
{
public:
    NamModelLoader();
    ~NamModelLoader();

    // ============ Public API ============

    /// Load a .nam model file. Returns false on failure.
    bool loadModel(const juce::File& modelFile);

    void prepare(const juce::dsp::ProcessSpec& spec);
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages);
    void reset();

    // Controls
    void setGain(float gain);
    void setPresence(float presence);
    void setDepth(float depth);
    void setOutputLevel(float level);
    void setBypass(bool bypassed) { bypass_ = bypassed; }
    bool isBypassed() const { return bypass_; }

    float getGain() const { return gain_; }
    float getPresence() const { return presence_; }
    float getDepth() const { return depth_; }
    float getOutputLevel() const { return outputLevel_; }
    bool hasModel() const { return hasModelFlag_.load(); }

    juce::String getLoadedFileName() const;

private:
    void resetState();

    // Parameters
    float gain_ = 1.0f;
    float presence_ = 0.5f;
    float depth_ = 0.5f;
    float outputLevel_ = 1.0f;

    // NeuralAudio model + loader
    NeuralAudio::NeuralModelLoader loader_;
    std::unique_ptr<NeuralAudio::NeuralModel> model_;
    mutable juce::SpinLock modelLock_;

    juce::File modelFile_;
    std::atomic<bool> hasModelFlag_ { false };
    int modelSampleRate_ = 0;

    // Audio state
    double sampleRate_ = 44100.0;
    size_t maxBufferSize_ = 512;
    int receptiveFieldSize_ = 0;
    size_t silentSamples_ = 0;
    bool smartBypassed_ = false;
    bool bypass_ = false;

    // Mono working buffers (input and model output).
    std::vector<float> monoIn_;
    std::vector<float> monoOut_;
};

} // namespace namberry
