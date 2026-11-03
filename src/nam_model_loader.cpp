#include "namberry/NamModelLoader.h"

#include <algorithm>
#include <cmath>

namespace namberry {

NamModelLoader::NamModelLoader()
{
    monoIn_.assign (maxBufferSize_, 0.0f);
    monoOut_.assign (maxBufferSize_, 0.0f);
}

NamModelLoader::~NamModelLoader() = default;

//==============================================================================
void NamModelLoader::resetState()
{
    silentSamples_ = 0;
    smartBypassed_ = receptiveFieldSize_ > 0;
}

bool NamModelLoader::loadModel (const juce::File& modelFile)
{
    if (! modelFile.existsAsFile())
    {
        juce::Logger::getCurrentLogger()->writeToLog (
            "NAMBerry: model file does not exist: " + modelFile.getFullPathName());
        return false;
    }

    loader_.SetExternalSampleRate (static_cast<int> (sampleRate_));
    loader_.SetDefaultMaxAudioBufferSize (static_cast<int> (maxBufferSize_));

    std::unique_ptr<NeuralAudio::NeuralModel> newModel;

    try
    {
        newModel.reset (loader_.CreateFromFile (
            std::filesystem::path (modelFile.getFullPathName().toStdString())));
    }
    catch (const std::exception& e)
    {
        juce::Logger::getCurrentLogger()->writeToLog (
            juce::String ("NAMBerry: exception loading model: ") + e.what());
        return false;
    }

    if (newModel == nullptr)
    {
        juce::Logger::getCurrentLogger()->writeToLog (
            "NAMBerry: failed to load model: " + modelFile.getFullPathName());
        return false;
    }

    newModel->SetMaxAudioBufferSize (static_cast<int> (maxBufferSize_));

    {
        const juce::SpinLock::ScopedLockType lock (modelLock_);

        model_ = std::move (newModel);
        receptiveFieldSize_ = model_->GetReceptiveFieldSize();
        resetState();

        modelFile_ = modelFile;
        modelSampleRate_ = static_cast<int> (sampleRate_);
        hasModelFlag_.store (true);
    }

    return true;
}

//==============================================================================
void NamModelLoader::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    maxBufferSize_ = static_cast<size_t> (juce::jmax (1u, spec.maximumBlockSize));

    loader_.SetExternalSampleRate (static_cast<int> (sampleRate_));
    loader_.SetDefaultMaxAudioBufferSize (static_cast<int> (maxBufferSize_));

    monoIn_.assign (maxBufferSize_, 0.0f);
    monoOut_.assign (maxBufferSize_, 0.0f);

    // The model is built for a specific sample rate (WaveNet dilations are
    // adjusted for oversampling). Rebuild it if the device rate changed.
    if (hasModelFlag_.load() && modelFile_.existsAsFile()
        && modelSampleRate_ != static_cast<int> (sampleRate_))
    {
        loadModel (modelFile_);
        return;
    }

    const juce::SpinLock::ScopedLockType lock (modelLock_);

    if (model_ != nullptr)
    {
        model_->SetMaxAudioBufferSize (static_cast<int> (maxBufferSize_));
        receptiveFieldSize_ = model_->GetReceptiveFieldSize();
    }

    resetState();
}

void NamModelLoader::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    if (numChannels == 0 || numSamples == 0)
        return;

    // No model: pass the dry signal through unchanged.
    if (! hasModelFlag_.load() || model_ == nullptr)
        return;

    // If a model is being swapped in on the message thread, pass the dry
    // signal for this block rather than blocking the audio thread.
    const juce::SpinLock::ScopedTryLockType lock (modelLock_);
    if (! lock.isLocked())
        return;

    const size_t n = static_cast<size_t> (numSamples);

    if (monoIn_.size() < n) monoIn_.resize (n, 0.0f);
    if (monoOut_.size() < n) monoOut_.resize (n, 0.0f);

    // --- Sum all input channels to mono (averaged to keep headroom) ---
    std::fill (monoIn_.begin(), monoIn_.begin() + static_cast<std::ptrdiff_t> (n), 0.0f);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* data = buffer.getReadPointer (ch);
        for (size_t i = 0; i < n; ++i)
            monoIn_[i] += data[i];
    }

    const float channelScale = 1.0f / static_cast<float> (numChannels);

    // --- Input gain ---
    const float inputGain = gain_ * channelScale;
    for (size_t i = 0; i < n; ++i)
        monoIn_[i] *= inputGain;

    // --- Warm-up with silence (WaveNet receptive field) ---
    if (receptiveFieldSize_ > 0 && smartBypassed_)
    {
        bool allSilent = true;
        for (size_t i = 0; i < n; ++i)
        {
            if (std::abs (monoIn_[i]) > 0.001f)
            {
                allSilent = false;
                break;
            }
        }

        if (allSilent)
        {
            silentSamples_ += n;
            if (silentSamples_ >= static_cast<size_t> (receptiveFieldSize_))
                smartBypassed_ = false;
        }
        else
        {
            silentSamples_ = 0;
            smartBypassed_ = false;
        }

        if (smartBypassed_)
        {
            buffer.clear();
            return;
        }
    }

    // --- Run the mono model, chunked to its max buffer size ---
    size_t offset = 0;
    size_t remaining = n;

    while (remaining > 0)
    {
        const size_t chunk = juce::jmin (remaining, maxBufferSize_);

        if (monoOut_.size() < offset + chunk)
            monoOut_.resize (offset + chunk, 0.0f);

        model_->Process (monoIn_.data() + offset,
                         monoOut_.data() + offset,
                         chunk);

        offset += chunk;
        remaining -= chunk;
    }

    // --- Output level, then send the same mono result to every channel ---
    const float outGain = outputLevel_;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* data = buffer.getWritePointer (ch);
        for (size_t i = 0; i < n; ++i)
            data[i] = monoOut_[i] * outGain;
    }
}

void NamModelLoader::reset()
{
    const juce::SpinLock::ScopedLockType lock (modelLock_);

    if (model_ != nullptr)
        model_->Prewarm();

    resetState();
}

//==============================================================================
juce::String NamModelLoader::getLoadedFileName() const
{
    return hasModelFlag_.load() ? modelFile_.getFileName() : juce::String();
}

void NamModelLoader::setGain(float gain)             { gain_ = juce::jlimit(0.0f, 5.0f, gain); }
void NamModelLoader::setPresence(float presence)     { presence_ = juce::jlimit(0.0f, 1.0f, presence); }
void NamModelLoader::setDepth(float depth)           { depth_ = juce::jlimit(0.0f, 1.0f, depth); }
void NamModelLoader::setOutputLevel(float level)     { outputLevel_ = juce::jlimit(0.0f, 5.0f, level); }

} // namespace namberry
