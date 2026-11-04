#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_dsp/juce_dsp.h>
#include "namberry/NamModelLoader.h"
#include "namberry/IrLoader.h"
#include "namberry/MultiBandEQ.h"
#include "namberry/DropTuner.h"
#include "namberry/DigitalCapo.h"
#include "namberry/Compressor.h"
#include "namberry/NoiseGate.h"
#include "namberry/PostEffects.h"

namespace namberry {

class NAMBerryProcessor : public juce::AudioProcessor
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

    // Last-used folders, stored separately for NAM models and IR files.
    juce::File getNamDirectory() const;
    void setNamDirectory (const juce::File& directory);
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
    //==============================================================================
    // DSP Modules
    std::unique_ptr<DropTuner> dropTuner_;
    std::unique_ptr<NoiseGate> noiseGate_;
    std::unique_ptr<Compressor> compressor_;
    std::unique_ptr<DigitalCapo> digitalCapo_;
    std::unique_ptr<NamModelLoader> namModel_;
    std::unique_ptr<NamModelLoader> namPedal_;
    std::unique_ptr<IrLoader> irLoader_;
    std::unique_ptr<MultiBandEQ> preAmpEQ_;
    std::unique_ptr<MultiBandEQ> postAmpEQ_;
    std::unique_ptr<PostEffects> postEffects_;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts_;
    std::unique_ptr<juce::PropertiesFile> settings_;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NAMBerryProcessor)
};

} // namespace namberry
