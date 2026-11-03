#include "namberry/DigitalCapo.h"

namespace namberry {

void DigitalCapo::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    reset();
}

void DigitalCapo::reset() {}

void DigitalCapo::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    if (bypass_)
        return;

    // Pass-through for now. Fret position and mix are retained as parameters and
    // will drive a dedicated pitch shifter stage in a later iteration.
    juce::ignoreUnused (buffer, fretPosition_, mix_, sampleRate_);
}

void DigitalCapo::setFretPosition (int fret) { fretPosition_ = juce::jlimit (0, 12, fret); }
void DigitalCapo::setMix (float mix)         { mix_ = juce::jlimit (0.0f, 1.0f, mix); }

void DigitalCapo::setParams (const Params& p)
{
    fretPosition_ = juce::jlimit (0, 12, p.fretPosition);
    mix_ = juce::jlimit (0.0f, 1.0f, p.mix);
    bypass_ = p.bypass;
}

} // namespace namberry
