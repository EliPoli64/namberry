#include "PluginEditor.h"

namespace namberry {

//==============================================================================
NAMBerryEditor::NAMBerryEditor (NAMBerryProcessor& p)
    : AudioProcessorEditor (&p), processor_ (p)
{
    // Set a default window size; will be refined in Phase 5 UI work
    setSize (900, 600);
}

NAMBerryEditor::~NAMBerryEditor() = default;

//==============================================================================
void NAMBerryEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1e));

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawFittedText ("NAMBerry", getLocalBounds().removeFromTop(50), juce::Justification::centred, 1);

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (14.0f));
    g.drawFittedText ("Signal Chain: Tuner → Comp → Pre-EQ → Capo → NAM → Post-EQ → IR",
                      getLocalBounds().removeFromTop(90).removeFromBottom(30),
                      juce::Justification::centred, 1);
}

void NAMBerryEditor::resized()
{
    // Layout will be implemented in Phase 5 (full UI)
}

} // namespace namberry
