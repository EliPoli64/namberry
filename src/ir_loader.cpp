#include "namberry/IrLoader.h"

#include <cmath>

namespace namberry {

IrLoader::IrLoader()
{
    mix_.fill (0.0f);
    pan_.fill (0.0f);
    phaseInvert_.fill (false);
    loaded_.fill (false);
}

void IrLoader::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    maxBlockSize_ = static_cast<int>(spec.maximumBlockSize);

    for (int s = 0; s < kNumSlots; ++s)
    {
        juce::dsp::ProcessSpec convSpec;
        convSpec.sampleRate = spec.sampleRate;
        convSpec.maximumBlockSize = spec.maximumBlockSize;
        convSpec.numChannels = spec.numChannels;
        convolutions_[static_cast<size_t>(s)].prepare (convSpec);

        wet_[s].setSize (static_cast<int>(spec.numChannels), maxBlockSize_);
        wet_[s].clear();
    }
}

void IrLoader::reset()
{
    for (auto& c : convolutions_)
        c.reset();
    for (auto& w : wet_)
        w.clear();
}

bool IrLoader::loadIr (int slot, const juce::File& irFile)
{
    if (slot < 0 || slot >= kNumSlots || !irFile.existsAsFile())
        return false;

    auto& conv = convolutions_[static_cast<size_t>(slot)];

    // Stereo::no keeps things predictable for the 4-slot summing mixer.
    conv.loadImpulseResponse (irFile,
                              juce::dsp::Convolution::Stereo::no,
                              juce::dsp::Convolution::Trim::yes,
                              0,
                              juce::dsp::Convolution::Normalise::yes);

    loaded_[static_cast<size_t>(slot)] = true;
    mix_[static_cast<size_t>(slot)] = 1.0f;
    return true;
}

void IrLoader::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();
    if (numChannels == 0 || numSamples == 0)
        return;

    // If no slot is loaded/mixed in, pass the dry signal through unchanged.
    // (Previously the buffer was always cleared, which muted the whole chain
    // until an IR was loaded.)
    bool anyActive = false;
    float maxMix = 0.0f;
    for (int s = 0; s < kNumSlots; ++s)
    {
        const float m = mix_[static_cast<size_t>(s)];
        if (loaded_[static_cast<size_t>(s)] && m > 0.0f)
        {
            anyActive = true;
            maxMix = juce::jmax (maxMix, m);
        }
    }

    if (! anyActive)
        return;

    // Keep a dry copy because we rebuild the output from the slot mix.
    juce::AudioBuffer<float> dry (numChannels, numSamples);
    dry.makeCopyOf (buffer);

    buffer.clear();

    for (int s = 0; s < kNumSlots; ++s)
    {
        const float mix = mix_[static_cast<size_t>(s)];
        if (!loaded_[static_cast<size_t>(s)] || mix <= 0.0f)
            continue;

        auto& wet = wet_[s];
        if (wet.getNumChannels() != numChannels || wet.getNumSamples() < numSamples)
        {
            wet.setSize (numChannels, numSamples);
        }
        wet.makeCopyOf (dry);

        juce::dsp::AudioBlock<float> block (wet);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        convolutions_[static_cast<size_t>(s)].process (ctx);

        // Simple stereo balance pan.
        const float pan = pan_[static_cast<size_t>(s)];
        const float leftGain  = juce::jlimit (0.0f, 1.0f, 0.5f - 0.5f * pan);
        const float rightGain = juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * pan);
        const float phase = phaseInvert_[static_cast<size_t>(s)] ? -1.0f : 1.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float chGain = (numChannels == 1) ? 1.0f
                                : ((ch == 0) ? leftGain : rightGain);
            buffer.addFrom (ch, 0, wet.getReadPointer (ch), numSamples,
                            phase * mix * chGain);
        }
    }

    // Blend the dry signal back in: full dry at mix 0, full wet at mix 1.
    const float dryGain = juce::jlimit (0.0f, 1.0f, 1.0f - maxMix);
    if (dryGain > 0.0f)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.addFrom (ch, 0, dry.getReadPointer (ch), numSamples, dryGain);
    }
}

void IrLoader::setSlotMix (int slot, float mix)
{
    if (slot >= 0 && slot < kNumSlots) mix_[static_cast<size_t>(slot)] = juce::jlimit (0.0f, 1.0f, mix);
}
void IrLoader::setSlotPan (int slot, float pan)
{
    if (slot >= 0 && slot < kNumSlots) pan_[static_cast<size_t>(slot)] = juce::jlimit (-1.0f, 1.0f, pan);
}
void IrLoader::setSlotPhaseInvert (int slot, bool invert)
{
    if (slot >= 0 && slot < kNumSlots) phaseInvert_[static_cast<size_t>(slot)] = invert;
}
float IrLoader::getSlotMix (int slot) const
{
    return (slot >= 0 && slot < kNumSlots) ? mix_[static_cast<size_t>(slot)] : 0.0f;
}
float IrLoader::getSlotPan (int slot) const
{
    return (slot >= 0 && slot < kNumSlots) ? pan_[static_cast<size_t>(slot)] : 0.0f;
}
bool IrLoader::getSlotPhaseInvert (int slot) const
{
    return (slot >= 0 && slot < kNumSlots) ? phaseInvert_[static_cast<size_t>(slot)] : false;
}
bool IrLoader::isSlotLoaded (int slot) const
{
    return (slot >= 0 && slot < kNumSlots) && loaded_[static_cast<size_t>(slot)];
}

} // namespace namberry
