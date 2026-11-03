#include "namberry/NamModelLoader.h"

#include <cmath>
#include <algorithm>

namespace namberry {

NamModelLoader::NamModelLoader()
{
    scratch_.resize(maxBufferSize_, 0.0f);
}

NamModelLoader::~NamModelLoader() = default;

bool NamModelLoader::loadModel(const juce::File& modelFile)
{
    if (!modelFile.existsAsFile())
    {
        juce::Logger::getCurrentLogger()->writeToLog(
            "NAMBerry: model file does not exist: " + modelFile.getFullPathName());
        return false;
    }

    // Give the loader our current sample rate/buffer settings.
    loader_.SetExternalSampleRate(static_cast<int>(sampleRate_));
    loader_.SetDefaultMaxAudioBufferSize(static_cast<int>(maxBufferSize_));

    std::unique_ptr<NeuralAudio::NeuralModel> newModel;
    try
    {
        newModel.reset(loader_.CreateFromFile(
            std::filesystem::path(modelFile.getFullPathName().toStdString())));
    }
    catch (const std::exception& e)
    {
        juce::Logger::getCurrentLogger()->writeToLog(
            juce::String("NAMBerry: exception loading model: ") + e.what());
        return false;
    }

    if (newModel == nullptr)
    {
        juce::Logger::getCurrentLogger()->writeToLog(
            "NAMBerry: failed to load model: " + modelFile.getFullPathName());
        return false;
    }

    newModel->SetMaxAudioBufferSize(static_cast<int>(maxBufferSize_));
    receptiveFieldSize_ = newModel->GetReceptiveFieldSize();

    model_ = std::move(newModel);

    silentSamples_ = 0;
    smartBypassed_ = receptiveFieldSize_ > 0; // only need warm-up if model has a receptive field

    return true;
}

void NamModelLoader::prepare(const juce::dsp::ProcessSpec& spec){
    sampleRate_ = spec.sampleRate;
    maxBufferSize_ = spec.maximumBlockSize;

    loader_.SetExternalSampleRate(static_cast<int>(sampleRate_));
    loader_.SetDefaultMaxAudioBufferSize(static_cast<int>(maxBufferSize_));

    if (model_ != nullptr)
        model_->SetMaxAudioBufferSize(static_cast<int>(maxBufferSize_));

    scratch_.assign(maxBufferSize_, 0.0f);

    silentSamples_ = 0;
    smartBypassed_ = (model_ != nullptr) && (receptiveFieldSize_ > 0);
}

void NamModelLoader::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    if (buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0)
        return;

    // No model: pass through silently (dry) so the chain keeps working.
    if (model_ == nullptr)
        return;

    // --- Warm-up with silence (WaveNet receptive field) ---
    if (smartBypassed_)
    {
        bool allSilent = true;
        for (int ch = 0; ch < buffer.getNumChannels() && allSilent; ++ch)
        {
            const float* d = buffer.getReadPointer(ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                if (std::abs(d[i]) > 0.001f) { allSilent = false; break; }
        }

        if (allSilent)
        {
            silentSamples_ += static_cast<size_t>(buffer.getNumSamples());
            if (silentSamples_ >= static_cast<size_t>(receptiveFieldSize_))
                smartBypassed_ = false;
        }
        else
        {
            smartBypassed_ = false;
        }

        if (smartBypassed_)
        {
            buffer.clear();
            return;
        }
    }

    // --- Input gain ---
    buffer.applyGain(gain_);

    // --- Run model per channel (models are typically mono) ---
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* data = buffer.getWritePointer(ch);
        const int n = buffer.getNumSamples();

        if (scratch_.size() < static_cast<size_t>(n))
            scratch_.resize(static_cast<size_t>(n), 0.0f);

        // Models are mono; run each channel independently through the same model.
        model_->Process(data, scratch_.data(), static_cast<size_t>(n));
        std::copy(scratch_.begin(), scratch_.begin() + n, data);
    }

    // --- Output level ---
    buffer.applyGain(outputLevel_);
}

void NamModelLoader::reset()
{
    if (model_ != nullptr)
        model_->Prewarm();
    silentSamples_ = 0;
    smartBypassed_ = (model_ != nullptr) && (receptiveFieldSize_ > 0);
}

void NamModelLoader::setGain(float gain)             { gain_ = juce::jlimit(0.0f, 5.0f, gain); }
void NamModelLoader::setPresence(float presence)     { presence_ = juce::jlimit(0.0f, 1.0f, presence); }
void NamModelLoader::setDepth(float depth)           { depth_ = juce::jlimit(0.0f, 1.0f, depth); }
void NamModelLoader::setOutputLevel(float level)     { outputLevel_ = juce::jlimit(0.0f, 5.0f, level); }

} // namespace namberry
