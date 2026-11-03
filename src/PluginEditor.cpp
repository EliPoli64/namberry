#include "PluginEditor.h"

#include <cmath>
#include <functional>
#include <utility>

namespace namberry {

namespace {

//==============================================================================
// Palette
constexpr juce::uint32 kBg        = 0xff161618;
constexpr juce::uint32 kPanel     = 0xff202024;
constexpr juce::uint32 kPanelLine = 0xff34343a;
constexpr juce::uint32 kAccent    = 0xff22d3ee;
constexpr juce::uint32 kText      = 0xffd4d4d8;
constexpr juce::uint32 kTextDim   = 0xff8b8b93;

using APVTS = juce::AudioProcessorValueTreeState;

//==============================================================================
/// A label plus either a rotary slider (>= 48 px hit area) or a 48x48 toggle,
/// bound directly to an APVTS parameter.
class ParamControl : public juce::Component
{
public:
    enum class Kind { Knob, Toggle };

    ParamControl (APVTS& state, const juce::String& paramId,
                  const juce::String& text, Kind kind = Kind::Knob)
    {
        addAndMakeVisible (label_);
        label_.setText (text, juce::dontSendNotification);
        label_.setJustificationType (juce::Justification::centred);
        label_.setFont (juce::FontOptions (11.0f));
        label_.setColour (juce::Label::textColourId, juce::Colour (kText));

        auto* param = state.getParameter (paramId);

        if (kind == Kind::Toggle
            || dynamic_cast<juce::AudioParameterBool*> (param) != nullptr)
        {
            toggle_.setButtonText ({});
            addAndMakeVisible (toggle_);
            buttonAttachment_ = std::make_unique<APVTS::ButtonAttachment> (
                state, paramId, toggle_);
        }
        else
        {
            slider_.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider_.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 15);
            slider_.setColour (juce::Slider::textBoxTextColourId, juce::Colour (kTextDim));
            slider_.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            slider_.setDoubleClickReturnValue (true,
                param != nullptr ? param->convertFrom0to1 (param->getDefaultValue()) : 0.0);
            addAndMakeVisible (slider_);
            sliderAttachment_ = std::make_unique<APVTS::SliderAttachment> (
                state, paramId, slider_);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label_.setBounds (r.removeFromTop (14));

        if (sliderAttachment_ != nullptr)
        {
            slider_.setBounds (r.reduced (1));
        }
        else
        {
            toggle_.setBounds (r.withSizeKeepingCentre (48, 48));
        }
    }

    static constexpr int cellWidth  = 74;
    static constexpr int cellHeight = 88;

private:
    juce::Label label_;
    juce::Slider slider_;
    juce::ToggleButton toggle_;
    std::unique_ptr<APVTS::SliderAttachment> sliderAttachment_;
    std::unique_ptr<APVTS::ButtonAttachment> buttonAttachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParamControl)
};

//==============================================================================
/// A rounded section of the signal chain, titled and laid out left to right.
/// The border glow fades in on hover; the fade is disabled when the
/// "Reduced Motion" preference is enabled.
class SectionPanel : public juce::Component,
                     private juce::Timer
{
public:
    SectionPanel (juce::String title, int columns, int fixedWidth,
                  std::function<bool()> reducedMotion)
        : title_ (std::move (title)), columns_ (columns),
          fixedWidth_ (fixedWidth), reducedMotion_ (std::move (reducedMotion))
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
        startTimerHz (30);
    }

    void addItem (juce::Component* item)
    {
        items_.push_back (item);
        addAndMakeVisible (item);
    }

    void addRow (juce::Component* row, int height)
    {
        rows_.push_back ({ row, height });
        addAndMakeVisible (row);
    }

    int preferredWidth() const
    {
        return fixedWidth_ > 0 ? fixedWidth_
                               : pad_ * 2 + columns_ * ParamControl::cellWidth;
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (3.0f);

        g.setColour (juce::Colour (kPanel));
        g.fillRoundedRectangle (bounds, 8.0f);

        const auto line = juce::Colour (kPanelLine).interpolatedWith (
            juce::Colour (kAccent), highlight_);
        g.setColour (line);
        g.drawRoundedRectangle (bounds, 8.0f, 1.0f + highlight_);

        g.setColour (juce::Colour (kAccent).withAlpha (0.75f + 0.25f * highlight_));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (title_,
                    getLocalBounds().reduced (pad_).removeFromTop (titleHeight_),
                    juce::Justification::centredLeft);
    }

    void mouseEnter (const juce::MouseEvent&) override { hovered_ = true; }
    void mouseExit  (const juce::MouseEvent&) override { hovered_ = false; }

    void resized() override
    {
        auto r = getLocalBounds().reduced (pad_);
        r.removeFromTop (titleHeight_);

        const int count = static_cast<int> (items_.size());
        const int rows = (count + columns_ - 1) / columns_;

        for (int row = 0; row < rows; ++row)
        {
            for (int col = 0; col < columns_; ++col)
            {
                const int index = row * columns_ + col;
                if (index >= count)
                    break;

                items_[(size_t) index]->setBounds (
                    r.getX() + col * ParamControl::cellWidth,
                    r.getY() + row * ParamControl::cellHeight,
                    ParamControl::cellWidth,
                    ParamControl::cellHeight);
            }
        }

        int y = r.getY() + rows * ParamControl::cellHeight + 4;
        for (auto& row : rows_)
        {
            row.first->setBounds (r.getX(), y, r.getWidth(), row.second);
            y += row.second + 6;
        }
    }

private:
    void timerCallback() override
    {
        const float target = hovered_ ? 1.0f : 0.0f;
        const bool instant = reducedMotion_ != nullptr && reducedMotion_();

        float next = highlight_;
        if (instant)
            next = target;
        else
            next += (target - highlight_) * 0.35f;

        if (std::abs (next - highlight_) > 0.002f)
        {
            highlight_ = next;
            repaint();
        }
        else if (highlight_ != target)
        {
            highlight_ = target;
            repaint();
        }
    }

    static constexpr int pad_ = 8;
    static constexpr int titleHeight_ = 22;

    juce::String title_;
    int columns_;
    int fixedWidth_;
    std::function<bool()> reducedMotion_;
    std::vector<juce::Component*> items_;
    std::vector<std::pair<juce::Component*, int>> rows_;
    float highlight_ = 0.0f;
    bool hovered_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SectionPanel)
};

//==============================================================================
/// Drag-and-drop target + file chooser button for a .nam model.
class NamLoadBox : public juce::Component,
                   public juce::FileDragAndDropTarget
{
public:
    NamLoadBox (juce::String caption, std::function<bool (const juce::File&)> loader)
        : caption_ (std::move (caption)), loader_ (std::move (loader))
    {
        addAndMakeVisible (name_);
        name_.setText ("No model loaded", juce::dontSendNotification);
        name_.setJustificationType (juce::Justification::centred);
        name_.setFont (juce::FontOptions (11.0f));
        name_.setColour (juce::Label::textColourId, juce::Colour (kTextDim));

        addAndMakeVisible (loadButton_);
        loadButton_.setButtonText ("LOAD .NAM");
        loadButton_.onClick = [this] { chooseFile(); };
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (const auto& f : files)
            if (f.endsWithIgnoreCase (".nam"))
                return true;
        return false;
    }

    void fileDragEnter (const juce::StringArray&, int, int) override
    {
        dragOver_ = true;
        repaint();
    }

    void fileDragExit (const juce::StringArray&) override
    {
        dragOver_ = false;
        repaint();
    }

    void filesDropped (const juce::StringArray& files, int, int) override
    {
        dragOver_ = false;
        repaint();

        if (! files.isEmpty())
            load (juce::File (files[0]));
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);

        g.setColour (juce::Colour (dragOver_ ? 0xff27272a : 0xff1a1a1d));
        g.fillRoundedRectangle (bounds, 6.0f);

        g.setColour (juce::Colour (dragOver_ ? kAccent : kPanelLine));
        g.drawRoundedRectangle (bounds, 6.0f, dragOver_ ? 2.0f : 1.0f);

        g.setColour (juce::Colour (kAccent));
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText (caption_, bounds.toNearestInt().removeFromTop (13),
                    juce::Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4);
        r.removeFromTop (12);
        name_.setBounds (r.removeFromTop (16));
        loadButton_.setBounds (r.reduced (2));
    }

private:
    void chooseFile()
    {
        chooser_ = std::make_unique<juce::FileChooser> (
            "Load NAM model", juce::File(), "*.nam");

        chooser_->launchAsync (juce::FileBrowserComponent::openMode
                                 | juce::FileBrowserComponent::canSelectFiles,
                               [this] (const juce::FileChooser& fc)
                               {
                                   const auto file = fc.getResult();
                                   if (file.existsAsFile())
                                       load (file);
                               });
    }

    void load (const juce::File& file)
    {
        if (loader_ != nullptr && loader_ (file))
        {
            name_.setText (file.getFileName(), juce::dontSendNotification);
            name_.setColour (juce::Label::textColourId, juce::Colour (kText));
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Could not load model",
                "NAMberry could not load: " + file.getFileName());
        }
    }

    juce::String caption_;
    std::function<bool (const juce::File&)> loader_;
    juce::Label name_;
    juce::TextButton loadButton_;
    std::unique_ptr<juce::FileChooser> chooser_;
    bool dragOver_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamLoadBox)
};

//==============================================================================
/// One of the four IR slots: load button, mix, pan and phase invert.
class IrSlotRow : public juce::Component,
                  public juce::FileDragAndDropTarget
{
public:
    IrSlotRow (APVTS& state, int slot, NAMBerryProcessor& processor)
        : slot_ (slot), processor_ (processor)
    {
        addAndMakeVisible (title_);
        title_.setText ("IR " + juce::String (slot + 1), juce::dontSendNotification);
        title_.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        title_.setColour (juce::Label::textColourId, juce::Colour (kText));

        addAndMakeVisible (loadButton_);
        loadButton_.setButtonText ("Load IR");
        loadButton_.onClick = [this] { chooseFile(); };

        addAndMakeVisible (fileName_);
        fileName_.setText ("empty", juce::dontSendNotification);
        fileName_.setFont (juce::FontOptions (10.0f));
        fileName_.setColour (juce::Label::textColourId, juce::Colour (kTextDim));

        buildKnob (state, "irMix" + juce::String (slot), "Mix", mix_, mixLabel_);
        buildKnob (state, "irPan" + juce::String (slot), "Pan", pan_, panLabel_);

        addAndMakeVisible (phase_);
        phase_.setButtonText ("\xc3\x98"); // Ø
        addAndMakeVisible (phaseLabel_);
        phaseLabel_.setText ("Phase", juce::dontSendNotification);
        phaseLabel_.setJustificationType (juce::Justification::centred);
        phaseLabel_.setFont (juce::FontOptions (10.0f));
        phaseLabel_.setColour (juce::Label::textColourId, juce::Colour (kText));
        phaseAttachment_ = std::make_unique<APVTS::ButtonAttachment> (
            state, "irPhase" + juce::String (slot), phase_);
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (const auto& f : files)
            if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif")
                || f.endsWithIgnoreCase (".aiff"))
                return true;
        return false;
    }

    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver_ = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver_ = false; repaint(); }

    void filesDropped (const juce::StringArray& files, int, int) override
    {
        dragOver_ = false;
        repaint();
        if (! files.isEmpty())
            load (juce::File (files[0]));
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (dragOver_ ? 0xff27272a : 0xff1a1a1d));
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (juce::Colour (dragOver_ ? kAccent : kPanelLine));
        g.drawRoundedRectangle (b, 6.0f, dragOver_ ? 2.0f : 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);

        auto top = r.removeFromTop (18);
        title_.setBounds (top.removeFromLeft (46));
        fileName_.setBounds (top);

        auto left = r.removeFromLeft (86);
        loadButton_.setBounds (left.removeFromTop (56).reduced (0, 4));

        auto phaseArea = r.removeFromRight (54);
        phase_.setBounds (phaseArea.removeFromTop (48).withSizeKeepingCentre (48, 48));
        phaseLabel_.setBounds (phaseArea.removeFromTop (14));

        auto knobArea = r;
        const int knobW = knobArea.getWidth() / 2;

        auto mixArea = knobArea.removeFromLeft (knobW);
        mixLabel_.setBounds (mixArea.removeFromBottom (14));
        mix_.setBounds (mixArea);

        panLabel_.setBounds (knobArea.removeFromBottom (14));
        pan_.setBounds (knobArea);
    }

private:
    void buildKnob (APVTS& state, const juce::String& id,
                    const juce::String& text, juce::Slider& slider,
                    juce::Label& label)
    {
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (10.0f));
        label.setColour (juce::Label::textColourId, juce::Colour (kText));
        addAndMakeVisible (label);

        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 50, 14);
        slider.setColour (juce::Slider::textBoxTextColourId, juce::Colour (kTextDim));
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (slider);
        mixAttachments_.push_back (std::make_unique<APVTS::SliderAttachment> (state, id, slider));
    }

    void chooseFile()
    {
        chooser_ = std::make_unique<juce::FileChooser> (
            "Load impulse response", juce::File(), "*.wav;*.aif;*.aiff");

        chooser_->launchAsync (juce::FileBrowserComponent::openMode
                                 | juce::FileBrowserComponent::canSelectFiles,
                               [this] (const juce::FileChooser& fc)
                               {
                                   const auto file = fc.getResult();
                                   if (file.existsAsFile())
                                       load (file);
                               });
    }

    void load (const juce::File& file)
    {
        if (processor_.loadIr (slot_, file))
        {
            fileName_.setText (file.getFileName(), juce::dontSendNotification);
            fileName_.setColour (juce::Label::textColourId, juce::Colour (kText));
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Could not load IR",
                "NAMberry could not load: " + file.getFileName());
        }
    }

    int slot_;
    NAMBerryProcessor& processor_;
    juce::Label title_, fileName_, mixLabel_, panLabel_, phaseLabel_;
    juce::TextButton loadButton_;
    juce::ToggleButton phase_;
    juce::Slider mix_, pan_;
    std::vector<std::unique_ptr<APVTS::SliderAttachment>> mixAttachments_;
    std::unique_ptr<APVTS::ButtonAttachment> phaseAttachment_;
    std::unique_ptr<juce::FileChooser> chooser_;
    bool dragOver_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IrSlotRow)
};

} // namespace

//==============================================================================
NAMBerryEditor::NAMBerryEditor (NAMBerryProcessor& p)
    : AudioProcessorEditor (&p), processor_ (p)
{
    // --- Theme -------------------------------------------------------------
    auto scheme = juce::LookAndFeel_V4::getDarkColourScheme();
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::windowBackground,
                        juce::Colour (kBg));
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::widgetBackground,
                        juce::Colour (kPanel));
    lnf_.setColourScheme (scheme);
    lnf_.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (kAccent));
    lnf_.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (kPanelLine));
    lnf_.setColour (juce::Slider::thumbColourId, juce::Colour (kAccent));
    lnf_.setColour (juce::Label::textColourId, juce::Colour (kText));
    lnf_.setColour (juce::ToggleButton::textColourId, juce::Colour (kText));
    lnf_.setColour (juce::ToggleButton::tickColourId, juce::Colour (kAccent));
    lnf_.setColour (juce::ToggleButton::tickDisabledColourId, juce::Colour (kPanelLine));
    lnf_.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff27272a));
    lnf_.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff0e7490));
    lnf_.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe4e4e7));
    lnf_.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setLookAndFeel (&lnf_);

    // --- Header controls ---------------------------------------------------
    addAndMakeVisible (reducedMotionButton_);
    reducedMotionButton_.setButtonText ("Reduced Motion");
    reducedMotionAttachment_ = std::make_unique<APVTS::ButtonAttachment> (
        processor_.getValueTreeState(), "reducedMotion", reducedMotionButton_);

    // --- Signal chain ------------------------------------------------------
    viewport_.setViewedComponent (&content_, false);
    viewport_.setScrollBarsShown (false, true);
    viewport_.setColour (juce::ScrollBar::thumbColourId, juce::Colour (kAccent).withAlpha (0.6f));
    viewport_.setColour (juce::ScrollBar::trackColourId, juce::Colour (kPanel));
    addAndMakeVisible (viewport_);

    buildUi();

    setResizable (true, true);
    setResizeLimits (900, 560, 4000, 900);
    setSize (1240, 640);

    startTimerHz (10);
}

NAMBerryEditor::~NAMBerryEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void NAMBerryEditor::buildUi()
{
    auto& state = processor_.getValueTreeState();
    auto reduced = [this]() -> bool
    {
        auto* value = processor_.getValueTreeState()
                          .getRawParameterValue ("reducedMotion");
        return value != nullptr && value->load() > 0.5f;
    };

    auto makePanel = [this, &reduced] (const juce::String& title, int cols,
                                       int width = 0) -> SectionPanel*
    {
        auto panel = std::make_unique<SectionPanel> (title, cols, width, reduced);
        auto* raw = panel.get();
        ownedControls_.push_back (std::move (panel));
        content_.addAndMakeVisible (raw);
        return raw;
    };

    auto makeControl = [this, &state] (const juce::String& id,
                                       const juce::String& text,
                                       ParamControl::Kind kind = ParamControl::Kind::Knob)
    {
        auto control = std::make_unique<ParamControl> (state, id, text, kind);
        auto* raw = control.get();
        ownedControls_.push_back (std::move (control));
        return raw;
    };

    auto makeNamBox = [this] (const juce::String& caption, bool pedal)
    {
        auto box = std::make_unique<NamLoadBox> (
            caption,
            [this, pedal] (const juce::File& file)
            {
                return pedal ? processor_.loadNamPedalModel (file)
                             : processor_.loadNamModel (file);
            });
        auto* raw = box.get();
        ownedControls_.push_back (std::move (box));
        return raw;
    };

    // 1. Tuner --------------------------------------------------------------
    if (auto* tuner = makePanel ("TUNER", 1))
    {
        noteLabel_.setText ("---", juce::dontSendNotification);
        noteLabel_.setJustificationType (juce::Justification::centred);
        noteLabel_.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        noteLabel_.setColour (juce::Label::textColourId, juce::Colour (kAccent));
        tuner->addRow (&noteLabel_, 34);
        tuner->addItem (makeControl ("tuningOffset", "Offset"));
        tuner->addItem (makeControl ("tuningBypass", "Bypass", ParamControl::Kind::Toggle));
    }

    // 2. Noise gate ---------------------------------------------------------
    if (auto* gate = makePanel ("GATE", 2))
    {
        gate->addItem (makeControl ("gateThreshold", "Threshold"));
        gate->addItem (makeControl ("gateAttack", "Attack"));
        gate->addItem (makeControl ("gateRelease", "Release"));
        gate->addItem (makeControl ("gateBypass", "Bypass", ParamControl::Kind::Toggle));
    }

    // 3. Compressor ---------------------------------------------------------
    if (auto* comp = makePanel ("COMP", 2))
    {
        comp->addItem (makeControl ("compThreshold", "Threshold"));
        comp->addItem (makeControl ("compRatio", "Ratio"));
        comp->addItem (makeControl ("compAttack", "Attack"));
        comp->addItem (makeControl ("compRelease", "Release"));
        comp->addItem (makeControl ("compMakeup", "Makeup"));
        comp->addItem (makeControl ("compTight", "Tight", ParamControl::Kind::Toggle));
    }

    // 4. Pre-amp EQ ---------------------------------------------------------
    if (auto* preEq = makePanel ("PRE-EQ", 3))
    {
        const char* bandNames[] = { "Low", "Mid", "High" };
        for (int band = 0; band < 3; ++band)
        {
            const auto i = juce::String (band);
            preEq->addItem (makeControl ("preEqFreq" + i, juce::String (bandNames[band]) + " Hz"));
            preEq->addItem (makeControl ("preEqGain" + i, juce::String (bandNames[band]) + " dB"));
            preEq->addItem (makeControl ("preEqQ" + i, juce::String (bandNames[band]) + " Q"));
        }
    }

    // 5. Digital capo -------------------------------------------------------
    if (auto* capo = makePanel ("CAPO", 2))
    {
        capo->addItem (makeControl ("capoFret", "Fret"));
        capo->addItem (makeControl ("capoMix", "Mix"));
    }

    // 6. NAM amp ------------------------------------------------------------
    if (auto* amp = makePanel ("NAM AMP", 2))
    {
        amp->addItem (makeControl ("ampGain", "Gain"));
        amp->addItem (makeControl ("ampPresence", "Presence"));
        amp->addItem (makeControl ("ampDepth", "Depth"));
        amp->addItem (makeControl ("ampOutput", "Output"));
        amp->addRow (makeNamBox ("AMP MODEL", false), 92);
    }

    // 7. NAM pedal ----------------------------------------------------------
    if (auto* pedal = makePanel ("NAM PEDAL", 2))
    {
        pedal->addItem (makeControl ("pedalGain", "Gain"));
        pedal->addItem (makeControl ("pedalOutput", "Output"));
        pedal->addItem (makeControl ("pedalBypass", "Bypass", ParamControl::Kind::Toggle));
        pedal->addRow (makeNamBox ("PEDAL MODEL", true), 92);
    }

    // 8. IR cabinet ---------------------------------------------------------
    if (auto* ir = makePanel ("IR CAB", 1, 340))
    {
        for (int slot = 0; slot < 4; ++slot)
        {
            auto row = std::make_unique<IrSlotRow> (state, slot, processor_);
            auto* raw = row.get();
            ownedControls_.push_back (std::move (row));
            ir->addRow (raw, 96);
        }
    }

    // 9. Post-amp EQ --------------------------------------------------------
    if (auto* postEq = makePanel ("POST-EQ", 3))
    {
        const char* bandNames[] = { "Low", "Mid", "High" };
        for (int band = 0; band < 3; ++band)
        {
            const auto i = juce::String (band);
            postEq->addItem (makeControl ("postEqFreq" + i, juce::String (bandNames[band]) + " Hz"));
            postEq->addItem (makeControl ("postEqGain" + i, juce::String (bandNames[band]) + " dB"));
            postEq->addItem (makeControl ("postEqQ" + i, juce::String (bandNames[band]) + " Q"));
        }
    }

    // 10. Output ------------------------------------------------------------
    if (auto* output = makePanel ("OUTPUT", 1))
        output->addItem (makeControl ("masterOutput", "Master"));
}

//==============================================================================
void NAMBerryEditor::layoutContent()
{
    constexpr int gap = 8;
    constexpr int margin = 8;

    int x = margin;

    const int contentHeight = juce::jmax (420, viewport_.getHeight() - 2 * margin);

    for (auto& control : ownedControls_)
    {
        if (auto* panel = dynamic_cast<SectionPanel*> (control.get()))
        {
            panel->setBounds (x, margin, panel->preferredWidth(), contentHeight);
            x += panel->preferredWidth() + gap;
        }
    }

    content_.setSize (x + margin, contentHeight + 2 * margin);
}

void NAMBerryEditor::resized()
{
    auto r = getLocalBounds();

    // Header controls (48 px tall target).
    auto header = r.removeFromTop (60);
    reducedMotionButton_.setBounds (header.removeFromRight (190).withSizeKeepingCentre (180, 48));

    viewport_.setBounds (r.reduced (4, 2));
    layoutContent();
}

void NAMBerryEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (kBg));

    auto header = getLocalBounds().removeFromTop (60);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("NAMBERRY", header.reduced (16, 0),
                juce::Justification::centredLeft);

    g.setColour (juce::Colour (kAccent));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawFittedText ("TUNER  \xe2\x86\x92  GATE  \xe2\x86\x92  COMP  \xe2\x86\x92  PRE-EQ  \xe2\x86\x92  CAPO  \xe2\x86\x92  AMP  \xe2\x86\x92  PEDAL  \xe2\x86\x92  IR  \xe2\x86\x92  POST-EQ  \xe2\x86\x92  OUT",
                      header.reduced (180, 0),
                      juce::Justification::centred, 1);

    g.setColour (juce::Colour (kPanelLine));
    g.fillRect (getLocalBounds().removeFromTop (60).removeFromBottom (1));
}

void NAMBerryEditor::timerCallback()
{
    if (auto* tuner = processor_.getDropTuner())
    {
        const auto note = tuner->getDetectedNote();
        if (note.isNotEmpty() && note != noteLabel_.getText())
            noteLabel_.setText (note, juce::dontSendNotification);
    }
}

} // namespace namberry
