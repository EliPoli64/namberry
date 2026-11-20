#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <optional>
#include "namberry/NamModelLoader.h"
#include "namberry/IrLoader.h"
#include "namberry/MultiBandEQ.h"
#include "namberry/PostAmpEQ.h"
#include "namberry/DropTuner.h"
#include "namberry/DigitalCapo.h"
#include "namberry/Compressor.h"
#include "namberry/NoiseGate.h"
#include "namberry/Impersonator.h"
#include "namberry/AcousticSim.h"
#include "namberry/PostEffects.h"
#include "namberry/RoomSim.h"

namespace namberry {

class NAMBerryProcessor : public juce::AudioProcessor,
                          private juce::AsyncUpdater
{
public:
    NAMBerryProcessor();
    ~NAMBerryProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int maxSamplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Init"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getValueTreeState() { return apvts_; }

    // Model loading (called from editor drag-and-drop or file chooser)
    bool loadNamModel (const juce::File& file);
    bool loadNamPedalModel (const juce::File& file);
    bool loadIr (int slot, const juce::File& file);

    // Last-used folders, stored separately for amps, pedals and IR files.
    juce::File getNamDirectory() const;
    void setNamDirectory (const juce::File& directory);
    juce::File getPedalDirectory() const;
    void setPedalDirectory (const juce::File& directory);
    juce::File getIrDirectory() const;
    void setIrDirectory (const juce::File& directory);

    // Preset save/load: parameter state plus the current amp, pedal and IR paths.
    bool savePreset (const juce::File& file);
    bool loadPreset (const juce::File& file);
    juce::File getPresetDirectory() const;
    void setPresetDirectory (const juce::File& directory);

    // Restores the last selected amp/pedal model and IR files.
    void restoreSavedFiles();

    // Getters for editor display
    DropTuner* getDropTuner() { return dropTuner_.get(); }
    Compressor* getCompressor() { return compressor_.get(); }
    DigitalCapo* getDigitalCapo() { return digitalCapo_.get(); }
    NamModelLoader* getNamModel() { return namModel_.get(); }
    NamModelLoader* getNamPedal() { return namPedal_.get(); }
    IrLoader* getIrLoader() { return irLoader_.get(); }

private:
    void handleAsyncUpdate() override;

    //==============================================================================
    // DSP Modules
    std::unique_ptr<DropTuner> dropTuner_;
    std::unique_ptr<NoiseGate> noiseGate_;
    std::unique_ptr<Compressor> compressor_;
    std::unique_ptr<AcousticSim> acousticSim_;
    std::unique_ptr<DigitalCapo> digitalCapo_;
    std::unique_ptr<Impersonator> impersonator_;
    std::unique_ptr<NamModelLoader> namModel_;
    std::unique_ptr<NamModelLoader> namPedal_;
    std::unique_ptr<IrLoader> irLoader_;
    std::unique_ptr<MultiBandEQ> preAmpEQ_;
    std::unique_ptr<PostAmpEQ> postAmpEQ_;
    std::unique_ptr<PostEffects> postEffects_;
    std::unique_ptr<RoomSim> room_;

    //==============================================================================
    // Acoustic-sim auto-off bookkeeping. The edge detector runs on the audio
    // thread; the actual parameter flips happen in handleAsyncUpdate() on the
    // message thread.
    void syncAcousticSimChainState();

    std::atomic<bool> acousticSimEngaged_ { false };
    bool acousticSimEqSynced_ = false;
    std::array<std::optional<float>, 5> acousticSimSavedEq_;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts_;
    std::unique_ptr<juce::PropertiesFile> settings_;

    // Raw Post EQ parameter pointers cached once so processBlock never has to
    // build parameter-ID strings on the audio thread.
    struct CachedPostEqParams
    {
        std::atomic<float>* bypass = nullptr;
        std::array<std::atomic<float>*, PostAmpEQ::kNumBands> frequency {};
        std::array<std::atomic<float>*, PostAmpEQ::kNumBands> gain {};
        std::array<std::atomic<float>*, PostAmpEQ::kNumBands> q {};
        std::array<std::atomic<float>*, PostAmpEQ::kNumBands> type {};
        std::atomic<float>* highPassFrequency = nullptr;
        std::atomic<float>* highPassQ = nullptr;
        std::atomic<float>* lowPassFrequency = nullptr;
        std::atomic<float>* lowPassQ = nullptr;
    };

    CachedPostEqParams postEqParams_;

    // Post FX parameters are read every block too. Cache the raw pointers so
    // this section also avoids constructing parameter-ID strings on audio.
    struct CachedPostFxParams
    {
        std::atomic<float>* bypass = nullptr;
        std::atomic<float>* chorusBypass = nullptr;
        std::atomic<float>* chorusMix = nullptr;
        std::atomic<float>* chorusRate = nullptr;
        std::atomic<float>* chorusDepth = nullptr;
        std::atomic<float>* chorusFeedback = nullptr;
        std::atomic<float>* chorusDelay = nullptr;
        std::atomic<float>* flangerBypass = nullptr;
        std::atomic<float>* flangerMix = nullptr;
        std::atomic<float>* flangerRate = nullptr;
        std::atomic<float>* flangerDepth = nullptr;
        std::atomic<float>* flangerManual = nullptr;
        std::atomic<float>* flangerFeedback = nullptr;
        std::atomic<float>* phaserBypass = nullptr;
        std::atomic<float>* phaserMix = nullptr;
        std::atomic<float>* phaserRate = nullptr;
        std::atomic<float>* phaserDepth = nullptr;
        std::atomic<float>* phaserFeedback = nullptr;
        std::atomic<float>* phaserStages = nullptr;
        std::atomic<float>* roomAmount = nullptr;
        std::atomic<float>* delayBypass = nullptr;
        std::atomic<float>* delayMix = nullptr;
        std::atomic<float>* delayTimeMs = nullptr;
        std::atomic<float>* delayFeedback = nullptr;
        std::atomic<float>* delayPingPong = nullptr;
        std::atomic<float>* reverbBypass = nullptr;
        std::atomic<float>* reverbMix = nullptr;
        std::atomic<float>* reverbDecay = nullptr;
        std::atomic<float>* reverbWidth = nullptr;
    };

    CachedPostFxParams postFxParams_;

    std::atomic<int> pendingLatencySamples_ { 0 };
    std::atomic<int> appliedLatencySamples_ { 0 };

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NAMBerryProcessor)
};

} // namespace namberry
