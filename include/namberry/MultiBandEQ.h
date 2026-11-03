#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <vector>
#include <memory>

namespace namberry {

/// Three-band EQ: Low Shelf, Mid Peak, High Shelf (JUCE dsp::IIR).
class MultiBandEQ
{
public:
    MultiBandEQ() = default;
    ~MultiBandEQ() = default;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void reset();

    /// bandIdx: 0 = low shelf, 1 = mid peak, 2 = high shelf.
    void setBandParameters (int bandIdx, float frequency, float gainDb, float q);

    struct BandParams { float frequency; float gainDb; float q; };
    BandParams getBandParameters (int bandIdx) const;

    void setBypass (bool bypassed) { bypass_ = bypassed; }
    bool isBypassed() const { return bypass_; }

private:
    void updateCoefficients (int bandIdx);

    double sampleRate_ = 44100.0;
    bool bypass_ = false;

    std::array<BandParams, 3> bandData_ {{
        { 200.0f,   0.0f, 1.0f },
        { 1000.0f,  0.0f, 1.0f },
        { 4000.0f,  0.0f, 1.0f }
    }};

    // [band][channel]
    std::array<std::vector<std::unique_ptr<juce::dsp::IIR::Filter<float>>>, 3> filters_;
};

} // namespace namberry
