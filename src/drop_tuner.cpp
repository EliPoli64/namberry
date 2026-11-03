#include "namberry/DropTuner.h"

#include <cmath>
#include <algorithm>

namespace namberry {

void DropTuner::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate_ = spec.sampleRate;
    reset();
}

void DropTuner::reset()
{
    detectedFreqHz_ = 0.0f;
    detectedMidiNote_ = -1;
    detectedNoteName_ = "---";
}

void DropTuner::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    const int n = buffer.getNumSamples();
    if (n <= 0 || buffer.getNumChannels() == 0)
        return;

    // Analyse the channel with the most energy.
    int active = 0;
    float bestEnergy = -1.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* d = buffer.getReadPointer (ch);
        float e = 0.0f;
        for (int i = 0; i < n; ++i)
            e += std::abs (d[i]);
        if (e > bestEnergy) { bestEnergy = e; active = ch; }
    }

    std::vector<float> mono (static_cast<size_t>(n));
    const float* src = buffer.getReadPointer (active);
    std::copy (src, src + n, mono.begin());

    const float freq = detectFundamental (mono);
    detectedFreqHz_ = freq;
    detectedMidiNote_ = frequencyToMidi (freq);
    detectedNoteName_ = midiToName (detectedMidiNote_);

    // NOTE: this module is a detector/controller. Actual transposition of the
    // audio by the tuning offset is delegated to a dedicated pitch shifter.
    // The buffer is left intact so audio passes through unchanged.
    juce::ignoreUnused (bypass_, tuningOffsetSemitones_);
}

float DropTuner::detectFundamental (const std::vector<float>& x) const
{
    const int n = static_cast<int>(x.size());
    if (n < 64)
        return 0.0f;

    // Remove DC.
    double mean = 0.0;
    for (float v : x) mean += v;
    mean /= n;

    // Energy gate.
    double energy = 0.0;
    for (float v : x) energy += (v - mean) * (v - mean);
    if (energy / n < 1.0e-6)
        return 0.0f;

    const int minLag = juce::jmax (2, static_cast<int>(sampleRate_ / 1200.0)); // ~1200 Hz max
    const int maxLag = juce::jmin (n - 1, static_cast<int>(sampleRate_ / 50.0)); // ~50 Hz min

    // Normalised autocorrelation, pick the best peak.
    double bestCorr = 0.0;
    int bestLag = 0;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double corr = 0.0;
        double norm = 0.0;
        for (int i = 0; i + lag < n; ++i)
        {
            const double a = x[static_cast<size_t>(i)] - mean;
            const double b = x[static_cast<size_t>(i + lag)] - mean;
            corr += a * b;
            norm += a * a;
        }
        if (norm <= 0.0) continue;
        corr /= norm;
        if (corr > bestCorr) { bestCorr = corr; bestLag = lag; }
    }

    if (bestLag <= 0 || bestCorr < 0.2)
        return 0.0f;

    // Parabolic interpolation for sub-sample accuracy.
    return static_cast<float>(sampleRate_ / static_cast<double>(bestLag));
}

int DropTuner::frequencyToMidi (float hz)
{
    if (hz <= 0.0f) return -1;
    return static_cast<int>(std::lround (69.0 + 12.0 * std::log2 (hz / 440.0)));
}

juce::String DropTuner::midiToName (int midiNote)
{
    if (midiNote < 0) return "---";
    static const char* names[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    const int octave = (midiNote / 12) - 1;
    return juce::String (names[midiNote % 12]) + juce::String (octave);
}

void DropTuner::setTuningOffsetSemitones (float offset)
{
    tuningOffsetSemitones_ = juce::jlimit (-12.0f, 0.0f, offset);
}

DropTuner::Params DropTuner::getParams() const
{
    return { tuningOffsetSemitones_, bypass_ };
}

void DropTuner::setParams (const Params& p)
{
    tuningOffsetSemitones_ = juce::jlimit (-12.0f, 0.0f, p.tuningOffset);
    bypass_ = p.bypass;
}

} // namespace namberry
