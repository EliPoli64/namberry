// Renders each editor tab to a PNG so the UI can be reviewed headlessly.
//
// Usage:
//   NAMBerryUiSnapshot [output-dir]

#include "PluginEditor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <iostream>

namespace {

juce::Component* findChildByName (juce::Component& parent, const juce::String& name)
{
    for (int i = 0; i < parent.getNumChildComponents(); ++i)
    {
        auto* child = parent.getChildComponent (i);

        if (child->getName() == name)
            return child;

        if (auto* found = findChildByName (*child, name))
            return found;
    }

    return nullptr;
}

juce::TextButton* findButton (juce::Component& parent, const juce::String& text)
{
    for (int i = 0; i < parent.getNumChildComponents(); ++i)
    {
        auto* child = parent.getChildComponent (i);

        if (auto* button = dynamic_cast<juce::TextButton*> (child))
            if (button->getButtonText() == text)
                return button;

        if (auto* found = findButton (*child, text))
            return found;
    }

    return nullptr;
}

void leftClick (juce::Component& target, float x, float y)
{
    const auto now = juce::Time::getCurrentTime();

    juce::MouseEvent event (juce::Desktop::getInstance().getMainMouseSource(),
                            { x, y },
                            juce::ModifierKeys::leftButtonModifier,
                            0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &target, &target, now, { x, y }, now, 1, false);

    target.mouseDown (event);
}

// Taps outside the popup panel, which is how the menu is dismissed on screen.
void dismissPopup (juce::Component& editor)
{
    if (auto* popup = findChildByName (editor, "FilePopup"))
        leftClick (*popup, 1.0f, 1.0f);
}

bool writePng (const juce::File& file, const juce::Image& image)
{
    juce::FileOutputStream stream (file);

    if (! stream.openedOk())
        return false;

    juce::PNGImageFormat png;
    return png.writeImageToStream (image, stream);
}

} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const auto outputDir = argc > 1
        ? juce::File (juce::String (argv[1]))
        : juce::File::getCurrentWorkingDirectory().getChildFile ("ui-snapshots");

    outputDir.createDirectory();

    namberry::NAMBerryProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000, 512);
    processor.prepareToPlay (48000, 512);

    // Prime the tuner so the snapshot shows a live note/cents reading.
    {
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        double phase = 0.0;

        for (int block = 0; block < 200; ++block)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float s = 0.2f * static_cast<float> (
                    std::sin (phase));
                phase += 2.0 * juce::MathConstants<double>::pi * 223.0 / 48000.0;
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }

            processor.processBlock (buffer, midi);
        }
    }

    namberry::NAMBerryEditor editor (processor);
    editor.setSize (1280, 640);

    juce::TabbedButtonBar* tabBar = nullptr;
    for (int i = 0; i < editor.getNumChildComponents(); ++i)
        if (auto* bar = dynamic_cast<juce::TabbedButtonBar*> (editor.getChildComponent (i)))
            tabBar = bar;

    if (tabBar == nullptr)
    {
        std::cerr << "FAIL: tab bar not found\n";
        return 1;
    }

    static const char* names[] = { "pre-fx", "amp", "post-eq", "ir", "post-fx" };

    for (int tab = 0; tab < 5; ++tab)
    {
        // No change message here: layoutContent() reads the tab bar's index,
        // so a resize is enough and the tool stays message-loop free.
        tabBar->setCurrentTabIndex (tab, false);
        editor.resized();

        const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
        const auto file = outputDir.getChildFile (juce::String (names[tab]) + ".png");

        juce::FileOutputStream stream (file);
        if (! stream.openedOk())
        {
            std::cerr << "FAIL: could not write " << file.getFullPathName() << "\n";
            return 1;
        }

        juce::PNGImageFormat png;
        if (! png.writeImageToStream (image, stream))
        {
            std::cerr << "FAIL: could not encode " << file.getFullPathName() << "\n";
            return 1;
        }

        std::cout << "Wrote " << file.getFullPathName() << "\n";
    }

    // Amp tab: tapping the grille screen lists the model folder.
    tabBar->setCurrentTabIndex (1, false);
    editor.resized();

    if (auto* screen = findChildByName (editor, "AmpScreen"))
    {
        leftClick (*screen, 4.0f, 4.0f);

        if (writePng (outputDir.getChildFile ("browser.png"),
                      editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f)))
            std::cout << "Wrote " << outputDir.getChildFile ("browser.png").getFullPathName() << "\n";
        else
            std::cerr << "FAIL: could not write browser snapshot\n";

        dismissPopup (editor);
    }
    else
    {
        std::cerr << "FAIL: amp model screen not found\n";
    }

    if (findButton (editor, "LOAD") == nullptr)
        std::cerr << "FAIL: amp LOAD button not found\n";

    // IR tab: tapping the IR screen lists the IR folder.
    tabBar->setCurrentTabIndex (3, false);
    editor.resized();

    if (auto* screen = findChildByName (editor, "IrScreen0"))
    {
        leftClick (*screen, 4.0f, 4.0f);

        if (writePng (outputDir.getChildFile ("browser-ir.png"),
                      editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f)))
            std::cout << "Wrote " << outputDir.getChildFile ("browser-ir.png").getFullPathName() << "\n";
        else
            std::cerr << "FAIL: could not write IR browser snapshot\n";

        dismissPopup (editor);
    }
    else
    {
        std::cerr << "FAIL: IR screen not found\n";
    }

    if (findButton (editor, "LOAD") == nullptr)
        std::cerr << "FAIL: IR LOAD button not found\n";

    std::cout << "PASS\n";
    return 0;
}
