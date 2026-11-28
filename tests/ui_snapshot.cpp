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

juce::Viewport* findViewport (juce::Component& parent)
{
    for (int i = 0; i < parent.getNumChildComponents(); ++i)
    {
        auto* child = parent.getChildComponent (i);

        if (auto* viewport = dynamic_cast<juce::Viewport*> (child))
            return viewport;

        if (auto* found = findViewport (*child))
            return found;
    }

    return nullptr;
}

juce::MouseEvent makeMouseEvent (juce::Component& target,
                                 juce::Point<float> position,
                                 juce::ModifierKeys modifiers,
                                 int numberOfClicks = 1)
{
    const auto now = juce::Time::getCurrentTime();

    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                             position,
                             modifiers,
                             0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                             &target, &target, now, position, now, numberOfClicks, false);
}

void leftClick (juce::Component& target, float x, float y)
{
    target.mouseDown (makeMouseEvent (target, { x, y },
                                      juce::ModifierKeys::leftButtonModifier));
}

void dragLeft (juce::Component& target, juce::Point<float> from, juce::Point<float> to)
{
    target.mouseDown (makeMouseEvent (target, from, juce::ModifierKeys::leftButtonModifier));
    target.mouseDrag (makeMouseEvent (target, to, juce::ModifierKeys::leftButtonModifier));
    target.mouseUp (makeMouseEvent (target, to, juce::ModifierKeys()));
}

void wheelOver (juce::Component& target, juce::Point<float> position, float deltaY)
{
    juce::MouseWheelDetails wheel;
    wheel.deltaY = deltaY;
    target.mouseWheelMove (makeMouseEvent (target, position, juce::ModifierKeys(), 0), wheel);
}

void doubleClick (juce::Component& target, juce::Point<float> position)
{
    target.mouseDown (makeMouseEvent (target, position, juce::ModifierKeys::leftButtonModifier));
    target.mouseUp (makeMouseEvent (target, position, juce::ModifierKeys()));
    target.mouseDoubleClick (makeMouseEvent (target, position,
                                             juce::ModifierKeys::leftButtonModifier, 2));
}

// Taps outside the popup panel, which is how the menu is dismissed on screen.
void dismissPopup (juce::Component& editor)
{
    if (auto* popup = findChildByName (editor, "FilePopup"))
        leftClick (*popup, 1.0f, 1.0f);
}

bool writePng (const juce::File& file, const juce::Image& image)
{
    // FileOutputStream appends to an existing file, so truncate first or every
    // run would leave the previous PNG at the start of the file.
    file.deleteFile();

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
    editor.setSize (1280, 720);

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

        auto* viewport = findViewport (editor);
        auto* content = viewport != nullptr ? viewport->getViewedComponent() : nullptr;

        if (viewport == nullptr || content == nullptr)
        {
            std::cerr << "FAIL: tab content viewport not found\n";
            return 1;
        }

        if (content->getWidth() > viewport->getWidth()
            || content->getHeight() > viewport->getHeight())
        {
            std::cerr << "FAIL: " << names[tab] << " overflows the default viewport "
                      << content->getWidth() << "x" << content->getHeight()
                      << " vs " << viewport->getWidth() << "x"
                      << viewport->getHeight() << "\n";
            return 1;
        }

        if (tab == 1)
        {
            auto* screen = findChildByName (editor, "AmpScreen");
            auto* header = screen != nullptr ? screen->getParentComponent() : nullptr;

            if (header == nullptr)
            {
                std::cerr << "FAIL: amp model screen not found\n";
                return 1;
            }

            // The amp screen is centred inside the header row, with the
            // previous/next arrows flanking it on either side.
            const int expectedCentre = header->getLocalBounds().getCentreX();
            const int actualCentre = screen->getBounds().getCentreX();

            if (std::abs (actualCentre - expectedCentre) > 2)
            {
                std::cerr << "FAIL: amp model screen not centred in header "
                          << actualCentre << " vs " << expectedCentre << "\n";
                return 1;
            }

            bool foundPrev = false, foundNext = false;

            for (int i = 0; i < header->getNumChildComponents(); ++i)
            {
                if (auto* button = dynamic_cast<juce::Button*> (header->getChildComponent (i)))
                {
                    if (button->getButtonText() == "<" && button->getRight() <= screen->getX())
                        foundPrev = true;
                    else if (button->getButtonText() == ">"
                             && button->getX() >= screen->getRight())
                        foundNext = true;
                }
            }

            if (! foundPrev || ! foundNext)
            {
                std::cerr << "FAIL: amp seek arrows not flanking the screen\n";
                return 1;
            }
        }

        const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
        const auto file = outputDir.getChildFile (juce::String (names[tab]) + ".png");

        // Truncate: FileOutputStream appends when the file already exists.
        file.deleteFile();

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

    // Post FX tab: both convolution reverb IR slots use the same file picker
    // pattern as the cabinet loader, each with its own screen and folder.
    tabBar->setCurrentTabIndex (4, false);
    editor.resized();

    for (int slot = 0; slot < 2; ++slot)
    {
        const auto screenName = "ReverbIrScreen" + juce::String (slot);
        auto* screen = findChildByName (editor, screenName);

        if (screen == nullptr)
        {
            std::cerr << "FAIL: " << screenName << " not found\n";
            return 1;
        }

        if (! screen->isVisible() || screen->getWidth() < 40
            || screen->getHeight() < 12)
        {
            std::cerr << "FAIL: " << screenName << " not laid out\n";
            return 1;
        }

        leftClick (*screen, 4.0f, 4.0f);

        if (findChildByName (editor, "FilePopup") == nullptr)
        {
            std::cerr << "FAIL: " << screenName
                      << " did not open the file popup\n";
            return 1;
        }

        dismissPopup (editor);
    }

    if (findButton (editor, "LOAD") == nullptr)
    {
        std::cerr << "FAIL: reverb IR LOAD button not found\n";
        return 1;
    }

    // Post EQ tab: drag band 1, change its Q with the wheel, then double-click
    // it back to 0 dB. This exercises the graphical editor without a screen.
    tabBar->setCurrentTabIndex (2, false);
    editor.resized();

    bool interactionOk = true;

    if (auto* graph = findChildByName (editor, "PostEqGraph"))
    {
        const float width = (float) graph->getWidth();
        const float height = (float) graph->getHeight();
        const float plotX = 52.0f;
        const float plotY = 46.0f;
        const float plotWidth = width - 192.0f;
        const float plotHeight = height - 94.0f;

        auto xForFrequency = [plotX, plotWidth] (float frequency)
        {
            const auto normalised = std::log (frequency / 20.0f) / std::log (1000.0f);
            return plotX + normalised * plotWidth;
        };

        auto yForGain = [plotY, plotHeight] (float gainDb)
        {
            return plotY + (24.0f - gainDb) / 48.0f * plotHeight;
        };

        auto value = [&processor] (const char* id)
        {
            auto* parameter = processor.getValueTreeState().getRawParameterValue (id);
            return parameter != nullptr ? parameter->load() : -9999.0f;
        };

        dragLeft (*graph,
                  { xForFrequency (63.0f), yForGain (0.0f) },
                  { xForFrequency (850.0f), yForGain (6.0f) });

        const auto draggedFrequency = value ("postEqBand0Freq");
        const auto draggedGain = value ("postEqBand0Gain");

        if (std::abs (draggedFrequency - 850.0f) > 2.0f
            || std::abs (draggedGain - 6.0f) > 0.15f)
        {
            std::cerr << "FAIL: post EQ drag freq=" << draggedFrequency
                      << " gain=" << draggedGain << "\n";
            interactionOk = false;
        }

        const juce::Point<float> node { xForFrequency (draggedFrequency),
                                        yForGain (draggedGain) };
        wheelOver (*graph, node, 1.0f);
        const auto wheelQ = value ("postEqBand0Q");

        if (wheelQ <= 0.75f)
        {
            std::cerr << "FAIL: post EQ wheel did not change Q: " << wheelQ << "\n";
            interactionOk = false;
        }

        doubleClick (*graph, node);
        const auto resetGain = value ("postEqBand0Gain");

        if (std::abs (resetGain) > 0.05f)
        {
            std::cerr << "FAIL: post EQ double-click did not disable the band: "
                      << resetGain << "\n";
            interactionOk = false;
        }

        // A second double-click on the hollow node re-enables the band and
        // restores its previous gain.
        const juce::Point<float> disabledNode { xForFrequency (draggedFrequency),
                                                yForGain (0.0f) };
        doubleClick (*graph, disabledNode);
        const auto reEnabledGain = value ("postEqBand0Gain");

        if (std::abs (reEnabledGain - 6.0f) > 0.15f)
        {
            std::cerr << "FAIL: post EQ double-click did not re-enable the band: "
                      << reEnabledGain << "\n";
            interactionOk = false;
        }

        // The focus knobs retarget to the selected band. Drive them through
        // their sliders to confirm they write the matching parameters.
        if (auto* boost = dynamic_cast<juce::Slider*> (
                findChildByName (editor, "PostEqBoostSlider")))
        {
            boost->setValue (3.0, juce::sendNotificationSync);
            const auto knobGain = value ("postEqBand0Gain");

            if (std::abs (knobGain - 3.0f) > 0.15f)
            {
                std::cerr << "FAIL: post EQ boost knob wrote gain "
                          << knobGain << "\n";
                interactionOk = false;
            }
        }
        else
        {
            std::cerr << "FAIL: post EQ boost knob not found\n";
            interactionOk = false;
        }

        if (auto* q = dynamic_cast<juce::Slider*> (
                findChildByName (editor, "PostEqQSlider")))
        {
            q->setValue (2.0, juce::sendNotificationSync);
            const auto knobQ = value ("postEqBand0Q");

            if (std::abs (knobQ - 2.0f) > 0.02f)
            {
                std::cerr << "FAIL: post EQ Q knob wrote Q " << knobQ << "\n";
                interactionOk = false;
            }
        }
        else
        {
            std::cerr << "FAIL: post EQ Q knob not found\n";
            interactionOk = false;
        }

        // The curve-type switch retargets to the focused band as well.
        if (auto* selector = findChildByName (editor, "PostEqTypeSelector"))
        {
            if (! selector->isVisible() || selector->getWidth() < 40
                || selector->getHeight() < 20)
            {
                std::cerr << "FAIL: post EQ type selector has no usable bounds\n";
                interactionOk = false;
            }
        }
        else
        {
            std::cerr << "FAIL: post EQ type selector not found\n";
            interactionOk = false;
        }

        auto checkTypeButton = [&] (const char* name, float expected)
        {
            auto* button = dynamic_cast<juce::TextButton*> (
                findChildByName (editor, name));

            if (button == nullptr)
            {
                std::cerr << "FAIL: post EQ type button not found: " << name << "\n";
                interactionOk = false;
                return;
            }

            if (! button->isVisible() || button->getWidth() < 8)
            {
                std::cerr << "FAIL: post EQ type button not laid out: " << name << "\n";
                interactionOk = false;
                return;
            }

            button->setToggleState (true, juce::sendNotification);

            if (std::abs (value ("postEqBand0Type") - expected) > 0.01f)
            {
                std::cerr << "FAIL: post EQ type " << name << " wrote "
                          << value ("postEqBand0Type") << "\n";
                interactionOk = false;
            }
        };

        checkTypeButton ("PostEqTypeHigh", 1.0f);
        checkTypeButton ("PostEqTypeBand", 2.0f);
        checkTypeButton ("PostEqTypeLow", 0.0f);
    }
    else
    {
        std::cerr << "FAIL: post EQ graph not found\n";
        interactionOk = false;
    }

    if (! interactionOk)
        return 1;

    std::cout << "PASS\n";
    return 0;
}
