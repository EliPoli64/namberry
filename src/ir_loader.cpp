#include "namberry/IrLoader.h"

#include <cmath>

namespace namberry {

IrLoader::IrLoader()
{
    loaded_.fill (false);
}

void IrLoader::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    maxBlockSize_ = static_cast<int>(spec.maximumBlockSize);
    numChannels_ = static_cast<int>(spec.numChannels);

    for (int s = 0; s < kNumSlots; ++s)
    {
        juce::dsp::ProcessSpec convSpec;
        convSpec.sampleRate = spec.sampleRate;
        convSpec.maximumBlockSize = spec.maximumBlockSize;
        convSpec.numChannels = spec.numChannels;
        convolutions_[static_cast<size_t>(s)].prepare (convSpec);

        wet_[static_cast<size_t>(s)].setSize (numChannels_, maxBlockSize_);
        wet_[static_cast<size_t>(s)].clear();
    }

    dry_.setSize (numChannels_, maxBlockSize_);
    dry_.clear();
}

void IrLoader::reset()
{
    for (auto& c : convolutions_)
        c.reset();

    dry_.clear();

    for (auto& w : wet_)
        w.clear();
}

bool IrLoader::loadIr (int slot, const juce::File& irFile)
{
    if (slot < 0 || slot >= kNumSlots || !irFile.existsAsFile())
        return false;

    auto& conv = convolutions_[static_cast<size_t>(slot)];

    // Stereo::no keeps the two-slot crossfade predictable.
    conv.loadImpulseResponse (irFile,
                              juce::dsp::Convolution::Stereo::no,
                              juce::dsp::Convolution::Trim::yes,
                              0,
                              juce::dsp::Convolution::Normalise::yes);

    loaded_[static_cast<size_t>(slot)] = true;
    irFiles_[static_cast<size_t>(slot)] = irFile;
    return true;
}

void IrLoader::setBlend (float blend)
{
    blend_ = juce::jlimit (0.0f, 1.0f, blend);
}

void IrLoader::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    if (bypass_)
        return;

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();
    if (numChannels == 0 || numSamples == 0)
        return;

    const bool loadedA = loaded_[0];
    const bool loadedB = loaded_[1];

    // With no IR loaded the dry signal passes through unchanged.
    if (! loadedA && ! loadedB)
        return;

    // Make sure the scratch buffers fit this block. Hosts may hand us more
    // samples than the prepared maximum after a block-size change.
    if (numSamples > dry_.getNumSamples() || numChannels != dry_.getNumChannels())
    {
        const int size = juce::jmax (numSamples, maxBlockSize_);
        dry_.setSize (numChannels, size);

        for (auto& w : wet_)
            w.setSize (numChannels, size);
    }

    // Crossfade gains: both IRs loaded means (1 - blend) + blend = 1, so the
    // cabinet level never changes with the blend position. A single loaded IR
    // always plays at unity regardless of where the blend sits.
    float gains[kNumSlots] {};
    if (loadedA && loadedB)
    {
        gains[0] = 1.0f - blend_;
        gains[1] = blend_;
    }
    else
    {
        gains[0] = loadedA ? 1.0f : 0.0f;
        gains[1] = loadedB ? 1.0f : 0.0f;
    }

    // Keep a dry copy because the output is rebuilt from the IR bus.
    for (int ch = 0; ch < numChannels; ++ch)
        dry_.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    buffer.clear();

    for (int s = 0; s < kNumSlots; ++s)
    {
        const float gain = gains[s];
        if (! loaded_[static_cast<size_t>(s)] || gain <= 0.0f)
            continue;

        auto& wet = wet_[static_cast<size_t>(s)];

        for (int ch = 0; ch < numChannels; ++ch)
            wet.copyFrom (ch, 0, dry_, ch, 0, numSamples);

        // Build the block from exactly numSamples: the scratch buffers are
        // sized for the prepared maximum, and feeding the tail to the
        // convolution would advance its internal state.
        juce::dsp::AudioBlock<float> block (wet.getArrayOfWritePointers(),
                                            static_cast<size_t>(numChannels),
                                            static_cast<size_t>(numSamples));
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        convolutions_[static_cast<size_t>(s)].process (ctx);

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.addFrom (ch, 0, wet.getReadPointer (ch), numSamples, gain);
    }
}

bool IrLoader::isSlotLoaded (int slot) const
{
    return (slot >= 0 && slot < kNumSlots) && loaded_[static_cast<size_t>(slot)];
}

juce::String IrLoader::getLoadedFileName (int slot) const
{
    return (slot >= 0 && slot < kNumSlots)
        ? irFiles_[static_cast<size_t>(slot)].getFileName()
        : juce::String();
}

juce::File IrLoader::getLoadedFile (int slot) const
{
    return (slot >= 0 && slot < kNumSlots)
        ? irFiles_[static_cast<size_t>(slot)]
        : juce::File();
}

} // namespace namberry
