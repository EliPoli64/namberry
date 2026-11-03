// Renders each editor tab to a PNG so the UI can be reviewed headlessly.
//
// Usage:
//   NAMBerryUiSnapshot [output-dir]

#include "PluginEditor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <iostream>

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

    std::cout << "PASS\n";
    return 0;
}
