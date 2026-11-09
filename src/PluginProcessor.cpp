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

    // ---- Tuner reference ----
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "tuningReference", "Tuning Reference",
        juce::NormalisableRange<float>(400.0f, 480.0f, 0.1f), 440.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    // ---- Impersonator pickup shaper ----
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "impersonatorToNeck", "Impersonator To Neck", true));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "impersonatorLevel", "Impersonator Level",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "impersonatorBypass", "Impersonator Bypass", true));

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

    // ---- Shared pitch stage (capo + drop tune) ----
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        "capoFret", "Pitch Semitones", -12, 12, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "capoTone", "Pitch Tone Match",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "capoMix", "Pitch Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "capoQuality", "Pitch Quality Mode", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "capoBypass", "Pitch Bypass", false));

    // ---- Compressor ----
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "compAttack", "Comp Attack",
        juce::NormalisableRange<float>(0.1f, 1000.0f, 0.1f), 5.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "compRelease", "Comp Release",
        juce::NormalisableRange<float>(0.1f, 1000.0f, 0.1f), 120.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "compThreshold", "Comp Threshold",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -24.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "compRatio", "Comp Ratio",
        juce::NormalisableRange<float>(1.0f, 20.0f, 0.1f), 8.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "compMakeup", "Comp Makeup",
        juce::NormalisableRange<float>(-60.0f, 60.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "compTight", "Comp Tight", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "compBypass", "Comp Bypass", false));

    // ---- NAM Pedal / Preamp (separate .nam model) ----
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "pedalGain", "Pedal Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "pedalOutput", "Pedal Output",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "pedalBass", "Pedal Bass",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "pedalMid", "Pedal Mid",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "pedalTreble", "Pedal Treble",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "pedalBypass", "Pedal Bypass", true));

    // ---- Pre-Amp EQ (3 bands) ----
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "preEqBypass", "Pre EQ Bypass", false));

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
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "postEqBypass", "Post EQ Bypass", false));

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

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "postEqHpfFreq", "Post EQ HPF",
        juce::NormalisableRange<float>(20.0f, 1000.0f, 1.0f), 20.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "postEqLpfFreq", "Post EQ LPF",
        juce::NormalisableRange<float>(2000.0f, 20000.0f, 10.0f), 20000.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    // ---- NAM Amp controls ----
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "ampBypass", "Amp Bypass", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "ampGain", "Amp Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "ampBass", "Amp Bass",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "ampMid", "Amp Mid",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "ampTreble", "Amp Treble",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "ampPresence", "Amp Presence",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "ampDepth", "Amp Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "ampOutput", "Amp Output",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));

    // ---- Master output ----
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "masterOutput", "Master Output",
        juce::NormalisableRange<float>(-60.0f, 6.0f, 0.1f), 0.0f));

    // ---- UI preference: reduced motion ----
    // ---- Section bypass (right-click tab icons) ----
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "preFxBypass", "Pre FX Bypass", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "postFxBypass", "Post FX Bypass", false));

    // ---- Post effects ----
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "chorusBypass", "Chorus Bypass", true));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "chorusRate", "Chorus Rate",
        juce::NormalisableRange<float>(0.1f, 5.0f, 0.01f), 0.8f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "chorusDepth", "Chorus Depth",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 50.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "chorusFeedback", "Chorus Feedback",
        juce::NormalisableRange<float>(0.0f, 70.0f, 1.0f), 15.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "chorusDelay", "Chorus Delay",
        juce::NormalisableRange<float>(5.0f, 30.0f, 0.1f), 15.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "chorusMix", "Chorus Mix",
        juce::NormalisableRange<float>(0.0f, 50.0f, 1.0f), 50.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "flangerBypass", "Flanger Bypass", true));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "flangerRate", "Flanger Rate",
        juce::NormalisableRange<float>(0.05f, 10.0f, 0.01f), 0.4f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "flangerDepth", "Flanger Depth",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 60.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "flangerManual", "Flanger Manual",
        juce::NormalisableRange<float>(0.5f, 10.0f, 0.1f), 2.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "flangerFeedback", "Flanger Feedback",
        juce::NormalisableRange<float>(-95.0f, 95.0f, 1.0f), 40.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "flangerMix", "Flanger Mix",
        juce::NormalisableRange<float>(0.0f, 50.0f, 1.0f), 50.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "phaserBypass", "Phaser Bypass", true));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "phaserRate", "Phaser Rate",
        juce::NormalisableRange<float>(0.05f, 8.0f, 0.01f), 0.5f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "phaserDepth", "Phaser Depth",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 70.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "phaserFeedback", "Phaser Feedback",
        juce::NormalisableRange<float>(0.0f, 90.0f, 1.0f), 30.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "phaserStages", "Phaser Stages",
        juce::NormalisableRange<float>(4.0f, 12.0f, 4.0f), 8.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float value, int) { return juce::String (juce::roundToInt (value)); })));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "phaserMix", "Phaser Mix",
        juce::NormalisableRange<float>(0.0f, 50.0f, 1.0f), 50.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    // ---- Room ambience (short algorithmic room; not a long reverb) ----
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "roomSize", "Room Size",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 45.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "roomBlend", "Room Blend",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "delayBypass", "Delay Bypass", true));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "delayMix", "Delay Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.25f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "delayTimeMs", "Delay Time",
        juce::NormalisableRange<float>(10.0f, 2000.0f, 1.0f), 200.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "delayFeedback", "Delay Feedback",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.1f), 4.0f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "reverbBypass", "Reverb Bypass", true));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "reverbMix", "Reverb Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "reverbDecay", "Reverb Decay",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "reverbWidth", "Reverb Width",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.7f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "reducedMotion", "Reduced Motion", false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "irBypass", "IR Bypass", false));

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
namespace {

constexpr int kFxStateVersion = 3;

juce::ValueTree findParameterState (const juce::ValueTree& state, const juce::String& id)
{
    return state.getChildWithProperty ("id", id);
}

float parameterValue (const juce::ValueTree& state, const juce::String& id, float fallback)
{
    const auto child = findParameterState (state, id);
    return child.isValid() ? static_cast<float> (child.getProperty ("value", fallback))
                           : fallback;
}

void setParameterValue (juce::ValueTree& state, const juce::String& id, float value)
{
    auto child = findParameterState (state, id);

    if (child.isValid())
        child.setProperty ("value", value, nullptr);
}

void addParameterIfMissing (juce::ValueTree& state, const juce::String& id, float value)
{
    if (findParameterState (state, id).isValid())
        return;

    juce::ValueTree parameter ("PARAM");
    parameter.setProperty ("id", id, nullptr);
    parameter.setProperty ("value", value, nullptr);
    state.appendChild (parameter, nullptr);
}

void migrateRoomBypassState (juce::ValueTree& state)
{
    const auto roomBypass = findParameterState (state, "roomBypass");

    if (roomBypass.isValid()
        && static_cast<float> (roomBypass.getProperty ("value", 1.0f)) > 0.5f)
    {
        setParameterValue (state, "roomBlend", 0.0f);
    }
}

void migrateFxCamelCaseState (juce::ValueTree& state)
{
    // The original post-FX parameters stored mix/depth in 0..1. The revamped
    // pedals use 0..100 (%), so convert existing project state once.
    for (const auto* id : { "chorusMix", "chorusDepth",
                            "flangerMix", "flangerDepth",
                            "phaserMix", "phaserDepth" })
    {
        setParameterValue (state, id, parameterValue (state, id, 0.0f) * 100.0f);
    }

    setParameterValue (state, "flangerFeedback",
                       juce::jlimit (-95.0f, 95.0f,
                                     parameterValue (state, "flangerFeedback", 0.0f) * 95.0f));

    setParameterValue (state, "chorusRate",
                       juce::jlimit (0.1f, 5.0f, parameterValue (state, "chorusRate", 1.5f)));
    setParameterValue (state, "flangerRate",
                       juce::jlimit (0.05f, 10.0f, parameterValue (state, "flangerRate", 0.8f)));
    setParameterValue (state, "phaserRate",
                       juce::jlimit (0.05f, 8.0f, parameterValue (state, "phaserRate", 0.7f)));

    addParameterIfMissing (state, "chorusFeedback", 15.0f);
    addParameterIfMissing (state, "chorusDelay", 15.0f);
    addParameterIfMissing (state, "flangerManual", 2.0f);
    addParameterIfMissing (state, "phaserFeedback", 30.0f);
    addParameterIfMissing (state, "phaserStages", 8.0f);

    // Room was added after the original post-FX block; keep old projects
    // usable if they predate it. The room is now always available with a 0 %
    // default mix, so no bypass parameter is added.
    addParameterIfMissing (state, "roomSize", 45.0f);
    addParameterIfMissing (state, "roomBlend", 0.0f);

    state.setProperty ("fxVersion", 2, nullptr);
}

void migratePostFxRangeState (juce::ValueTree& state)
{
    // Version 3 narrowed the modulation mix controls from 0..100 to 0..50
    // and changed delay feedback from a coefficient to a 0..10 repeat control.
    for (const auto* id : { "chorusMix", "flangerMix", "phaserMix" })
    {
        setParameterValue (state, id,
                           juce::jlimit (0.0f, 50.0f,
                                         parameterValue (state, id, 50.0f)));
    }

    setParameterValue (state, "delayFeedback",
                       juce::jlimit (0.0f, 10.0f,
                                     parameterValue (state, "delayFeedback", 0.4f) * 10.0f));

    addParameterIfMissing (state, "preFxBypass", 0.0f);
    addParameterIfMissing (state, "postFxBypass", 0.0f);
    state.setProperty ("fxVersion", kFxStateVersion, nullptr);
}

} // namespace

//==============================================================================
NAMBerryProcessor::NAMBerryProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_ (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    // Persistent, per-user settings (last-used NAM/IR folders).
    juce::PropertiesFile::Options options;
    options.applicationName = "NAMBerry";
    options.filenameSuffix = "settings";
    options.folderName = "NAMBerry";
    options.osxLibrarySubFolder = "Application Support";

    const auto settingsFile =
        juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("NAMBerry")
            .getChildFile ("NAMBerry.settings");

    settingsFile.getParentDirectory().createDirectory();
    settings_ = std::make_unique<juce::PropertiesFile> (settingsFile, options);

    // Instantiate all DSP modules
    dropTuner_ = std::make_unique<DropTuner>();
    noiseGate_ = std::make_unique<NoiseGate>();
    compressor_ = std::make_unique<Compressor>();
    digitalCapo_ = std::make_unique<DigitalCapo>();
    impersonator_ = std::make_unique<Impersonator>();
    namModel_ = std::make_unique<NamModelLoader>();
    namPedal_ = std::make_unique<NamModelLoader>();
    irLoader_ = std::make_unique<IrLoader>();
    preAmpEQ_ = std::make_unique<MultiBandEQ>();
    postAmpEQ_ = std::make_unique<MultiBandEQ>();
    postEffects_ = std::make_unique<PostEffects>();
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
    digitalCapo_->setQualityMode (apvts_.getRawParameterValue("capoQuality")->load() > 0.5f
        ? PitchShifter::QualityMode::quality
        : PitchShifter::QualityMode::lowLatency);
    digitalCapo_->prepare(spec);
    impersonator_->prepare(spec);

    const int initialPitchLatency = digitalCapo_->getReportedLatencySamples();
    setLatencySamples (initialPitchLatency);
    pendingLatencySamples_.store (initialPitchLatency);
    appliedLatencySamples_.store (initialPitchLatency);

    namModel_->prepare(spec);
    namPedal_->prepare(spec);
    irLoader_->prepare(spec);
    preAmpEQ_->prepare(spec);
    postAmpEQ_->prepare(spec);
    postEffects_->prepare (spec);

    restoreSavedFiles();
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
    // Signal chain order (see spec.md §3.1):
    // Input → Tuner (detect) → Pitch (capo/drop) → NoiseGate → Compressor →
    // NAM Pedal → Pre-Amp EQ → NAM Amp → Post-Amp EQ → IR Loader → Post FX → Master
    //==============================================================================

    const bool preFxBypassed = apvts_.getRawParameterValue("preFxBypass")->load() > 0.5f;

    // 1. Tuner (detection/display only; the shared pitch stage does the shifting)
    if (auto* reference = apvts_.getRawParameterValue ("tuningReference"))
        dropTuner_->setReferenceFrequency (reference->load());

    dropTuner_->processBlock(buffer, midiMessages);

    // 2. Impersonator pickup shaper (after the tuner detector, before pitch)
    {
        const bool toNeck = apvts_.getRawParameterValue("impersonatorToNeck")->load() > 0.5f;

        impersonator_->setBypass (preFxBypassed
                                  || apvts_.getRawParameterValue("impersonatorBypass")->load() > 0.5f);
        impersonator_->setMode (toNeck ? Impersonator::Mode::bridgeToNeck
                                       : Impersonator::Mode::neckToBridge);
        impersonator_->setOutputLevelDb (apvts_.getRawParameterValue("impersonatorLevel")->load());
        impersonator_->processBlock (buffer, midiMessages);
    }

    // 3. Shared pitch stage for the digital capo and drop tuning
    {
        const bool pitchBypassed = apvts_.getRawParameterValue("capoBypass")->load() > 0.5f;
        const float semitones = apvts_.getRawParameterValue("capoFret")->load();
        const float tone = apvts_.getRawParameterValue("capoTone")->load() / 100.0f;
        const float mix = apvts_.getRawParameterValue("capoMix")->load() / 100.0f;

        digitalCapo_->setQualityMode (apvts_.getRawParameterValue("capoQuality")->load() > 0.5f
            ? PitchShifter::QualityMode::quality
            : PitchShifter::QualityMode::lowLatency);
        digitalCapo_->setBypass (preFxBypassed || pitchBypassed);
        digitalCapo_->setSemitones (semitones);
        digitalCapo_->setTone (tone);
        digitalCapo_->setMix (mix);
        digitalCapo_->processBlock (buffer, midiMessages);

        const int pitchLatency = digitalCapo_->getReportedLatencySamples();

        // Never call setLatencySamples() from the audio thread. Some hosts
        // respond by reconfiguring their audio stream, which can produce the
        // ALSA "broken pipe" errors seen when pitch was activated.
        if (pitchLatency != appliedLatencySamples_.load()
            && pitchLatency != pendingLatencySamples_.load())
        {
            pendingLatencySamples_.store (pitchLatency);
            triggerAsyncUpdate();
        }
    }

    // 3. Noise Gate
    {
        noiseGate_->setThresholdDb(apvts_.getRawParameterValue("gateThreshold")->load());
        noiseGate_->setAttackMs(apvts_.getRawParameterValue("gateAttack")->load());
        noiseGate_->setReleaseMs(apvts_.getRawParameterValue("gateRelease")->load());
        noiseGate_->setBypass(preFxBypassed
                              || apvts_.getRawParameterValue("gateBypass")->load() > 0.5f);
        noiseGate_->processBlock(buffer, midiMessages);
    }

    // 4. Compressor
    {
        const bool compBypassed = apvts_.getRawParameterValue("compBypass")->load() > 0.5f;
        compressor_->setBypass(preFxBypassed || compBypassed);
        compressor_->setAttackMs(apvts_.getRawParameterValue("compAttack")->load());
        compressor_->setReleaseMs(apvts_.getRawParameterValue("compRelease")->load());
        compressor_->setThresholdDb(apvts_.getRawParameterValue("compThreshold")->load());
        compressor_->setRatio(apvts_.getRawParameterValue("compRatio")->load());
        compressor_->setMakeupGainDb(apvts_.getRawParameterValue("compMakeup")->load());
        compressor_->setTightMode(apvts_.getRawParameterValue("compTight")->load() > 0.5f);
        compressor_->processBlock(buffer, midiMessages);
    }

    // 5. NAM Pedal / Preamp model (optional, bypassable) — runs before the amp section
    {
        const bool pedalBypassed = apvts_.getRawParameterValue("pedalBypass")->load() > 0.5f;
        if (!preFxBypassed && !pedalBypassed && namPedal_->hasModel())
        {
            const float pedalGainDb = apvts_.getRawParameterValue("pedalGain")->load();
            const float pedalOutputDb = apvts_.getRawParameterValue("pedalOutput")->load();
            namPedal_->setBypass(false);
            namPedal_->setGain(juce::Decibels::decibelsToGain(pedalGainDb));
            namPedal_->setBass(apvts_.getRawParameterValue("pedalBass")->load());
            namPedal_->setMid(apvts_.getRawParameterValue("pedalMid")->load());
            namPedal_->setTreble(apvts_.getRawParameterValue("pedalTreble")->load());
            namPedal_->setOutputLevel(juce::Decibels::decibelsToGain(pedalOutputDb));
            namPedal_->processBlock(buffer, midiMessages);
        }
        else
        {
            namPedal_->setBypass(true);
        }
    }

    // 6. Pre-Amp EQ
    {
        const bool preEqBypassed = apvts_.getRawParameterValue("preEqBypass")->load() > 0.5f;
        preAmpEQ_->setBypass(preFxBypassed || preEqBypassed);
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

    // 7. NAM Amp Model
    {
        const bool ampBypassed = apvts_.getRawParameterValue("ampBypass")->load() > 0.5f;
        const float ampGainDb = apvts_.getRawParameterValue("ampGain")->load();
        const float ampOutputDb = apvts_.getRawParameterValue("ampOutput")->load();
        namModel_->setBypass(ampBypassed);
        namModel_->setGain(juce::Decibels::decibelsToGain(ampGainDb));
        namModel_->setBass(apvts_.getRawParameterValue("ampBass")->load());
        namModel_->setMid(apvts_.getRawParameterValue("ampMid")->load());
        namModel_->setTreble(apvts_.getRawParameterValue("ampTreble")->load());
        namModel_->setPresence(apvts_.getRawParameterValue("ampPresence")->load());
        namModel_->setDepth(apvts_.getRawParameterValue("ampDepth")->load());
        namModel_->setOutputLevel(juce::Decibels::decibelsToGain(ampOutputDb));
        namModel_->processBlock(buffer, midiMessages);
    }

    // 8. Post-Amp EQ
    {
        const bool postEqBypassed = apvts_.getRawParameterValue("postEqBypass")->load() > 0.5f;
        postAmpEQ_->setBypass(postEqBypassed);
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
        postAmpEQ_->setHighPassFrequency (apvts_.getRawParameterValue("postEqHpfFreq")->load());
        postAmpEQ_->setLowPassFrequency (apvts_.getRawParameterValue("postEqLpfFreq")->load());
        postAmpEQ_->processBlock(buffer, midiMessages);
    }

    // 9. IR Loader (4 slots)
    {
        const bool irBypassed = apvts_.getRawParameterValue("irBypass")->load() > 0.5f;
        irLoader_->setBypass(irBypassed);
        for (int slot = 0; slot < 4; ++slot)
        {
            const auto idx = juce::String(slot);
            irLoader_->setSlotMix(slot, apvts_.getRawParameterValue("irMix" + idx)->load());
            irLoader_->setSlotPan(slot, apvts_.getRawParameterValue("irPan" + idx)->load());
            irLoader_->setSlotPhaseInvert(slot, apvts_.getRawParameterValue("irPhase" + idx)->load() > 0.5f);
        }
        irLoader_->processBlock(buffer, midiMessages);
    }

    // 10. Post effects
    {
        const bool postFxBypassed = apvts_.getRawParameterValue("postFxBypass")->load() > 0.5f;

        postEffects_->setBypass (postFxBypassed
            || (apvts_.getRawParameterValue("chorusBypass")->load() > 0.5f
                && apvts_.getRawParameterValue("flangerBypass")->load() > 0.5f
                && apvts_.getRawParameterValue("phaserBypass")->load() > 0.5f
                && apvts_.getRawParameterValue("roomBlend")->load() <= 0.001f
                && apvts_.getRawParameterValue("delayBypass")->load() > 0.5f
                && apvts_.getRawParameterValue("reverbBypass")->load() > 0.5f));

        postEffects_->setChorusBypass (apvts_.getRawParameterValue("chorusBypass")->load() > 0.5f);
        postEffects_->setChorusMix (apvts_.getRawParameterValue("chorusMix")->load());
        postEffects_->setChorusRate (apvts_.getRawParameterValue("chorusRate")->load());
        postEffects_->setChorusDepth (apvts_.getRawParameterValue("chorusDepth")->load());
        postEffects_->setChorusFeedback (apvts_.getRawParameterValue("chorusFeedback")->load());
        postEffects_->setChorusDelay (apvts_.getRawParameterValue("chorusDelay")->load());

        postEffects_->setFlangerBypass (apvts_.getRawParameterValue("flangerBypass")->load() > 0.5f);
        postEffects_->setFlangerMix (apvts_.getRawParameterValue("flangerMix")->load());
        postEffects_->setFlangerRate (apvts_.getRawParameterValue("flangerRate")->load());
        postEffects_->setFlangerDepth (apvts_.getRawParameterValue("flangerDepth")->load());
        postEffects_->setFlangerManual (apvts_.getRawParameterValue("flangerManual")->load());
        postEffects_->setFlangerFeedback (apvts_.getRawParameterValue("flangerFeedback")->load());

        postEffects_->setPhaserBypass (apvts_.getRawParameterValue("phaserBypass")->load() > 0.5f);
        postEffects_->setPhaserMix (apvts_.getRawParameterValue("phaserMix")->load());
        postEffects_->setPhaserRate (apvts_.getRawParameterValue("phaserRate")->load());
        postEffects_->setPhaserDepth (apvts_.getRawParameterValue("phaserDepth")->load());
        postEffects_->setPhaserFeedback (apvts_.getRawParameterValue("phaserFeedback")->load());
        postEffects_->setPhaserStages (static_cast<int> (apvts_.getRawParameterValue("phaserStages")->load()));

        postEffects_->setRoomSize (apvts_.getRawParameterValue("roomSize")->load() * 0.01f);
        postEffects_->setRoomBlend (apvts_.getRawParameterValue("roomBlend")->load() * 0.01f);

        postEffects_->setDelayBypass (apvts_.getRawParameterValue("delayBypass")->load() > 0.5f);
        postEffects_->setDelayMix (apvts_.getRawParameterValue("delayMix")->load());
        postEffects_->setDelayTimeMs (apvts_.getRawParameterValue("delayTimeMs")->load());
        postEffects_->setDelayFeedback (apvts_.getRawParameterValue("delayFeedback")->load());

        postEffects_->setReverbBypass (apvts_.getRawParameterValue("reverbBypass")->load() > 0.5f);
        postEffects_->setReverbMix (apvts_.getRawParameterValue("reverbMix")->load());
        postEffects_->setReverbDecay (apvts_.getRawParameterValue("reverbDecay")->load());
        postEffects_->setReverbWidth (apvts_.getRawParameterValue("reverbWidth")->load());

        postEffects_->processBlock(buffer, midiMessages);
    }

    // 11. Master output. A fixed +6 dB trim makes the plugin sit at a more
    // useful level without changing the saved Master value.
    {
        const float gainDb = apvts_.getRawParameterValue("masterOutput")->load();
        const float gain = juce::Decibels::decibelsToGain (gainDb + 6.0f);
        buffer.applyGain(gain);
    }
}

//==============================================================================
void NAMBerryProcessor::handleAsyncUpdate()
{
    const int latency = pendingLatencySamples_.load();

    if (latency != appliedLatencySamples_.load())
    {
        setLatencySamples (latency);
        appliedLatencySamples_.store (latency);
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
    state.setProperty ("fxVersion", kFxStateVersion, nullptr);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void NAMBerryProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr)
    {
        auto state = juce::ValueTree::fromXml (*xml);

        if (state.isValid())
        {
            const int version = static_cast<int> (state.getProperty ("fxVersion", 0));

            if (version < 2)
                migrateFxCamelCaseState (state);

            if (version < kFxStateVersion)
                migratePostFxRangeState (state);

            migrateRoomBypassState (state);
            apvts_.replaceState (state);
        }
    }
}

//==============================================================================
bool NAMBerryProcessor::loadNamModel (const juce::File& file)
{
    const bool loaded = namModel_->loadModel(file);

    if (loaded && settings_ != nullptr)
    {
        settings_->setValue ("ampModelPath", file.getFullPathName());
        settings_->saveIfNeeded();
    }

    return loaded;
}

bool NAMBerryProcessor::loadNamPedalModel (const juce::File& file)
{
    const bool loaded = namPedal_->loadModel(file);

    if (loaded && settings_ != nullptr)
    {
        settings_->setValue ("pedalModelPath", file.getFullPathName());
        settings_->saveIfNeeded();
    }

    return loaded;
}

bool NAMBerryProcessor::loadIr (int slot, const juce::File& file)
{
    const bool loaded = irLoader_->loadIr(slot, file);

    if (loaded && settings_ != nullptr)
    {
        settings_->setValue ("irSlot" + juce::String (slot) + "Path",
                             file.getFullPathName());
        settings_->saveIfNeeded();
    }

    return loaded;
}

//==============================================================================
bool NAMBerryProcessor::savePreset (const juce::File& file)
{
    const auto target = file.hasFileExtension ("namberrypreset")
        ? file
        : file.withFileExtension ("namberrypreset");

    target.getParentDirectory().createDirectory();

    auto root = std::make_unique<juce::XmlElement> ("NAMBERRYPRESET");
    root->setAttribute ("version", 1);

    auto state = apvts_.copyState();
    state.setProperty ("fxVersion", kFxStateVersion, nullptr);

    if (auto stateXml = std::unique_ptr<juce::XmlElement> (state.createXml()))
        root->addChildElement (stateXml.release());

    auto* files = root->createNewChildElement ("FILES");

    auto addFile = [files] (const char* name, const juce::File& path)
    {
        if (path.existsAsFile())
            files->setAttribute (name, path.getFullPathName());
    };

    addFile ("amp", namModel_ != nullptr ? namModel_->getLoadedFile() : juce::File());
    addFile ("pedal", namPedal_ != nullptr ? namPedal_->getLoadedFile() : juce::File());

    if (irLoader_ != nullptr)
        for (int slot = 0; slot < 4; ++slot)
            addFile (("ir" + juce::String (slot)).toRawUTF8(),
                     irLoader_->getLoadedFile (slot));

    if (! root->writeTo (target))
        return false;

    setPresetDirectory (target.getParentDirectory());
    return true;
}

bool NAMBerryProcessor::loadPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr || ! xml->hasTagName ("NAMBERRYPRESET"))
        return false;

    auto* params = xml->getChildByName ("PARAMETERS");

    if (params == nullptr)
        return false;

    juce::MemoryBlock stateBlock;
    copyXmlToBinary (*params, stateBlock);
    setStateInformation (stateBlock.getData(), static_cast<int> (stateBlock.getSize()));

    bool filesOk = true;

    if (auto* files = xml->getChildByName ("FILES"))
    {
        auto loadFile = [this, &filesOk, files] (const char* attribute,
                                                 std::function<bool (const juce::File&)> loader)
        {
            const auto path = files->getStringAttribute (attribute);

            if (path.isEmpty())
                return;

            const juce::File modelOrIr (path);

            if (! modelOrIr.existsAsFile() || ! loader (modelOrIr))
                filesOk = false;
        };

        loadFile ("amp", [this] (const juce::File& f) { return loadNamModel (f); });
        loadFile ("pedal", [this] (const juce::File& f) { return loadNamPedalModel (f); });

        for (int slot = 0; slot < 4; ++slot)
        {
            const auto attribute = "ir" + juce::String (slot);
            loadFile (attribute.toRawUTF8(),
                      [this, slot] (const juce::File& f) { return loadIr (slot, f); });
        }
    }

    setPresetDirectory (file.getParentDirectory());
    return filesOk;
}

juce::File NAMBerryProcessor::getPresetDirectory() const
{
    if (settings_ != nullptr)
    {
        const auto path = settings_->getValue ("lastPresetDirectory");

        if (path.isNotEmpty())
        {
            const juce::File directory (path);

            if (directory.isDirectory())
                return directory;
        }
    }

    const auto directory = juce::File::getSpecialLocation (
        juce::File::userApplicationDataDirectory)
            .getChildFile ("NAMBerry")
            .getChildFile ("Presets");

    directory.createDirectory();
    return directory;
}

void NAMBerryProcessor::setPresetDirectory (const juce::File& directory)
{
    if (settings_ == nullptr || ! directory.isDirectory())
        return;

    settings_->setValue ("lastPresetDirectory", directory.getFullPathName());
    settings_->saveIfNeeded();
}

void NAMBerryProcessor::restoreSavedFiles()
{
    if (settings_ == nullptr)
        return;

    auto restoreNam = [this] (const juce::String& key, bool pedal)
    {
        auto* loader = pedal ? namPedal_.get() : namModel_.get();

        if (loader == nullptr || loader->hasModel())
            return;

        const auto path = settings_->getValue (key);

        if (path.isEmpty())
            return;

        const juce::File file (path);

        if (file.existsAsFile())
        {
            if (pedal)
                loadNamPedalModel (file);
            else
                loadNamModel (file);
        }
    };

    restoreNam ("ampModelPath", false);
    restoreNam ("pedalModelPath", true);

    if (irLoader_ != nullptr)
    {
        for (int slot = 0; slot < 4; ++slot)
        {
            const auto path = settings_->getValue ("irSlot" + juce::String (slot) + "Path");

            if (path.isEmpty())
                continue;

            const juce::File file (path);

            if (file.existsAsFile())
                irLoader_->loadIr (slot, file);
        }
    }
}

//==============================================================================
juce::File NAMBerryProcessor::getNamDirectory() const
{
    if (settings_ != nullptr)
    {
        const auto path = settings_->getValue ("lastAmpDirectory");

        if (path.isNotEmpty())
            return juce::File (path);

        // Migrate the old shared NAM root to the amp root.
        const auto legacy = settings_->getValue ("lastNamDirectory");

        if (legacy.isNotEmpty())
            return juce::File (legacy);
    }

    return juce::File::getSpecialLocation (juce::File::userHomeDirectory);
}

void NAMBerryProcessor::setNamDirectory (const juce::File& directory)
{
    if (settings_ != nullptr && directory.isDirectory())
    {
        settings_->setValue ("lastAmpDirectory", directory.getFullPathName());
        settings_->saveIfNeeded();
    }
}

juce::File NAMBerryProcessor::getPedalDirectory() const
{
    if (settings_ != nullptr)
    {
        const auto path = settings_->getValue ("lastPedalDirectory");

        if (path.isNotEmpty())
            return juce::File (path);

        // Until the user picks a pedal folder, start from the amp folder.
        const auto legacy = settings_->getValue ("lastNamDirectory");

        if (legacy.isNotEmpty())
            return juce::File (legacy);
    }

    return getNamDirectory();
}

void NAMBerryProcessor::setPedalDirectory (const juce::File& directory)
{
    if (settings_ != nullptr && directory.isDirectory())
    {
        settings_->setValue ("lastPedalDirectory", directory.getFullPathName());
        settings_->saveIfNeeded();
    }
}

juce::File NAMBerryProcessor::getIrDirectory() const
{
    if (settings_ != nullptr)
    {
        const auto path = settings_->getValue ("lastIrDirectory");

        if (path.isNotEmpty())
            return juce::File (path);
    }

    return juce::File::getSpecialLocation (juce::File::userHomeDirectory);
}

void NAMBerryProcessor::setIrDirectory (const juce::File& directory)
{
    if (settings_ != nullptr && directory.isDirectory())
    {
        settings_->setValue ("lastIrDirectory", directory.getFullPathName());
        settings_->saveIfNeeded();
    }
}

} // namespace namberry

//==============================================================================
// JUCE Plugin Factory Function
//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new namberry::NAMBerryProcessor();
}
