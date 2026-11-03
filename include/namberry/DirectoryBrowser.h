#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace namberry {

/// In-editor directory browser used by the NAM and IR loaders.
/// The root folder is persisted by the caller; the user can traverse
/// subfolders and pick a file without leaving the plugin.
class DirectoryBrowser : public juce::Component,
                         private juce::ListBoxModel
{
public:
    DirectoryBrowser();
    ~DirectoryBrowser() override;

    void open (const juce::String& title,
               juce::StringArray extensions,
               const juce::File& root,
               std::function<void (const juce::File&)> onRootChanged,
               std::function<void (const juce::File&)> onFileChosen);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    struct Entry
    {
        juce::File file;
        juce::String name;
        bool isDirectory = false;
    };

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height,
                           bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

    void refresh();
    void navigateTo (const juce::File& directory);
    void setRoot (const juce::File& directory);
    void loadSelected();
    void chooseRoot();
    bool isUnderRoot (const juce::File& file) const;
    bool hasMatchingExtension (const juce::File& file) const;

    juce::String title_;
    juce::StringArray extensions_;
    juce::File root_;
    juce::File current_;
    std::vector<Entry> entries_;

    std::function<void (const juce::File&)> onRootChanged_;
    std::function<void (const juce::File&)> onFileChosen_;

    juce::Label pathLabel_;
    juce::ListBox list_;
    juce::TextButton upButton_, rootButton_, setRootButton_, loadButton_, cancelButton_;
    std::unique_ptr<juce::FileChooser> rootChooser_;

    juce::Rectangle<int> innerBounds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DirectoryBrowser)
};

} // namespace namberry
