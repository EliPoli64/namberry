#include "namberry/DirectoryBrowser.h"

#include <algorithm>

namespace namberry {

DirectoryBrowser::DirectoryBrowser()
{
    setOpaque (false);
    setWantsKeyboardFocus (true);

    addAndMakeVisible (pathLabel_);
    pathLabel_.setFont (juce::FontOptions (12.0f));
    pathLabel_.setColour (juce::Label::textColourId, juce::Colour (0xffa1a1aa));
    pathLabel_.setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (list_);
    list_.setModel (this);
    list_.setRowHeight (28);
    list_.setMultipleSelectionEnabled (false);
    list_.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list_.setColour (juce::ListBox::outlineColourId, juce::Colour (0xff34343a));
    list_.setOutlineThickness (1);

    auto configureButton = [this] (juce::TextButton& button, const juce::String& text)
    {
        addAndMakeVisible (button);
        button.setButtonText (text);
    };

    configureButton (upButton_, "UP");
    configureButton (rootButton_, "ROOT");
    configureButton (setRootButton_, "SET ROOT...");
    configureButton (loadButton_, "LOAD");
    configureButton (cancelButton_, "CANCEL");

    upButton_.onClick = [this]
    {
        if (current_ != root_)
            navigateTo (current_.getParentDirectory());
    };

    rootButton_.onClick = [this] { navigateTo (root_); };
    setRootButton_.onClick = [this] { chooseRoot(); };
    loadButton_.onClick = [this] { loadSelected(); };
    cancelButton_.onClick = [this] { setVisible (false); };

    setVisible (false);
}

DirectoryBrowser::~DirectoryBrowser() = default;

//==============================================================================
void DirectoryBrowser::open (const juce::String& title,
                             juce::StringArray extensions,
                             const juce::File& root,
                             std::function<void (const juce::File&)> onRootChanged,
                             std::function<void (const juce::File&)> onFileChosen)
{
    title_ = title;
    extensions_ = std::move (extensions);
    onRootChanged_ = std::move (onRootChanged);
    onFileChosen_ = std::move (onFileChosen);

    root_ = root.isDirectory() ? root
                               : juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    current_ = root_;

    refresh();

    setVisible (true);
    toFront (true);
    list_.grabKeyboardFocus();
}

//==============================================================================
void DirectoryBrowser::refresh()
{
    entries_.clear();

    if (current_ != root_ && current_.isAChildOf (root_))
        entries_.push_back ({ current_.getParentDirectory(), "..", true });

    std::vector<Entry> directories;
    std::vector<Entry> files;

    for (auto entry : juce::RangedDirectoryIterator (
             current_, false, "*",
             juce::File::findFiles | juce::File::findDirectories))
    {
        const auto file = entry.getFile();

        if (file.isHidden())
            continue;

        if (file.isDirectory())
            directories.push_back ({ file, file.getFileName(), true });
        else if (hasMatchingExtension (file))
            files.push_back ({ file, file.getFileName(), false });
    }

    auto byName = [] (const Entry& a, const Entry& b)
    {
        return a.name.compareIgnoreCase (b.name) < 0;
    };

    std::sort (directories.begin(), directories.end(), byName);
    std::sort (files.begin(), files.end(), byName);

    entries_.insert (entries_.end(), directories.begin(), directories.end());
    entries_.insert (entries_.end(), files.begin(), files.end());

    list_.updateContent();
    pathLabel_.setText (current_.getFullPathName(), juce::dontSendNotification);
    pathLabel_.setTooltip (current_.getFullPathName());
}

void DirectoryBrowser::navigateTo (const juce::File& directory)
{
    if (! directory.isDirectory() || ! isUnderRoot (directory))
        return;

    current_ = directory;
    refresh();
}

void DirectoryBrowser::setRoot (const juce::File& directory)
{
    if (! directory.isDirectory())
        return;

    root_ = directory;
    current_ = directory;

    if (onRootChanged_ != nullptr)
        onRootChanged_ (directory);

    refresh();
}

void DirectoryBrowser::chooseRoot()
{
    rootChooser_ = std::make_unique<juce::FileChooser> (
        "Choose root folder", root_, "*");

    rootChooser_->launchAsync (juce::FileBrowserComponent::openMode
                                 | juce::FileBrowserComponent::canSelectDirectories,
                               [this] (const juce::FileChooser& chooser)
                               {
                                   const auto directory = chooser.getResult();

                                   if (directory.isDirectory())
                                       setRoot (directory);
                               });
}

void DirectoryBrowser::loadSelected()
{
    const int row = list_.getSelectedRow();

    if (row < 0 || row >= (int) entries_.size())
        return;

    const auto entry = entries_[(size_t) row];

    if (entry.isDirectory)
    {
        navigateTo (entry.file);
        return;
    }

    if (onFileChosen_ != nullptr)
        onFileChosen_ (entry.file);

    setVisible (false);
}

//==============================================================================
bool DirectoryBrowser::isUnderRoot (const juce::File& file) const
{
    return file == root_ || file.isAChildOf (root_);
}

bool DirectoryBrowser::hasMatchingExtension (const juce::File& file) const
{
    if (extensions_.isEmpty())
        return true;

    for (const auto& extension : extensions_)
        if (file.hasFileExtension (extension))
            return true;

    return false;
}

//==============================================================================
int DirectoryBrowser::getNumRows()
{
    return (int) entries_.size();
}

void DirectoryBrowser::paintListBoxItem (int row, juce::Graphics& g,
                                         int width, int height, bool selected)
{
    if (row < 0 || row >= (int) entries_.size())
        return;

    const auto& entry = entries_[(size_t) row];
    auto bounds = juce::Rectangle<int> (0, 0, width, height);

    if (selected)
        g.fillAll (juce::Colour (0xff2d2d35));
    else if (row % 2 == 1)
        g.fillAll (juce::Colour (0x12ffffff));

    auto iconArea = bounds.removeFromLeft (30).reduced (7, 6);

    if (entry.isDirectory)
    {
        g.setColour (juce::Colour (0xff22d3ee));
        g.fillRoundedRectangle (iconArea.toFloat(), 2.0f);
        g.setColour (juce::Colour (0xff0e7490));
        g.fillRect (iconArea.getX(), iconArea.getY() - 2, iconArea.getWidth() / 2, 3);
    }
    else
    {
        g.setColour (juce::Colour (0xff8b8b93));
        g.drawRoundedRectangle (iconArea.toFloat().reduced (2.0f), 2.0f, 1.0f);
        g.drawLine (iconArea.getX() + 4.0f, (float) iconArea.getCentreY(),
                    iconArea.getRight() - 4.0f, (float) iconArea.getCentreY(), 1.0f);
    }

    g.setColour (entry.isDirectory ? juce::Colour (0xfff4f4f5)
                                   : juce::Colour (0xffd4d4d8));
    g.setFont (juce::FontOptions (14.0f,
                                  entry.isDirectory ? juce::Font::bold
                                                    : juce::Font::plain));
    g.drawFittedText (entry.name, bounds.reduced (6, 0),
                      juce::Justification::centredLeft, 1);
}

void DirectoryBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= (int) entries_.size())
        return;

    const auto entry = entries_[(size_t) row];

    if (entry.isDirectory)
    {
        navigateTo (entry.file);
        return;
    }

    list_.selectRow (row);
    loadSelected();
}

//==============================================================================
void DirectoryBrowser::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.72f));

    auto panel = innerBounds_.toFloat();

    g.setGradientFill (juce::ColourGradient (
        juce::Colour (0xff29292f), panel.getX(), panel.getY(),
        juce::Colour (0xff151517), panel.getX(), panel.getBottom(), false));
    g.fillRoundedRectangle (panel, 12.0f);

    g.setColour (juce::Colour (0xff22d3ee));
    g.drawRoundedRectangle (panel, 12.0f, 1.5f);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText (title_,
                innerBounds_.reduced (18).removeFromTop (32),
                juce::Justification::centredLeft);
}

void DirectoryBrowser::resized()
{
    innerBounds_ = getLocalBounds().withSizeKeepingCentre (
        juce::jmin (800, getWidth() - 40),
        juce::jmin (520, getHeight() - 40));

    auto r = innerBounds_.reduced (18);
    r.removeFromTop (36);
    pathLabel_.setBounds (r.removeFromTop (24));
    r.removeFromTop (6);

    auto buttonRow = r.removeFromBottom (48);

    const int buttonY = buttonRow.getY() + 4;
    const int buttonHeight = 40;
    int x = buttonRow.getX();

    upButton_.setBounds (x, buttonY, 70, buttonHeight); x += 76;
    rootButton_.setBounds (x, buttonY, 80, buttonHeight); x += 86;
    setRootButton_.setBounds (x, buttonY, 130, buttonHeight);

    const int right = buttonRow.getRight();
    cancelButton_.setBounds (right - 100, buttonY, 100, buttonHeight);
    loadButton_.setBounds (right - 210, buttonY, 100, buttonHeight);

    list_.setBounds (r.reduced (0, 4));
}

//==============================================================================
bool DirectoryBrowser::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        setVisible (false);
        return true;
    }

    if (key == juce::KeyPress::returnKey)
    {
        loadSelected();
        return true;
    }

    if (key == juce::KeyPress::backspaceKey)
    {
        if (current_ != root_)
            navigateTo (current_.getParentDirectory());

        return true;
    }

    return false;
}

void DirectoryBrowser::visibilityChanged()
{
    if (isVisible())
    {
        refresh();
        list_.grabKeyboardFocus();
    }
}

void DirectoryBrowser::mouseDown (const juce::MouseEvent& event)
{
    if (! innerBounds_.contains (event.getPosition()))
        setVisible (false);
}

} // namespace namberry
