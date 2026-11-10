#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

#include <functional>
#include <memory>
#include <vector>

namespace namberry {

class FilePopup;

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
    void mouseDown (const juce::MouseEvent& event) override;
    void toggleTabBypass (int tabIndex);
    void updateTabVisuals();
    void buildUi();
    void layoutContent();

    // File picking: clicking a screen opens the folder popup, the Load button
    // opens the system file chooser.
    void openFilePopup (juce::Component& anchor,
                        const juce::File& directory,
                        juce::StringArray extensions,
                        std::function<void (const juce::File&)> onFileChosen,
                        std::function<void (const juce::File&)> onDirectoryChanged);
    void openFileChooser (const juce::String& title,
                          const juce::File& startDirectory,
                          const juce::String& wildcard,
                          std::function<void (const juce::File&)> onFileChosen);

    void showPresetMenu();
    void savePresetAs();
    void loadPresetFromFile();

    NAMBerryProcessor& processor_;
    std::unique_ptr<juce::LookAndFeel_V4> lnf_;

    juce::Viewport viewport_;
    juce::Component content_;
    juce::TabbedButtonBar tabBar_;
    std::unique_ptr<juce::Component> bottomKnobs_;

    juce::ToggleButton reducedMotionButton_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> reducedMotionAttachment_;
    juce::TextButton presetButton_;
    std::unique_ptr<juce::FileChooser> presetChooser_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
    juce::File lastPresetFile_;

    std::vector<std::unique_ptr<juce::Component>> ownedControls_;
    std::vector<std::function<void()>> nameUpdaters_;
    std::function<void()> tunerUpdater_;
    std::vector<std::vector<juce::Component*>> tabPanels_;
    std::vector<juce::Component*> allPanels_;
    std::unique_ptr<FilePopup> filePopup_;
    int currentTabIndex_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NAMBerryEditor)
};

} // namespace namberry
