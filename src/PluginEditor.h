#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

#include <functional>
#include <memory>
#include <vector>

namespace namberry {

class DirectoryBrowser;

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
    enum class BrowserKind { Nam, Ir };

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void buildUi();
    void layoutContent();
    void openBrowser (BrowserKind kind,
                      std::function<void (const juce::File&)> onFileChosen);

    NAMBerryProcessor& processor_;
    std::unique_ptr<juce::LookAndFeel_V4> lnf_;

    juce::Viewport viewport_;
    juce::Component content_;
    juce::TabbedButtonBar tabBar_;

    juce::Label noteLabel_;
    juce::ToggleButton reducedMotionButton_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> reducedMotionAttachment_;

    std::vector<std::unique_ptr<juce::Component>> ownedControls_;
    std::vector<std::function<void()>> nameUpdaters_;
    std::unique_ptr<DirectoryBrowser> browser_;
    std::vector<std::vector<juce::Component*>> tabPanels_;
    std::vector<juce::Component*> allPanels_;
    int currentTabIndex_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NAMBerryEditor)
};

} // namespace namberry
