#include "namberry/DigitalCapo.h"
#include "namberry/PitchShifter.h"

namespace namberry {

DigitalCapo::DigitalCapo()
    : shifter_ (std::make_unique<PitchShifter>())
{
}

DigitalCapo::~DigitalCapo() = default;

//==============================================================================
void DigitalCapo::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    maxBlockSize_ = juce::jmax (1, static_cast<int> (spec.maximumBlockSize));

    shifter_->prepare (spec);
    latencySamples_ = shifter_->getLatencySamples();

    dryDelay_.setMaximumDelayInSamples (latencySamples_ + maxBlockSize_ + 8);
    dryDelay_.prepare (spec);
    dryDelay_.reset();

    wetBuffer_.setSize (juce::jmax (1, static_cast<int> (spec.numChannels)),
                        maxBlockSize_);
    wetBuffer_.clear();

    mixSmoothed_.reset (spec.sampleRate, 0.020);
    mixSmoothed_.setCurrentAndTargetValue (0.0f);

    semitoneSmoothed_.reset (spec.sampleRate, 0.020);
    semitoneSmoothed_.setCurrentAndTargetValue (static_cast<float> (fretPosition_));

    active_ = false;
    activationSamplesRemaining_ = 0;
    prepared_ = true;
}

void DigitalCapo::reset()
{
    if (prepared_)
        shifter_->reset();

    dryDelay_.reset();

    mixSmoothed_.setCurrentAndTargetValue (0.0f);
    semitoneSmoothed_.setCurrentAndTargetValue (static_cast<float> (fretPosition_));

    active_ = false;
    activationSamplesRemaining_ = 0;
    wetBuffer_.clear();
}

//==============================================================================
void DigitalCapo::processBlock (juce::AudioBuffer<float>& buffer,
                                juce::MidiBuffer& /*midi*/)
{
    if (! prepared_ || buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0)
        return;

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    if (wetBuffer_.getNumChannels() != numChannels
        || wetBuffer_.getNumSamples() < numSamples)
    {
        wetBuffer_.setSize (numChannels, numSamples, false, false, true);
    }

    const bool wantActive = ! bypass_
                         && fretPosition_ != 0
                         && mix_ > 0.0f;

    const bool wasActive = active_;

    if (wantActive)
    {
        if (! wasActive)
        {
            // Start with a clean shifter state and full target pitch.
            shifter_->reset();
            semitoneSmoothed_.setCurrentAndTargetValue (static_cast<float> (fretPosition_));
            active_ = true;

            // The shifter's first useful output arrives after its latency.
            // Keep the dry signal until then so enabling the capo never dips.
            activationSamplesRemaining_ = latencySamples_;
        }
        else
        {
            semitoneSmoothed_.setTargetValue (static_cast<float> (fretPosition_));
        }

        if (activationSamplesRemaining_ > 0)
        {
            activationSamplesRemaining_ = juce::jmax (0, activationSamplesRemaining_ - numSamples);
            mixSmoothed_.setTargetValue (0.0f);
        }
        else
        {
            mixSmoothed_.setTargetValue (mix_);
        }

        // Block-rate semitone smoothing; this only updates scalar state.
        semitoneSmoothed_.skip (numSamples);
        shifter_->setTransposeSemitones (semitoneSmoothed_.getCurrentValue());
    }
    else
    {
        mixSmoothed_.setTargetValue (0.0f);

        if (! wasActive)
            activationSamplesRemaining_ = 0;
    }

    const bool runShifter = active_
                         && (wantActive || mixSmoothed_.getCurrentValue() > 0.0001f);

    if (runShifter)
        shifter_->process (buffer, wetBuffer_, numSamples);

    const int shifterChannels = shifter_->getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        const float mix = mixSmoothed_.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float input = buffer.getSample (ch, i);

            dryDelay_.pushSample (ch, input);
            const float dry = dryDelay_.popSample (ch, static_cast<float> (latencySamples_));

            const float wet = (runShifter && ch < shifterChannels)
                                ? wetBuffer_.getSample (ch, i)
                                : 0.0f;

            buffer.setSample (ch, i, dry + (wet - dry) * mix);
        }
    }

    if (! wantActive && mixSmoothed_.getCurrentValue() <= 0.0001f)
    {
        active_ = false;
        activationSamplesRemaining_ = 0;
    }
}

//==============================================================================
void DigitalCapo::setFretPosition (int fret)
{
    fretPosition_ = juce::jlimit (-12, 12, fret);
}

void DigitalCapo::setMix (float mix)
{
    mix_ = juce::jlimit (0.0f, 1.0f, mix);
}

void DigitalCapo::setParams (const Params& p)
{
    fretPosition_ = juce::jlimit (-12, 12, p.fretPosition);
    mix_ = juce::jlimit (0.0f, 1.0f, p.mix);
    bypass_ = p.bypass;
}

} // namespace namberry
