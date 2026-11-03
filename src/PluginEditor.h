#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

#include <memory>
#include <vector>

namespace namberry {

class NAMBerryEditor : public juce::AudioProcessorEditor,
                       private juce::Timer,
                       private juce::ChangeListener
{
public:
    explicit NAMBerryEditor (NAMBerryProcessor&);
    ~NAMBerryEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void buildUi();
    void layoutContent();

    NAMBerryProcessor& processor_;
    juce::LookAndFeel_V4 lnf_;

    juce::Viewport viewport_;
    juce::Component content_;
    juce::TabbedButtonBar tabBar_;

    juce::Label noteLabel_;
    juce::ToggleButton reducedMotionButton_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> reducedMotionAttachment_;

    std::vector<std::unique_ptr<juce::Component>> ownedControls_;
    std::vector<std::vector<juce::Component*>> tabPanels_;
    std::vector<juce::Component*> allPanels_;
    int currentTabIndex_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NAMBerryEditor)
};

} // namespace namberry
