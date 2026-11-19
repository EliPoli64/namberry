#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <memory>
#include <vector>

namespace namberry {

/// Eight-band graphical EQ used after the NAM amp. Every band has its own
/// selectable curve shape, plus a separate high-pass and low-pass filter.
class PostAmpEQ
{
public:
    static constexpr int kNumBands = 8;

    /// "Low Pass" and "High Pass" are shelving curves so the band's gain knob
    /// always has a useful boost/cut meaning. "Band" is a peaking bell.
    enum class BandType
    {
        LowPass = 0,
        HighPass = 1,
        Band = 2
    };

    static BandType bandTypeFromInt (int value);
    static int bandTypeToInt (BandType type);

    struct BandParams
    {
        float frequency = 1000.0f;
        float gainDb = 0.0f;
        float q = 1.0f;
        BandType type = BandType::Band;

        bool operator== (const BandParams& other) const = default;
    };

    struct Settings
    {
        std::array<BandParams, kNumBands> bands {{
            { 63.0f,   0.0f, 0.707f, BandType::LowPass },
            { 125.0f,  0.0f, 1.0f,   BandType::Band },
            { 250.0f,  0.0f, 1.0f,   BandType::Band },
            { 500.0f,  0.0f, 1.0f,   BandType::Band },
            { 1000.0f, 0.0f, 1.0f,   BandType::Band },
            { 2000.0f, 0.0f, 1.0f,   BandType::Band },
            { 4000.0f, 0.0f, 1.0f,   BandType::Band },
            { 8000.0f, 0.0f, 0.707f, BandType::HighPass }
        }};
        float highPassFrequency = 20.0f;
        float highPassQ = 0.707f;
        float lowPassFrequency = 20000.0f;
        float lowPassQ = 0.707f;

        bool operator== (const Settings& other) const = default;
    };

    PostAmpEQ() = default;
    explicit PostAmpEQ (const Settings& settings);

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    void setBandParameters (int bandIdx, float frequency, float gainDb, float q,
                            BandType type);
    void setHighPassParameters (float frequencyHz, float q);
    void setLowPassParameters (float frequencyHz, float q);

    void setBypass (bool bypassed) { bypass_ = bypassed; }
    bool isBypassed() const { return bypass_; }

    BandParams getBandParameters (int bandIdx) const;
    Settings getSettings() const { return { bands_, highPassFrequency_, highPassQ_,
                                            lowPassFrequency_, lowPassQ_ }; }

    /// Fills outDb with the summed magnitude of the complete EQ. Safe to call
    /// from the message thread with a locally built Settings object.
    static void getResponseDb (const Settings& settings, double sampleRate,
                               const double* frequencies, float* outDb, int numFrequencies);
    static float getResponseDb (const Settings& settings, double sampleRate,
                                double frequency);

private:
    static juce::dsp::IIR::Coefficients<float>::Ptr makeBandCoefficients (
        double sampleRate, const BandParams& band);
    static juce::dsp::IIR::Coefficients<float>::Ptr makeHighPassCoefficients (
        float frequency, float q, double sampleRate);
    static juce::dsp::IIR::Coefficients<float>::Ptr makeLowPassCoefficients (
        float frequency, float q, double sampleRate);

    void updateBandCoefficients (int bandIdx);
    void updatePassCoefficients();

    double sampleRate_ = 44100.0;
    bool bypass_ = false;

    std::array<BandParams, kNumBands> bands_ {{
        { 63.0f,   0.0f, 0.707f, BandType::LowPass },
        { 125.0f,  0.0f, 1.0f,   BandType::Band },
        { 250.0f,  0.0f, 1.0f,   BandType::Band },
        { 500.0f,  0.0f, 1.0f,   BandType::Band },
        { 1000.0f, 0.0f, 1.0f,   BandType::Band },
        { 2000.0f, 0.0f, 1.0f,   BandType::Band },
        { 4000.0f, 0.0f, 1.0f,   BandType::Band },
        { 8000.0f, 0.0f, 0.707f, BandType::HighPass }
    }};

    float highPassFrequency_ = 20.0f;
    float highPassQ_ = 0.707f;
    float lowPassFrequency_ = 20000.0f;
    float lowPassQ_ = 0.707f;

    // [band][channel], plus one filter per channel for each pass filter.
    std::array<std::vector<std::unique_ptr<juce::dsp::IIR::Filter<float>>>, kNumBands> filters_;
    std::vector<std::unique_ptr<juce::dsp::IIR::Filter<float>>> highPassFilters_;
    std::vector<std::unique_ptr<juce::dsp::IIR::Filter<float>>> lowPassFilters_;
};

} // namespace namberry
