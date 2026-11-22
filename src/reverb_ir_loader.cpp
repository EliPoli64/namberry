#include "namberry/ReverbIrLoader.h"

#include <cmath>

namespace namberry {
namespace {

/// Fade the IR tail to zero just before the requested length. Keeping the fade
/// short but smooth avoids both an audible chop and a long crossfade that would
/// blur the IR's early reflections.
void applyTailCutoff (juce::AudioBuffer<float>& ir,
                      double sampleRate,
                      float cutoffSeconds)
{
    const int length = ir.getNumSamples();

    if (length <= 0 || cutoffSeconds <= 0.0f)
    {
        ir.clear();
        return;
    }

    const int cutoff = juce::jmin (
        length, juce::roundToInt (static_cast<double> (cutoffSeconds) * sampleRate));

    if (cutoff >= length)
        return; // IR already fits: leave every sample untouched.

    for (int ch = 0; ch < ir.getNumChannels(); ++ch)
        for (int i = cutoff; i < length; ++i)
            ir.setSample (ch, i, 0.0f);

    const int fadeLength = juce::jmax (
        1, juce::jmin (juce::roundToInt (0.05 * sampleRate),
                       juce::jmax (1, cutoff / 4)));
    const int fadeStart = juce::jmax (0, cutoff - fadeLength);
    const int fadeSpan = juce::jmax (1, cutoff - fadeStart - 1);

    for (int ch = 0; ch < ir.getNumChannels(); ++ch)
        for (int i = fadeStart; i < cutoff; ++i)
        {
            const float alpha = 0.5f - 0.5f * std::cos (
                juce::MathConstants<float>::pi
                * static_cast<float> (i - fadeStart) / static_cast<float> (fadeSpan));
            ir.setSample (ch, i, ir.getSample (ch, i) * (1.0f - alpha));
        }
}

} // namespace

ReverbIrLoader::ReverbIrLoader()
{
    formatManager_.registerBasicFormats();
}

void ReverbIrLoader::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    maxBlockSize_ = static_cast<int> (spec.maximumBlockSize);
    numChannels_ = static_cast<int> (spec.numChannels);

    dry_.setSize (numChannels_, maxBlockSize_);
    dry_.clear();

    const int smoothingSteps = juce::jmax (1, maxBlockSize_ / 8);

    for (auto& slot : slots_)
    {
        slot.convolution.prepare (spec);
        slot.wet.setSize (numChannels_, maxBlockSize_);
        slot.wet.clear();

        // A short ramp keeps automation clean without smearing the IR attack.
        slot.mixGain.reset (smoothingSteps);
        slot.mixGain.setCurrentAndTargetValue (slot.mix * levelGain_.load());
    }

    dryGain_.reset (smoothingSteps);
    dryGain_.setCurrentAndTargetValue (
        1.0f - juce::jmax (slots_[0].mix, slots_[1].mix));
}

void ReverbIrLoader::reset()
{
    dry_.clear();

    for (auto& slot : slots_)
    {
        slot.convolution.reset();
        slot.wet.clear();
        slot.mixGain.setCurrentAndTargetValue (slot.mix * levelGain_.load());
    }

    dryGain_.setCurrentAndTargetValue (
        1.0f - juce::jmax (slots_[0].mix, slots_[1].mix));
}

bool ReverbIrLoader::reloadWithCutoff (int slotIndex,
                                       const juce::File& file,
                                       float seconds)
{
    if (slotIndex < 0 || slotIndex >= kNumSlots || ! file.existsAsFile())
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager_.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    const int numSamples = static_cast<int> (reader->lengthInSamples);
    const int readerChannels = static_cast<int> (reader->numChannels);

    if (numSamples <= 0 || readerChannels <= 0)
        return false;

    juce::AudioBuffer<float> ir (readerChannels, numSamples);
    ir.clear();

    if (! reader->read (&ir, 0, numSamples, 0, true, true))
        return false;

    applyTailCutoff (ir, reader->sampleRate, seconds);

    auto& slot = slots_[(size_t) slotIndex];

    // Stereo preserves real reverb IRs; mono files are expanded by Convolution.
    // Do not trim in JUCE: the fade above is the only tail edit. Normalise keeps
    // wildly different capture levels from jumping the output.
    slot.convolution.loadImpulseResponse (std::move (ir),
                                          reader->sampleRate,
                                          juce::dsp::Convolution::Stereo::yes,
                                          juce::dsp::Convolution::Trim::no,
                                          juce::dsp::Convolution::Normalise::yes);

    slot.irFile = file;
    slot.loaded = true;
    appliedCutoff_[(size_t) slotIndex] = seconds;
    return true;
}

bool ReverbIrLoader::loadIr (int slotIndex, const juce::File& irFile)
{
    if (slotIndex < 0 || slotIndex >= kNumSlots)
        return false;

    return reloadWithCutoff (slotIndex, irFile,
                             cutoffSeconds_[(size_t) slotIndex].load());
}

void ReverbIrLoader::setMix (int slotIndex, float mix)
{
    if (slotIndex < 0 || slotIndex >= kNumSlots)
        return;

    auto& slot = slots_[(size_t) slotIndex];
    slot.mix = juce::jlimit (0.0f, 1.0f, mix);
    slot.mixGain.setTargetValue (slot.mix * levelGain_.load());
}

float ReverbIrLoader::getMix (int slotIndex) const
{
    if (slotIndex < 0 || slotIndex >= kNumSlots)
        return 0.0f;

    return slots_[(size_t) slotIndex].mix;
}

void ReverbIrLoader::setLevelDb (float db)
{
    const auto clamped = juce::jlimit (kMinLevelDb, kMaxLevelDb, db);
    const auto gain = static_cast<float> (
        std::pow (10.0, static_cast<double> (clamped) / 20.0));

    levelGain_.store (gain);

    for (auto& slot : slots_)
        slot.mixGain.setTargetValue (slot.mix * gain);
}

float ReverbIrLoader::getLevelDb() const
{
    return static_cast<float> (
        20.0 * std::log10 (static_cast<double> (levelGain_.load())));
}

bool ReverbIrLoader::setCutoff (int slotIndex, float seconds)
{
    if (slotIndex < 0 || slotIndex >= kNumSlots)
        return false;

    const auto clamped = juce::jlimit (0.01f, 10.0f, seconds);
    const auto previous = cutoffSeconds_[(size_t) slotIndex].exchange (clamped);
    return std::abs (clamped - previous) > 1.0e-4f;
}

float ReverbIrLoader::getCutoff (int slotIndex) const
{
    if (slotIndex < 0 || slotIndex >= kNumSlots)
        return kDefaultCutoffSeconds;

    return cutoffSeconds_[(size_t) slotIndex].load();
}

void ReverbIrLoader::applyPendingCutoffChanges()
{
    for (int slot = 0; slot < kNumSlots; ++slot)
    {
        if (! slots_[(size_t) slot].loaded)
            continue;

        const auto desired = cutoffSeconds_[(size_t) slot].load();

        if (std::abs (desired - appliedCutoff_[(size_t) slot]) < 1.0e-4f)
            continue;

        // A failed reload leaves appliedCutoff_ alone so the next update retries.
        reloadWithCutoff (slot, slots_[(size_t) slot].irFile, desired);
    }
}

void ReverbIrLoader::setBypass (bool bypassed)
{
    if (bypassed && ! bypass_)
        reset();

    bypass_ = bypassed;
}

void ReverbIrLoader::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    if (bypass_)
        return;

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    if (numChannels <= 0 || numSamples <= 0
        || numChannels != dry_.getNumChannels() || dry_.getNumSamples() <= 0)
        return;

    bool anyLoaded = false;
    for (const auto& slot : slots_)
        anyLoaded = anyLoaded || slot.loaded;

    if (! anyLoaded)
        return;

    // A Mix at 100 % means 100 % reverb: remove the dry path entirely. With two
    // parallel slots, the higher Mix decides how much dry is left.
    dryGain_.setTargetValue (1.0f - juce::jmax (slots_[0].mix, slots_[1].mix));

    // Scratch is allocated in prepare() and never resized here. A host block
    // larger than the prepared maximum is handled in chunks instead.
    const int chunkSize = dry_.getNumSamples();

    for (int offset = 0; offset < numSamples; offset += chunkSize)
    {
        const int count = juce::jmin (chunkSize, numSamples - offset);

        for (int ch = 0; ch < numChannels; ++ch)
            dry_.copyFrom (ch, 0, buffer, ch, offset, count);

        // Crossfade against the loudest Mix, then add both parallel wet sends.
        for (int i = 0; i < count; ++i)
        {
            const float dryGain = dryGain_.getNextValue();

            for (int ch = 0; ch < numChannels; ++ch)
                buffer.setSample (ch, offset + i,
                                  dry_.getSample (ch, i) * dryGain);
        }

        for (auto& slot : slots_)
        {
            if (! slot.loaded)
                continue;

            for (int ch = 0; ch < numChannels; ++ch)
                slot.wet.copyFrom (ch, 0, dry_, ch, 0, count);

            juce::dsp::AudioBlock<float> block (slot.wet.getArrayOfWritePointers(),
                                                static_cast<size_t> (numChannels),
                                                static_cast<size_t> (count));
            juce::dsp::ProcessContextReplacing<float> context (block);
            slot.convolution.process (context);

            for (int i = 0; i < count; ++i)
            {
                const float wetGain = slot.mixGain.getNextValue();

                for (int ch = 0; ch < numChannels; ++ch)
                    buffer.addSample (ch, offset + i,
                                      slot.wet.getSample (ch, i) * wetGain);
            }
        }
    }
}

bool ReverbIrLoader::isLoaded (int slotIndex) const
{
    return slotIndex >= 0 && slotIndex < kNumSlots
        && slots_[(size_t) slotIndex].loaded;
}

juce::String ReverbIrLoader::getLoadedFileName (int slotIndex) const
{
    return isLoaded (slotIndex)
        ? slots_[(size_t) slotIndex].irFile.getFileName()
        : juce::String();
}

juce::File ReverbIrLoader::getLoadedFile (int slotIndex) const
{
    return isLoaded (slotIndex) ? slots_[(size_t) slotIndex].irFile : juce::File();
}

} // namespace namberry
