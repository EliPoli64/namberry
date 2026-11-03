#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

namespace namberry {

class NAMBerryEditor : public juce::AudioProcessorEditor
{
public:
    explicit NAMBerryEditor (NAMBerryProcessor&);
    ~NAMBerryEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    NAMBerryProcessor& processor_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NAMBerryEditor)
};

} // namespace namberry
