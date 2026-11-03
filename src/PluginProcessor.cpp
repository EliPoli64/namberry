#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace namberry {

//==============================================================================
// Parameter Layout
//==============================================================================
static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // ---- Drop Tuner ----
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "tuningOffset", "Tuning Offset",
        juce::NormalisableRange<float>(-12.0f, 0.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "tuningBypass", "Tuner Bypass", false));

    // ---- Noise Gate ----
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "gateThreshold", "Gate Threshold",
        juce::NormalisableRange<float>(-100.0f, 0.0f, 0.1f), -60.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "gateAttack", "Gate Attack",
        juce::NormalisableRange<float>(0.1f, 1000.0f, 0.1f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "gateRelease", "Gate Release",
        juce::NormalisableRange<float>(0.1f, 2000.0f, 0.1f), 100.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "gateBypass", "Gate Bypass", false));

    // ---- Digital Capo ----
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        "capoFret", "Capo Fret", 0, 12, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "capoMix", "Capo Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));

    // ---- Compressor ----
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "compAttack", "Comp Attack",
        juce::NormalisableRange<float>(0.1f, 1000.0f, 0.1f), 10.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "compRelease", "Comp Release",
        juce::NormalisableRange<float>(0.1f, 1000.0f, 0.1f), 100.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "compThreshold", "Comp Threshold",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -20.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "compRatio", "Comp Ratio",
        juce::NormalisableRange<float>(1.0f, 20.0f, 0.1f), 4.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "compMakeup", "Comp Makeup",
        juce::NormalisableRange<float>(-60.0f, 60.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "compTight", "Comp Tight", false));

    // ---- NAM Pedal / Preamp (separate .nam model) ----
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "pedalGain", "Pedal Gain",
        juce::NormalisableRange<float>(0.0f, 5.0f, 0.01f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "pedalOutput", "Pedal Output",
        juce::NormalisableRange<float>(0.0f, 5.0f, 0.01f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "pedalBypass", "Pedal Bypass", true));

    // ---- Pre-Amp EQ (3 bands) ----
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "preEqFreq0", "Pre EQ Low Freq",
        juce::NormalisableRange<float>(20.0f, 200.0f, 1.0f), 200.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "preEqGain0", "Pre EQ Low Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "preEqQ0", "Pre EQ Low Q",
        juce::NormalisableRange<float>(0.1f, 10.0f, 0.01f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "preEqFreq1", "Pre EQ Mid Freq",
        juce::NormalisableRange<float>(200.0f, 4000.0f, 1.0f), 1000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "preEqGain1", "Pre EQ Mid Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "preEqQ1", "Pre EQ Mid Q",
        juce::NormalisableRange<float>(0.1f, 10.0f, 0.01f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "preEqFreq2", "Pre EQ High Freq",
        juce::NormalisableRange<float>(4000.0f, 20000.0f, 1.0f), 4000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "preEqGain2", "Pre EQ High Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "preEqQ2", "Pre EQ High Q",
        juce::NormalisableRange<float>(0.1f, 10.0f, 0.01f), 1.0f));

    // ---- Post-Amp EQ (3 bands) ----
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "postEqFreq0", "Post EQ Low Freq",
        juce::NormalisableRange<float>(20.0f, 200.0f, 1.0f), 200.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "postEqGain0", "Post EQ Low Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "postEqQ0", "Post EQ Low Q",
        juce::NormalisableRange<float>(0.1f, 10.0f, 0.01f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "postEqFreq1", "Post EQ Mid Freq",
        juce::NormalisableRange<float>(200.0f, 4000.0f, 1.0f), 1000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "postEqGain1", "Post EQ Mid Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "postEqQ1", "Post EQ Mid Q",
        juce::NormalisableRange<float>(0.1f, 10.0f, 0.01f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "postEqFreq2", "Post EQ High Freq",
        juce::NormalisableRange<float>(4000.0f, 20000.0f, 1.0f), 4000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "postEqGain2", "Post EQ High Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "postEqQ2", "Post EQ High Q",
        juce::NormalisableRange<float>(0.1f, 10.0f, 0.01f), 1.0f));

    // ---- NAM Amp controls ----
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "ampGain", "Amp Gain",
        juce::NormalisableRange<float>(0.0f, 5.0f, 0.01f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "ampPresence", "Amp Presence",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "ampDepth", "Amp Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "ampOutput", "Amp Output",
        juce::NormalisableRange<float>(0.0f, 5.0f, 0.01f), 1.0f));

    // ---- Master output ----
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "masterOutput", "Master Output",
        juce::NormalisableRange<float>(-60.0f, 6.0f, 0.1f), 0.0f));

    // ---- UI preference: reduced motion ----
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "reducedMotion", "Reduced Motion", false));

    // ---- IR Loader (4 slots) ----
    for (int slot = 0; slot < 4; ++slot)
    {
        const auto idx = juce::String(slot);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "irMix" + idx, "IR " + idx + " Mix",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "irPan" + idx, "IR " + idx + " Pan",
            juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            "irPhase" + idx, "IR " + idx + " Phase", false));
    }

    return { params.begin(), params.end() };
}

//==============================================================================
NAMBerryProcessor::NAMBerryProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_ (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    // Instantiate all DSP modules
    dropTuner_ = std::make_unique<DropTuner>();
    noiseGate_ = std::make_unique<NoiseGate>();
    compressor_ = std::make_unique<Compressor>();
    digitalCapo_ = std::make_unique<DigitalCapo>();
    namModel_ = std::make_unique<NamModelLoader>();
    namPedal_ = std::make_unique<NamModelLoader>();
    irLoader_ = std::make_unique<IrLoader>();
    preAmpEQ_ = std::make_unique<MultiBandEQ>();
    postAmpEQ_ = std::make_unique<MultiBandEQ>();
}

NAMBerryProcessor::~NAMBerryProcessor() = default;

//==============================================================================
void NAMBerryProcessor::prepareToPlay (double sampleRate, int maxSamplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(maxSamplesPerBlock);
    spec.numChannels = static_cast<juce::uint32>(getTotalNumOutputChannels());

    dropTuner_->prepare(spec);
    noiseGate_->prepare(spec);
    compressor_->prepare(spec);
    digitalCapo_->prepare(spec);
    namModel_->prepare(spec);
    namPedal_->prepare(spec);
    irLoader_->prepare(spec);
    preAmpEQ_->prepare(spec);
    postAmpEQ_->prepare(spec);
}

void NAMBerryProcessor::releaseResources()
{
    // Release any resources here
}

void NAMBerryProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Clear unused output channels
    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    //==============================================================================
    // Signal chain order:
    // Input → DropTuner → NoiseGate → Compressor → Pre-Amp EQ → DigitalCapo →
    // NAM Amp → NAM Pedal → IR Loader → Post-Amp EQ → Master Output
    //==============================================================================

    // 1. Drop Tuner
    {
        auto* param = apvts_.getRawParameterValue("tuningBypass");
        bool bypass = param->load() > 0.5f;
        dropTuner_->setBypass(bypass);
        auto* offsetParam = apvts_.getRawParameterValue("tuningOffset");
        dropTuner_->setTuningOffsetSemitones(offsetParam->load());
        dropTuner_->processBlock(buffer, midiMessages);
    }

    // 1b. Noise Gate
    {
        noiseGate_->setThresholdDb(apvts_.getRawParameterValue("gateThreshold")->load());
        noiseGate_->setAttackMs(apvts_.getRawParameterValue("gateAttack")->load());
        noiseGate_->setReleaseMs(apvts_.getRawParameterValue("gateRelease")->load());
        noiseGate_->setBypass(apvts_.getRawParameterValue("gateBypass")->load() > 0.5f);
        noiseGate_->processBlock(buffer, midiMessages);
    }

    // 2. Compressor
    {
        compressor_->setAttackMs(apvts_.getRawParameterValue("compAttack")->load());
        compressor_->setReleaseMs(apvts_.getRawParameterValue("compRelease")->load());
        compressor_->setThresholdDb(apvts_.getRawParameterValue("compThreshold")->load());
        compressor_->setRatio(apvts_.getRawParameterValue("compRatio")->load());
        compressor_->setMakeupGainDb(apvts_.getRawParameterValue("compMakeup")->load());
        compressor_->setTightMode(apvts_.getRawParameterValue("compTight")->load() > 0.5f);
        compressor_->processBlock(buffer, midiMessages);
    }

    // 3. Pre-Amp EQ
    {
        preAmpEQ_->setBandParameters(0,
            apvts_.getRawParameterValue("preEqFreq0")->load(),
            apvts_.getRawParameterValue("preEqGain0")->load(),
            apvts_.getRawParameterValue("preEqQ0")->load());
        preAmpEQ_->setBandParameters(1,
            apvts_.getRawParameterValue("preEqFreq1")->load(),
            apvts_.getRawParameterValue("preEqGain1")->load(),
            apvts_.getRawParameterValue("preEqQ1")->load());
        preAmpEQ_->setBandParameters(2,
            apvts_.getRawParameterValue("preEqFreq2")->load(),
            apvts_.getRawParameterValue("preEqGain2")->load(),
            apvts_.getRawParameterValue("preEqQ2")->load());
        preAmpEQ_->processBlock(buffer, midiMessages);
    }

    // 4. Digital Capo
    {
        digitalCapo_->setFretPosition(static_cast<int>(apvts_.getRawParameterValue("capoFret")->load()));
        digitalCapo_->setMix(apvts_.getRawParameterValue("capoMix")->load());
        digitalCapo_->processBlock(buffer, midiMessages);
    }

    // 5. NAM Amp Model
    {
        namModel_->setGain(apvts_.getRawParameterValue("ampGain")->load());
        namModel_->setPresence(apvts_.getRawParameterValue("ampPresence")->load());
        namModel_->setDepth(apvts_.getRawParameterValue("ampDepth")->load());
        namModel_->setOutputLevel(apvts_.getRawParameterValue("ampOutput")->load());
        namModel_->processBlock(buffer, midiMessages);
    }

    // 5b. NAM Pedal / Preamp model (optional, bypassable)
    {
        const bool pedalBypassed = apvts_.getRawParameterValue("pedalBypass")->load() > 0.5f;
        if (!pedalBypassed && namPedal_->hasModel())
        {
            namPedal_->setGain(apvts_.getRawParameterValue("pedalGain")->load());
            namPedal_->setOutputLevel(apvts_.getRawParameterValue("pedalOutput")->load());
            namPedal_->processBlock(buffer, midiMessages);
        }
    }

    // 6. IR Loader (4 slots)
    {
        for (int slot = 0; slot < 4; ++slot)
        {
            const auto idx = juce::String(slot);
            irLoader_->setSlotMix(slot, apvts_.getRawParameterValue("irMix" + idx)->load());
            irLoader_->setSlotPan(slot, apvts_.getRawParameterValue("irPan" + idx)->load());
            irLoader_->setSlotPhaseInvert(slot, apvts_.getRawParameterValue("irPhase" + idx)->load() > 0.5f);
        }
        irLoader_->processBlock(buffer, midiMessages);
    }

    // 7. Post-Amp EQ
    {
        postAmpEQ_->setBandParameters(0,
            apvts_.getRawParameterValue("postEqFreq0")->load(),
            apvts_.getRawParameterValue("postEqGain0")->load(),
            apvts_.getRawParameterValue("postEqQ0")->load());
        postAmpEQ_->setBandParameters(1,
            apvts_.getRawParameterValue("postEqFreq1")->load(),
            apvts_.getRawParameterValue("postEqGain1")->load(),
            apvts_.getRawParameterValue("postEqQ1")->load());
        postAmpEQ_->setBandParameters(2,
            apvts_.getRawParameterValue("postEqFreq2")->load(),
            apvts_.getRawParameterValue("postEqGain2")->load(),
            apvts_.getRawParameterValue("postEqQ2")->load());
        postAmpEQ_->processBlock(buffer, midiMessages);
    }

    // 8. Master output
    {
        const float gainDb = apvts_.getRawParameterValue("masterOutput")->load();
        const float gain = juce::Decibels::decibelsToGain(gainDb);
        buffer.applyGain(gain);
    }
}

//==============================================================================
juce::AudioProcessorEditor* NAMBerryProcessor::createEditor()
{
    return new NAMBerryEditor(*this);
}

//==============================================================================
bool NAMBerryProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Only stereo in / stereo out for now
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    return true;
}

//==============================================================================
void NAMBerryProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts_.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void NAMBerryProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr)
    {
        apvts_.replaceState (juce::ValueTree::fromXml (*xml));
    }
}

//==============================================================================
bool NAMBerryProcessor::loadNamModel (const juce::File& file)
{
    return namModel_->loadModel(file);
}

bool NAMBerryProcessor::loadNamPedalModel (const juce::File& file)
{
    return namPedal_->loadModel(file);
}

bool NAMBerryProcessor::loadIr (int slot, const juce::File& file)
{
    return irLoader_->loadIr(slot, file);
}

} // namespace namberry

//==============================================================================
// JUCE Plugin Factory Function
//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new namberry::NAMBerryProcessor();
}
