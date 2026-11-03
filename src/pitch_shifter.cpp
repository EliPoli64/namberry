#include "namberry/PitchShifter.h"

namespace namberry {

void PitchShifter::prepare (const juce::dsp::ProcessSpec& spec)
{
    numChannels_ = juce::jmax (1, static_cast<int> (spec.numChannels));
    maxBlockSize_ = juce::jmax (1, static_cast<int> (spec.maximumBlockSize));

    const auto sampleRate = static_cast<float> (spec.sampleRate);

    // 50 ms block / 6.25 ms hop: a compromise between latency and pitch
    // accuracy (the full 120 ms default is more accurate but too latent).
    const int blockSamples = juce::jmax (256, static_cast<int> (sampleRate * 0.050f));
    const int intervalSamples = juce::jmax (64, static_cast<int> (sampleRate * 0.00625f));

    stretch_.configure (numChannels_, blockSamples, intervalSamples, false);

    latencySamples_ = stretch_.inputLatency() + stretch_.outputLatency();

    inputPointers_.assign (static_cast<size_t> (numChannels_), nullptr);
    outputPointers_.assign (static_cast<size_t> (numChannels_), nullptr);

    prepared_ = true;
    reset();
}

void PitchShifter::reset()
{
    stretch_.reset();
}

void PitchShifter::setTransposeSemitones (float semitones)
{
    // setTransposeSemitones only updates scalar state in Signalsmith Stretch.
    stretch_.setTransposeSemitones (semitones);
}

void PitchShifter::process (const juce::AudioBuffer<float>& input,
                            juce::AudioBuffer<float>& output,
                            int numSamples)
{
    if (! prepared_ || numSamples <= 0)
        return;

    const int channels = juce::jmin (numChannels_,
                                     input.getNumChannels(),
                                     output.getNumChannels());

    if (channels <= 0)
        return;

    const int samples = juce::jmin (numSamples,
                                    input.getNumSamples(),
                                    output.getNumSamples());

    for (int offset = 0; offset < samples; offset += maxBlockSize_)
    {
        const int chunk = juce::jmin (maxBlockSize_, samples - offset);

        for (int ch = 0; ch < channels; ++ch)
        {
            // Signalsmith Stretch does not modify its input, so the cast is safe.
            inputPointers_[(size_t) ch] =
                const_cast<float*> (input.getReadPointer (ch)) + offset;
            outputPointers_[(size_t) ch] = output.getWritePointer (ch) + offset;
        }

        stretch_.process (inputPointers_.data(), chunk,
                          outputPointers_.data(), chunk);
    }
}

} // namespace namberry
