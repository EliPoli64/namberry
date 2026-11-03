#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <NeuralAudio/NeuralModel.h>

#include <memory>

namespace namberry {

/// Wrapper around NeuralAudio::NeuralModel for loading and running .nam models.
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

    float getGain() const { return gain_; }
    float getPresence() const { return presence_; }
    float getDepth() const { return depth_; }
    float getOutputLevel() const { return outputLevel_; }
    bool hasModel() const { return model_ != nullptr; }

private:
    // Parameters
    float gain_ = 1.0f;
    float presence_ = 0.5f;
    float depth_ = 0.5f;
    float outputLevel_ = 1.0f;

    // NeuralAudio model + loader
    NeuralAudio::NeuralModelLoader loader_;
    std::unique_ptr<NeuralAudio::NeuralModel> model_;

    // Audio state
    double sampleRate_ = 44100.0;
    size_t maxBufferSize_ = 512;

    // Warm-up handling
    int receptiveFieldSize_ = 0;
    size_t silentSamples_ = 0;
    bool smartBypassed_ = true;

    // Scratch buffer for mono in/out (models may be mono)
    std::vector<float> scratch_;
};

} // namespace namberry
