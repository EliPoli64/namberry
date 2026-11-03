#include "PluginEditor.h"
#include "namberry/DirectoryBrowser.h"

#include <cmath>
#include <functional>
#include <utility>

namespace namberry {

namespace {

//==============================================================================
// Palette
constexpr juce::uint32 kBg        = 0xff111113;
constexpr juce::uint32 kPanel     = 0xff202024;
constexpr juce::uint32 kPanelLine = 0xff34343a;
constexpr juce::uint32 kAccent    = 0xff22d3ee;
constexpr juce::uint32 kText      = 0xffd4d4d8;
constexpr juce::uint32 kTextDim   = 0xff8b8b93;

using APVTS = juce::AudioProcessorValueTreeState;

enum class UnitStyle { Pedal, AmpHead, RackUnit };

//==============================================================================
void drawScrew (juce::Graphics& g, float cx, float cy, float r)
{
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (cx - r + 1.0f, cy - r + 1.0f, 2.0f * r, 2.0f * r);

    g.setGradientFill (juce::ColourGradient (
        juce::Colour (0xffd8d8de), cx - r, cy - r,
        juce::Colour (0xff5a5a62), cx + r, cy + r, false));
    g.fillEllipse (cx - r, cy - r, 2.0f * r, 2.0f * r);

    g.setColour (juce::Colour (0xff1b1b1e));
    g.drawLine (cx - r * 0.55f, cy, cx + r * 0.55f, cy, 1.0f);
}

void drawBrushedPanel (juce::Graphics& g, juce::Rectangle<float> b,
                       juce::Colour top, juce::Colour bottom, float radius)
{
    g.setGradientFill (juce::ColourGradient (top, b.getX(), b.getY(),
                                             bottom, b.getX(), b.getBottom(), false));
    g.fillRoundedRectangle (b, radius);

    g.setColour (juce::Colours::white.withAlpha (0.03f));
    for (float y = b.getY() + 2.0f; y < b.getBottom(); y += 3.0f)
        g.drawHorizontalLine ((int) y, b.getX() + 2.0f, b.getRight() - 2.0f);
}

juce::Array<juce::File> listMatchingFiles (const juce::File& directory,
                                           const juce::StringArray& extensions)
{
    juce::Array<juce::File> files;

    if (! directory.isDirectory())
        return files;

    for (auto entry : juce::RangedDirectoryIterator (
             directory, false, "*", juce::File::findFiles))
    {
        const auto file = entry.getFile();

        for (const auto& extension : extensions)
        {
            if (file.hasFileExtension (extension))
            {
                files.add (file);
                break;
            }
        }
    }

    files.sort();
    return files;
}

//==============================================================================
/// Metal knobs, toggle switches and brushed buttons for the whole editor.
class HardwareLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HardwareLookAndFeel()
    {
        auto scheme = getDarkColourScheme();
        scheme.setUIColour (ColourScheme::windowBackground, juce::Colour (kBg));
        scheme.setUIColour (ColourScheme::widgetBackground, juce::Colour (kPanel));
        setColourScheme (scheme);

        setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (kAccent));
        setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (kPanelLine));
        setColour (juce::Slider::thumbColourId, juce::Colour (kAccent));
        setColour (juce::Label::textColourId, juce::Colour (kText));
        setColour (juce::ToggleButton::textColourId, juce::Colour (kText));
        setColour (juce::TextButton::buttonColourId, juce::Colour (0xff35353b));
        setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff0e7490));
        setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe4e4e7));
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::TabbedButtonBar::tabOutlineColourId, juce::Colour (0xff2a2a2f));
        setColour (juce::TabbedButtonBar::frontTextColourId, juce::Colours::white);
        setColour (juce::TabbedButtonBar::tabTextColourId, juce::Colour (kTextDim));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider& slider) override
    {
        const auto centre = juce::Point<float> (x + width * 0.5f, y + height * 0.5f);
        const float radius = juce::jmin (width, height) * 0.5f - 4.0f;
        if (radius < 5.0f)
            return;

        const float angle = rotaryStartAngle
                          + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // Tick ring.
        g.setColour (juce::Colour (0xff5a5a61));
        for (int i = 0; i <= 10; ++i)
        {
            const float a = rotaryStartAngle
                          + (rotaryEndAngle - rotaryStartAngle) * (float) i / 10.0f;
            const auto dir = juce::Point<float> (std::sin (a), -std::cos (a));
            g.drawLine ({ centre + dir * (radius + 1.0f),
                          centre + dir * (radius + 4.0f) }, 1.0f);
        }

        // Outer skirt.
        auto skirt = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f)
                         .withCentre (centre);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (skirt.translated (0.0f, 2.0f));
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xff4a4a52), centre.x, skirt.getY(),
            juce::Colour (0xff18181b), centre.x, skirt.getBottom(), false));
        g.fillEllipse (skirt);
        g.setColour (juce::Colour (0xff0b0b0d));
        g.drawEllipse (skirt, 1.3f);

        // Cap.
        auto cap = skirt.reduced (radius * 0.17f);
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xff5f5f68), cap.getX(), cap.getY(),
            juce::Colour (0xff242428), cap.getX(), cap.getBottom(), false));
        g.fillEllipse (cap);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawEllipse (cap.reduced (1.0f), 1.0f);

        // Pointer.
        const auto dir = juce::Point<float> (std::sin (angle), -std::cos (angle));
        const auto p1 = centre + dir * (radius * 0.20f);
        const auto p2 = centre + dir * (radius * 0.86f);
        g.setColour (juce::Colour (0xfff4f4f5));
        g.drawLine ({ p1, p2 }, 2.6f);

        if (slider.hasKeyboardFocus (true))
        {
            g.setColour (juce::Colour (kAccent).withAlpha (0.8f));
            g.drawEllipse (skirt.expanded (2.5f), 1.5f);
        }
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool isMouseOver, bool) override
    {
        auto bounds = button.getLocalBounds().toFloat();
        const auto fullBounds = bounds;
        const auto text = button.getButtonText();
        const bool on = button.getToggleState();

        auto switchArea = text.isEmpty()
                            ? bounds
                            : bounds.removeFromLeft (36.0f);

        auto base = juce::Rectangle<float> (32.0f, 15.0f)
                        .withCentre (switchArea.getCentre());

        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xff26262a), base.getX(), base.getY(),
            juce::Colour (0xff0e0e10), base.getX(), base.getBottom(), false));
        g.fillRoundedRectangle (base, 5.0f);
        g.setColour (isMouseOver ? juce::Colour (0xff5a5a62) : juce::Colour (0xff0b0b0d));
        g.drawRoundedRectangle (base, 5.0f, 1.0f);

        const auto leverCentre = base.getCentre()
                               + juce::Point<float> (on ? 6.5f : -6.5f, 0.0f);
        g.setGradientFill (juce::ColourGradient (
            on ? juce::Colour (0xffe8e8ee) : juce::Colour (0xff9a9aa2),
            leverCentre.x, leverCentre.y - 9.0f,
            on ? juce::Colour (0xff7b7b84) : juce::Colour (0xff4f4f57),
            leverCentre.x, leverCentre.y + 9.0f, false));
        g.fillRoundedRectangle (juce::Rectangle<float> (6.0f, 19.0f)
                                    .withCentre (leverCentre), 2.8f);

        const auto led = juce::Rectangle<float> (8.0f, 8.0f)
                             .withCentre (switchArea.getCentre()
                                          + juce::Point<float> (-21.0f, 0.0f));
        g.setColour (on ? juce::Colour (0xff4ade80) : juce::Colour (0xff23302a));
        g.fillEllipse (led);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (led, 1.0f);

        if (text.isNotEmpty())
        {
            g.setColour (juce::Colour (kText));
            g.setFont (juce::FontOptions (12.0f));
            g.drawFittedText (text, bounds.toNearestInt(),
                              juce::Justification::centredLeft, 1);
        }

        if (button.hasKeyboardFocus (true))
        {
            g.setColour (juce::Colour (kAccent).withAlpha (0.8f));
            g.drawRoundedRectangle (fullBounds.reduced (1.0f), 5.0f, 1.5f);
        }
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool isMouseOver, bool isButtonDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
        auto base = backgroundColour;

        if (isButtonDown)      base = base.darker (0.25f);
        else if (isMouseOver)  base = base.brighter (0.10f);

        g.setGradientFill (juce::ColourGradient (
            base.brighter (0.18f), bounds.getX(), bounds.getY(),
            base.darker (0.22f), bounds.getX(), bounds.getBottom(), false));
        g.fillRoundedRectangle (bounds, 6.0f);

        g.setColour (juce::Colour (0xff0c0c0e));
        g.drawRoundedRectangle (bounds, 6.0f, 1.2f);

        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawHorizontalLine ((int) bounds.getY() + 2,
                              bounds.getX() + 6.0f, bounds.getRight() - 6.0f);
    }

    juce::Font getLabelFont (juce::Label& label) override
    {
        // Bigger numeric readouts for the rack controls.
        return juce::FontOptions (juce::jlimit (12.0f, 17.0f,
                                                (float) label.getHeight() * 0.85f));
    }

    void drawTabButton (juce::TabBarButton& button, juce::Graphics& g,
                        bool isMouseOver, bool isMouseDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (2.0f);
        const bool front = button.isFrontTab();

        auto body = front ? juce::Colour (0xff3c3c43) : juce::Colour (0xff202024);
        if (isMouseOver) body = body.brighter (0.10f);
        if (isMouseDown) body = body.darker (0.12f);

        g.setGradientFill (juce::ColourGradient (
            body.brighter (0.12f), bounds.getX(), bounds.getY(),
            body.darker (0.18f), bounds.getX(), bounds.getBottom(), false));
        g.fillRoundedRectangle (bounds, 5.0f);

        g.setColour (front ? juce::Colour (kAccent) : juce::Colour (0xff2a2a2f));
        g.drawRoundedRectangle (bounds, 5.0f, front ? 1.6f : 1.0f);

        g.setColour (front ? juce::Colours::white : juce::Colour (kTextDim));
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawFittedText (button.getButtonText(), bounds.toNearestInt(),
                          juce::Justification::centred, 1);
    }
};

//==============================================================================
/// Binds a button to a bool parameter with inverted logic, so the control shows
/// "enabled" while the parameter stores "bypassed".
class InvertedButtonAttachment : private juce::AudioProcessorValueTreeState::Listener,
                                 private juce::AsyncUpdater
{
public:
    InvertedButtonAttachment (APVTS& state, const juce::String& paramId,
                              juce::Button& button)
        : state_ (state), paramId_ (paramId), button_ (button)
    {
        button_.setClickingTogglesState (true);
        button_.onClick = [this] { buttonClicked(); };
        state_.addParameterListener (paramId_, this);
        updateFromParameter();
    }

    ~InvertedButtonAttachment() override
    {
        state_.removeParameterListener (paramId_, this);
        button_.onClick = nullptr;
    }

private:
    void parameterChanged (const juce::String&, float) override
    {
        triggerAsyncUpdate();
    }

    void handleAsyncUpdate() override
    {
        updateFromParameter();
    }

    void updateFromParameter()
    {
        const auto* value = state_.getRawParameterValue (paramId_);
        const bool bypassed = value != nullptr && value->load() > 0.5f;
        button_.setToggleState (! bypassed, juce::dontSendNotification);
    }

    void buttonClicked()
    {
        if (auto* param = state_.getParameter (paramId_))
            param->setValueNotifyingHost (button_.getToggleState() ? 0.0f : 1.0f);
    }

    APVTS& state_;
    juce::String paramId_;
    juce::Button& button_;
};

//==============================================================================
/// Strobe-style tuning wheel. The strobe bands rotate with the cents error;
/// when the note is in tune they line up with the fixed top pointer.
class TuningWheel : public juce::Component
{
public:
    void setState (float cents, const juce::String& note, float referenceHz)
    {
        if (! std::isfinite (cents))
            cents = 0.0f;

        targetCents_ = juce::jlimit (-100.0f, 100.0f, cents);
        note_ = note;
        referenceHz_ = referenceHz;
        hasNote_ = note.isNotEmpty() && note != "---";

        if (! hasNote_)
            targetCents_ = 0.0f;

        displayCents_ += (targetCents_ - displayCents_) * 0.35f;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
        auto circle = juce::Rectangle<float> (size, size).withCentre (bounds.getCentre());
        const float radius = size * 0.5f - 2.0f;

        if (radius < 10.0f)
            return;

        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (circle.translated (0.0f, 3.0f));

        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xff26262c), circle.getCentreX(), circle.getY(),
            juce::Colour (0xff0b0b0d), circle.getCentreX(), circle.getBottom(), false));
        g.fillEllipse (circle);

        g.setColour (juce::Colour (0xff34343a));
        g.drawEllipse (circle, 1.5f);

        // Tick ring.
        for (int i = 0; i < 36; ++i)
        {
            const float angle = (float) i / 36.0f * juce::MathConstants<float>::twoPi;
            const auto dir = juce::Point<float> (std::sin (angle), -std::cos (angle));
            const float inner = radius - ((i % 3 == 0) ? 12.0f : 7.0f);

            g.setColour (i % 3 == 0 ? juce::Colour (0xff71717a)
                                    : juce::Colour (0xff3f3f46));
            g.drawLine ({ circle.getCentre() + dir * inner,
                          circle.getCentre() + dir * (radius - 2.0f) }, 1.0f);
        }

        const auto tuning = tuningColour();
        const float rotation = displayCents_ * 6.0f
                             * juce::MathConstants<float>::pi / 180.0f;

        // Rotating strobe bands.
        for (int i = 0; i < 12; ++i)
        {
            const float angle = rotation + (float) i / 12.0f
                              * juce::MathConstants<float>::twoPi;
            const auto dir = juce::Point<float> (std::sin (angle), -std::cos (angle));

            g.setColour (tuning.withAlpha (hasNote_ ? 0.9f : 0.2f));
            g.drawLine ({ circle.getCentre() + dir * (radius * 0.54f),
                          circle.getCentre() + dir * (radius * 0.86f) }, 3.0f);
        }

        // Fixed top pointer.
        juce::Path pointer;
        pointer.addTriangle (circle.getCentreX() - 6.0f, circle.getY() + 2.0f,
                             circle.getCentreX() + 6.0f, circle.getY() + 2.0f,
                             circle.getCentreX(), circle.getY() + 14.0f);
        g.setColour (juce::Colour (0xfff4f4f5));
        g.fillPath (pointer);

        // Centre readout.
        auto centre = circle.reduced (radius * 0.34f);
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xff17171a), centre.getCentreX(), centre.getY(),
            juce::Colour (0xff09090b), centre.getCentreX(), centre.getBottom(), false));
        g.fillEllipse (centre);
        g.setColour (juce::Colour (0xff34343a));
        g.drawEllipse (centre, 1.0f);

        g.setColour (tuning);
        g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
        g.drawFittedText (hasNote_ ? note_ : "---",
                          centre.toNearestInt().removeFromTop (38),
                          juce::Justification::centred, 1);

        juce::String centsText = "---";

        if (hasNote_)
            centsText = std::abs (targetCents_) < 2.0f
                ? "IN TUNE"
                : juce::String (targetCents_, 1) + " c";

        g.setColour (juce::Colour (kTextDim));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawFittedText (centsText,
                          centre.toNearestInt().removeFromBottom (20),
                          juce::Justification::centred, 1);

        g.setColour (juce::Colour (kTextDim).withAlpha (0.8f));
        g.setFont (juce::FontOptions (9.0f));
        g.drawText ("REF " + juce::String (referenceHz_, 1) + " Hz",
                    circle.toNearestInt().removeFromBottom (12),
                    juce::Justification::centred);
    }

private:
    juce::Colour tuningColour() const
    {
        if (! hasNote_)
            return juce::Colour (kTextDim);

        if (std::abs (targetCents_) < 2.0f)
            return juce::Colour (0xff4ade80);

        if (std::abs (targetCents_) < 10.0f)
            return juce::Colour (0xfffbbf24);

        return juce::Colour (0xfff87171);
    }

    float targetCents_ = 0.0f;
    float displayCents_ = 0.0f;
    float referenceHz_ = 440.0f;
    juce::String note_ { "---" };
    bool hasNote_ = false;
};

//==============================================================================
/// A numeric reference-frequency field (typed value, not a knob).
class ReferenceField : public juce::Component
{
public:
    ReferenceField (APVTS& state, const juce::String& paramId,
                    const juce::String& labelText)
    {
        addAndMakeVisible (label_);
        label_.setText (labelText, juce::dontSendNotification);
        label_.setJustificationType (juce::Justification::centred);
        label_.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        label_.setColour (juce::Label::textColourId, juce::Colour (kText));

        addAndMakeVisible (editor_);
        editor_.setMultiLine (false);
        editor_.setJustification (juce::Justification::centred);
        editor_.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        editor_.setInputRestrictions (7, "0123456789.");
        editor_.setSelectAllWhenFocused (true);
        editor_.setColour (juce::TextEditor::backgroundColourId,
                           juce::Colour (0xff121214));
        editor_.setColour (juce::TextEditor::textColourId,
                           juce::Colour (0xfff4f4f5));
        editor_.setColour (juce::TextEditor::outlineColourId,
                           juce::Colour (kPanelLine));
        editor_.setColour (juce::TextEditor::focusedOutlineColourId,
                           juce::Colour (kAccent));
        editor_.onReturnKey = [this] { commit(); };
        editor_.onFocusLost = [this] { commit(); };
        editor_.onEscapeKey = [this]
        {
            editor_.setText (juce::String (currentHz_, 1),
                             juce::dontSendNotification);
            editor_.giveAwayKeyboardFocus();
        };

        if (auto* param = state.getParameter (paramId))
        {
            attachment_ = std::make_unique<juce::ParameterAttachment> (
                *param,
                [this] (float hz)
                {
                    currentHz_ = hz;

                    if (! editor_.hasKeyboardFocus (true))
                        editor_.setText (juce::String (hz, 1),
                                         juce::dontSendNotification);
                });
            attachment_->sendInitialUpdate();
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label_.setBounds (r.removeFromTop (16));

        editor_.setBounds (r.withSizeKeepingCentre (120, 42));
    }

private:
    void commit()
    {
        const auto text = editor_.getText().trim();

        if (text.isEmpty())
        {
            editor_.setText (juce::String (currentHz_, 1),
                             juce::dontSendNotification);
            return;
        }

        const float hz = juce::jlimit (400.0f, 480.0f, text.getFloatValue());

        if (attachment_ != nullptr)
            attachment_->setValueAsCompleteGesture (hz);

        editor_.setText (juce::String (hz, 1), juce::dontSendNotification);
    }

    juce::Label label_;
    juce::TextEditor editor_;
    std::unique_ptr<juce::ParameterAttachment> attachment_;
    float currentHz_ = 440.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReferenceField)
};

//==============================================================================
/// A label plus either a metal rotary knob or a metal toggle switch.
class ParamControl : public juce::Component
{
public:
    enum class Kind { Knob, Toggle };

    ParamControl (APVTS& state, const juce::String& paramId,
                  const juce::String& text, Kind kind = Kind::Knob,
                  bool invert = false)
    {
        addAndMakeVisible (label_);
        label_.setText (text, juce::dontSendNotification);
        label_.setJustificationType (juce::Justification::centred);
        label_.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        label_.setColour (juce::Label::textColourId, juce::Colour (kText));

        auto* param = state.getParameter (paramId);

        if (kind == Kind::Toggle
            || dynamic_cast<juce::AudioParameterBool*> (param) != nullptr)
        {
            toggle_.setButtonText ({});
            addAndMakeVisible (toggle_);

            if (invert)
                invertedAttachment_ = std::make_unique<InvertedButtonAttachment> (
                    state, paramId, toggle_);
            else
                buttonAttachment_ = std::make_unique<APVTS::ButtonAttachment> (
                    state, paramId, toggle_);
        }
        else
        {
            slider_.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider_.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 18);
            slider_.setColour (juce::Slider::textBoxTextColourId, juce::Colour (kTextDim));
            slider_.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

            if (param != nullptr && param->getLabel().isNotEmpty())
                slider_.setTextValueSuffix (" " + param->getLabel());

            slider_.setDoubleClickReturnValue (true,
                param != nullptr ? param->convertFrom0to1 (param->getDefaultValue()) : 0.0);
            addAndMakeVisible (slider_);
            sliderAttachment_ = std::make_unique<APVTS::SliderAttachment> (
                state, paramId, slider_);
        }
    }

    void setDarkText (bool dark)
    {
        label_.setColour (juce::Label::textColourId,
                          dark ? juce::Colour (0xff2c2416) : juce::Colour (kText));
        slider_.setColour (juce::Slider::textBoxTextColourId,
                           dark ? juce::Colour (0xff4a3c22) : juce::Colour (kTextDim));
    }

    void setLabelFontSize (float size)
    {
        label_.setFont (juce::FontOptions (size, juce::Font::bold));
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label_.setBounds (r.removeFromTop (16));

        if (sliderAttachment_ != nullptr)
            slider_.setBounds (r.reduced (1));
        else
            toggle_.setBounds (r.withSizeKeepingCentre (48, 48));
    }

    static constexpr int cellWidth  = 74;
    static constexpr int cellHeight = 100;

private:
    juce::Label label_;
    juce::Slider slider_;
    juce::ToggleButton toggle_;
    std::unique_ptr<APVTS::SliderAttachment> sliderAttachment_;
    std::unique_ptr<APVTS::ButtonAttachment> buttonAttachment_;
    std::unique_ptr<InvertedButtonAttachment> invertedAttachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParamControl)
};

//==============================================================================
/// A toggle that is clickable but paints nothing, so a Footswitch can own the
/// visuals while still being bound to an APVTS bool parameter.
class InvisibleToggle : public juce::ToggleButton
{
public:
    void paint (juce::Graphics&) override {}
};

//==============================================================================
/// Chrome stomp switch + status LED, bound to a unit's bypass parameter.
class Footswitch : public juce::Component
{
public:
    Footswitch (APVTS& state, const juce::String& paramId, juce::Colour ledColour,
                bool invertState = true)
        : ledColour_ (ledColour), invert_ (invertState)
    {
        addAndMakeVisible (button_);
        button_.setButtonText ({});
        button_.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        button_.onStateChange = [this] { repaint(); };

        attachment_ = std::make_unique<APVTS::ButtonAttachment> (state, paramId, button_);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (18);
        button_.setBounds (r.withSizeKeepingCentre (52, 52));
    }

    void paint (juce::Graphics& g) override
    {
        const bool on = invert_ ? ! button_.getToggleState()
                                : button_.getToggleState();

        auto ledArea = getLocalBounds().removeFromTop (18);
        const auto ledCentre = ledArea.getCentre().toFloat();

        if (on)
        {
            g.setColour (ledColour_.withAlpha (0.35f));
            g.fillEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre (ledCentre));
        }

        g.setColour (on ? ledColour_ : juce::Colour (0xff1b1b1e));
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (ledCentre));
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (ledCentre), 1.0f);

        g.setColour (on ? juce::Colour (0xffe4e4e7) : juce::Colour (kTextDim));
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText (on ? "ON" : "OFF",
                    ledArea.withTrimmedLeft (ledArea.getCentreX() - ledArea.getX() + 8),
                    juce::Justification::centredLeft);

        auto sw = getLocalBounds().withTrimmedTop (18)
                      .withSizeKeepingCentre (52, 52).toFloat();

        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (sw.translated (0.0f, 3.0f));

        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xffe1e1e7), sw.getX(), sw.getY(),
            juce::Colour (0xff5a5a62), sw.getX(), sw.getBottom(), false));
        g.fillEllipse (sw);
        g.setColour (juce::Colour (0xff141416));
        g.drawEllipse (sw, 1.5f);

        auto cap = sw.reduced (7.0f);
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xff414147), cap.getCentreX(), cap.getY(),
            juce::Colour (0xff151517), cap.getCentreX(), cap.getBottom(), false));
        g.fillEllipse (cap);

        auto inner = cap.reduced (cap.getWidth() * 0.24f);
        g.setColour (juce::Colour (0xff232327));
        g.fillEllipse (inner);
        g.setColour (juce::Colour (0xff55555e));
        g.drawEllipse (inner, 1.0f);
    }

private:
    InvisibleToggle button_;
    std::unique_ptr<APVTS::ButtonAttachment> attachment_;
    juce::Colour ledColour_;
    bool invert_ = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Footswitch)
};

//==============================================================================
/// A stompbox, amp head or rack unit, with controls laid out inside it.
class SectionPanel : public juce::Component,
                     private juce::Timer
{
public:
    SectionPanel (juce::String title, UnitStyle style, int columns, int fixedWidth,
                  juce::Colour body, juce::Colour accent,
                  std::function<bool()> reducedMotion)
        : title_ (std::move (title)), style_ (style), columns_ (columns),
          fixedWidth_ (fixedWidth), bodyColour_ (body), accentColour_ (accent),
          reducedMotion_ (std::move (reducedMotion))
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

    void setFooter (juce::Component* footer, int height)
    {
        footer_ = footer;
        footerHeight_ = height;
        if (footer_ != nullptr)
            addAndMakeVisible (footer_);
    }

    void setPreferredHeight (int height) { preferredHeight_ = height; }
    int preferredHeight() const { return preferredHeight_; }

    void setItemCellHeight (int height) { itemCellHeight_ = height; }

    void setRowsFirst (bool shouldComeFirst) { rowsFirst_ = shouldComeFirst; }
    bool isAmpHead() const { return style_ == UnitStyle::AmpHead; }

    int preferredWidth() const
    {
        if (fixedWidth_ > 0)
            return fixedWidth_;

        return pad_ * 2 + columns_ * ParamControl::cellWidth + 12;
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (3.0f);

        switch (style_)
        {
            case UnitStyle::Pedal:    paintPedal (g, bounds); break;
            case UnitStyle::AmpHead:  paintAmp (g, bounds); break;
            case UnitStyle::RackUnit: paintRack (g, bounds); break;
        }

        if (style_ == UnitStyle::AmpHead)
        {
            g.setColour (juce::Colour (0xff3a2f18));
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (title_, juce::Rectangle<int> (22, 8, 160, 26),
                        juce::Justification::centredLeft);
        }
        else if (style_ == UnitStyle::RackUnit)
        {
            g.setColour (accentColour_);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (title_, getLocalBounds().withTrimmedLeft (30).removeFromTop (titleHeight()),
                        juce::Justification::centredLeft);
        }

        if (highlight_ > 0.001f)
        {
            g.setColour (accentColour_.withAlpha (0.18f * highlight_));
            g.drawRoundedRectangle (bounds.reduced (1.0f), 10.0f, 2.0f);
        }
    }

    void mouseEnter (const juce::MouseEvent&) override { hovered_ = true; }
    void mouseExit  (const juce::MouseEvent&) override { hovered_ = false; }

    void resized() override
    {
        auto r = getLocalBounds().reduced (pad_);
        r.removeFromTop (titleHeight());

        if (style_ == UnitStyle::Pedal)
        {
            if (footer_ != nullptr)
            {
                auto footerArea = r.removeFromBottom (footerHeight_);
                footer_->setBounds (footerArea.withSizeKeepingCentre (64, footerHeight_));
            }

            const int count = (int) items_.size();
            const int gridRows = (count + columns_ - 1) / std::max (1, columns_);
            const int gridWidth = columns_ * ParamControl::cellWidth;
            const int gx = r.getX() + juce::jmax (0, (r.getWidth() - gridWidth) / 2);

            auto layoutItems = [&] (int startY)
            {
                for (int row = 0; row < gridRows; ++row)
                    for (int col = 0; col < columns_; ++col)
                    {
                        const int index = row * columns_ + col;
                        if (index >= count)
                            break;

                        items_[(size_t) index]->setBounds (
                            gx + col * ParamControl::cellWidth,
                            startY + row * ParamControl::cellHeight,
                            ParamControl::cellWidth,
                            ParamControl::cellHeight);
                    }

                return startY + gridRows * ParamControl::cellHeight + 6;
            };

            auto layoutRows = [&] (int startY)
            {
                int y = startY;
                for (auto& row : rows_)
                {
                    row.first->setBounds (r.getX(), y, r.getWidth(), row.second);
                    y += row.second + 6;
                }
                return y;
            };

            if (rowsFirst_)
            {
                const int afterRows = layoutRows (r.getY());
                layoutItems (afterRows);
            }
            else
            {
                const int afterItems = layoutItems (r.getY());
                layoutRows (afterItems);
            }
        }
        else
        {
            const int count = (int) items_.size();
            const int cols = std::max (1, columns_);
            const int itemRows = (count + cols - 1) / cols;
            const int cellW = r.getWidth() / cols;
            const int cellH = itemCellHeight_ > 0
                ? itemCellHeight_
                : (style_ == UnitStyle::AmpHead ? 104 : (count > 0 ? 76 : 0));

            for (int row = 0; row < itemRows; ++row)
                for (int col = 0; col < cols; ++col)
                {
                    const int index = row * cols + col;
                    if (index >= count)
                        break;

                    items_[(size_t) index]->setBounds (
                        r.getX() + col * cellW,
                        r.getY() + row * cellH,
                        cellW,
                        cellH);
                }

            int y = r.getY() + itemRows * cellH + 8;
            for (auto& row : rows_)
            {
                int rowX = r.getX() + 22;
                int rowW = r.getWidth() - 44;

                if (style_ == UnitStyle::AmpHead)
                {
                    rowW = juce::jmin (rowW, 360);
                    rowX = r.getX() + (r.getWidth() - rowW) / 2;
                }

                row.first->setBounds (rowX, y, rowW, row.second);
                y += row.second + 6;
            }
        }
    }

private:
    int titleHeight() const
    {
        switch (style_)
        {
            case UnitStyle::Pedal:    return 44;
            case UnitStyle::AmpHead:  return 34;
            default:                  return 26;
        }
    }

    void paintPedal (juce::Graphics& g, juce::Rectangle<float> b)
    {
        g.setColour (juce::Colours::black.withAlpha (0.40f));
        g.fillRoundedRectangle (b.translated (0.0f, 4.0f), 16.0f);

        g.setGradientFill (juce::ColourGradient (
            bodyColour_.brighter (0.22f), b.getCentreX(), b.getY(),
            bodyColour_.darker (0.55f), b.getCentreX(), b.getBottom(), false));
        g.fillRoundedRectangle (b, 16.0f);

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (b, 16.0f, 1.5f);
        g.setColour (juce::Colours::white.withAlpha (0.13f));
        g.drawRoundedRectangle (b.reduced (2.0f), 14.0f, 1.0f);

        auto plate = b.reduced (13.0f, 0.0f).withTrimmedTop (9.0f).withHeight (30.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (plate, 7.0f);
        g.setColour (accentColour_.withAlpha (0.55f));
        g.drawRoundedRectangle (plate, 7.0f, 1.0f);

        g.setColour (juce::Colour (0xfff4f4f5));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawFittedText (title_, plate.toNearestInt(),
                          juce::Justification::centred, 1);

        drawScrew (g, b.getX() + 11.0f, b.getY() + 11.0f, 3.5f);
        drawScrew (g, b.getRight() - 11.0f, b.getY() + 11.0f, 3.5f);
        drawScrew (g, b.getX() + 11.0f, b.getBottom() - 11.0f, 3.5f);
        drawScrew (g, b.getRight() - 11.0f, b.getBottom() - 11.0f, 3.5f);
    }

    void paintAmp (juce::Graphics& g, juce::Rectangle<float> b)
    {
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (b.translated (0.0f, 4.0f), 12.0f);

        // Tolex cabinet.
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xff202024), b.getX(), b.getY(),
            juce::Colour (0xff0c0c0e), b.getX(), b.getBottom(), false));
        g.fillRoundedRectangle (b, 12.0f);

        // Gold faceplate behind the knobs.
        auto face = b.reduced (10.0f).withTrimmedTop (6.0f).withHeight (145.0f);
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xffe2cf9a), face.getX(), face.getY(),
            juce::Colour (0xff9c8655), face.getX(), face.getBottom(), false));
        g.fillRoundedRectangle (face, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        for (float y = face.getY() + 3.0f; y < face.getBottom(); y += 3.0f)
            g.drawHorizontalLine ((int) y, face.getX() + 4.0f, face.getRight() - 4.0f);
        g.setColour (juce::Colour (0xff5a4a28));
        g.drawRoundedRectangle (face, 6.0f, 1.2f);

        // Grille cloth.
        auto grille = b.reduced (12.0f).withTrimmedTop (160.0f).withTrimmedBottom (14.0f);
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xff151517), grille.getX(), grille.getY(),
            juce::Colour (0xff09090b), grille.getX(), grille.getBottom(), false));
        g.fillRoundedRectangle (grille, 7.0f);
        g.setColour (juce::Colour (0xff1f1f23));
        for (float x = grille.getX() + 6.0f; x < grille.getRight(); x += 9.0f)
            g.drawLine (x, grille.getY() + 6.0f, x, grille.getBottom() - 6.0f, 0.7f);
        for (float y = grille.getY() + 6.0f; y < grille.getBottom(); y += 9.0f)
            g.drawLine (grille.getX() + 6.0f, y, grille.getRight() - 6.0f, y, 0.7f);
        g.setColour (juce::Colour (0xff2a2a2f));
        g.drawRoundedRectangle (grille, 7.0f, 1.2f);

        g.setColour (juce::Colour (0xffe6dfcd));
        g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        g.drawFittedText ("NAMBERRY", grille.toNearestInt(),
                          juce::Justification::centred, 1);

        g.setColour (juce::Colour (0xff25e07a));
        g.fillEllipse (b.getRight() - 34.0f, b.getY() + 18.0f, 7.0f, 7.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (b.getRight() - 34.0f, b.getY() + 18.0f, 7.0f, 7.0f, 1.0f);

        drawScrew (g, b.getX() + 10.0f, b.getY() + 10.0f, 3.5f);
        drawScrew (g, b.getRight() - 10.0f, b.getY() + 10.0f, 3.5f);
        drawScrew (g, b.getX() + 10.0f, b.getBottom() - 10.0f, 3.5f);
        drawScrew (g, b.getRight() - 10.0f, b.getBottom() - 10.0f, 3.5f);
    }

    void paintRack (juce::Graphics& g, juce::Rectangle<float> b)
    {
        g.setColour (juce::Colours::black.withAlpha (0.40f));
        g.fillRoundedRectangle (b.translated (0.0f, 3.0f), 6.0f);

        drawBrushedPanel (g, b, juce::Colour (0xff303036), juce::Colour (0xff151517), 6.0f);
        g.setColour (juce::Colour (0xff0a0a0c));
        g.drawRoundedRectangle (b, 6.0f, 1.5f);

        // Rack ears.
        auto leftEar = b.withWidth (22.0f);
        auto rightEar = b.withTrimmedLeft (b.getWidth() - 22.0f);
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xff3a3a41), leftEar.getX(), leftEar.getY(),
            juce::Colour (0xff1a1a1d), leftEar.getX(), leftEar.getBottom(), false));
        g.fillRect (leftEar);
        g.fillRect (rightEar);
        g.setColour (juce::Colour (0xff0b0b0d));
        g.drawLine (leftEar.getRight(), leftEar.getY(), leftEar.getRight(), leftEar.getBottom());
        g.drawLine (rightEar.getX(), rightEar.getY(), rightEar.getX(), rightEar.getBottom());

        drawScrew (g, leftEar.getCentreX(), b.getY() + 18.0f, 3.4f);
        drawScrew (g, leftEar.getCentreX(), b.getBottom() - 18.0f, 3.4f);
        drawScrew (g, rightEar.getCentreX(), b.getY() + 18.0f, 3.4f);
        drawScrew (g, rightEar.getCentreX(), b.getBottom() - 18.0f, 3.4f);

        g.setColour (accentColour_.withAlpha (0.55f));
        g.fillRect (b.getX() + 26.0f, b.getY() + 24.0f, b.getWidth() - 52.0f, 2.0f);

        // Vent slots bottom-right.
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        for (int i = 0; i < 6; ++i)
            g.fillRoundedRectangle (b.getRight() - 98.0f + i * 12.0f,
                                    b.getBottom() - 18.0f, 8.0f, 8.0f, 2.0f);
    }

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

    juce::String title_;
    UnitStyle style_;
    int columns_;
    int fixedWidth_;
    juce::Colour bodyColour_;
    juce::Colour accentColour_;
    std::function<bool()> reducedMotion_;
    std::vector<juce::Component*> items_;
    std::vector<std::pair<juce::Component*, int>> rows_;
    juce::Component* footer_ = nullptr;
    int footerHeight_ = 0;
    int preferredHeight_ = 0;
    int itemCellHeight_ = 0;
    bool rowsFirst_ = false;
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
    NamLoadBox (juce::String caption,
                std::function<bool (const juce::File&)> loader,
                std::function<juce::File()> getRootDirectory,
                std::function<juce::File()> getCurrentFile)
        : caption_ (std::move (caption)),
          loader_ (std::move (loader)),
          getRootDirectory_ (std::move (getRootDirectory)),
          getCurrentFile_ (std::move (getCurrentFile))
    {
        addAndMakeVisible (name_);
        name_.setText ("No model loaded", juce::dontSendNotification);
        name_.setJustificationType (juce::Justification::centred);
        name_.setFont (juce::FontOptions (10.5f));
        name_.setColour (juce::Label::textColourId, juce::Colour (kTextDim));

        addAndMakeVisible (prevButton_);
        prevButton_.setButtonText ("<");
        prevButton_.setTooltip ("Previous .nam file in this folder");
        prevButton_.onClick = [this] { browse (-1); };

        addAndMakeVisible (loadButton_);
        loadButton_.setButtonText ("BROWSE .NAM");
        loadButton_.onClick = [this]
        {
            if (onBrowse_ != nullptr)
                onBrowse_();
        };

        addAndMakeVisible (nextButton_);
        nextButton_.setButtonText (">");
        nextButton_.setTooltip ("Next .nam file in this folder");
        nextButton_.onClick = [this] { browse (1); };
    }

    void setOnBrowse (std::function<void()> callback)
    {
        onBrowse_ = std::move (callback);
    }

    bool loadFile (const juce::File& file) { return load (file); }

    void setDisplayedFileName (const juce::String& name)
    {
        const auto display = name.isNotEmpty() ? name : juce::String ("No model loaded");

        if (name_.getText() != display)
        {
            name_.setText (display, juce::dontSendNotification);
            name_.setColour (juce::Label::textColourId,
                             name.isNotEmpty() ? juce::Colour (kText)
                                               : juce::Colour (kTextDim));
        }
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (const auto& f : files)
            if (f.endsWithIgnoreCase (".nam"))
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
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);

        g.setColour (juce::Colour (dragOver_ ? 0xff2a2a2f : 0xff121214));
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
        loadButton_.setBounds (r.removeFromTop (48).reduced (2));
        r.removeFromTop (2);

        auto arrows = r.removeFromTop (48);
        prevButton_.setBounds (arrows.removeFromLeft (48));
        nextButton_.setBounds (arrows.removeFromRight (48));
    }

private:
    void browse (int delta)
    {
        const auto current = currentFile_.existsAsFile()
            ? currentFile_
            : (getCurrentFile_ != nullptr ? getCurrentFile_() : juce::File());

        const auto root = getRootDirectory_ != nullptr ? getRootDirectory_()
                                                       : juce::File();
        const auto directory = current.existsAsFile()
            ? current.getParentDirectory()
            : root;

        const auto files = listMatchingFiles (directory, juce::StringArray { ".nam" });

        if (files.isEmpty())
            return;

        int index = files.indexOf (current);

        if (index < 0)
            index = delta > 0 ? 0 : files.size() - 1;
        else
            index = (index + delta + files.size()) % files.size();

        load (files[index]);
    }

    bool load (const juce::File& file)
    {
        if (loader_ != nullptr && loader_ (file))
        {
            currentFile_ = file;
            setDisplayedFileName (file.getFileName());
            return true;
        }

        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Could not load model",
            "NAMberry could not load: " + file.getFileName());

        return false;
    }

    juce::String caption_;
    std::function<bool (const juce::File&)> loader_;
    std::function<juce::File()> getRootDirectory_;
    std::function<juce::File()> getCurrentFile_;
    std::function<void()> onBrowse_;
    juce::File currentFile_;
    juce::Label name_;
    juce::TextButton prevButton_, loadButton_, nextButton_;
    bool dragOver_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamLoadBox)
};

//==============================================================================
/// One of the four IR slots: load button, mix, pan and phase invert.
class IrSlotRow : public juce::Component,
                  public juce::FileDragAndDropTarget,
                  private juce::Timer
{
public:
    IrSlotRow (APVTS& state, int slot, NAMBerryProcessor& processor)
        : slot_ (slot), processor_ (processor)
    {
        startTimer (500);
        addAndMakeVisible (title_);
        title_.setText ("IR " + juce::String (slot + 1), juce::dontSendNotification);
        title_.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        title_.setColour (juce::Label::textColourId, juce::Colour (kText));

        addAndMakeVisible (loadButton_);
        loadButton_.setButtonText ("BROWSE");
        loadButton_.onClick = [this]
        {
            if (onBrowse_ != nullptr)
                onBrowse_();
        };

        addAndMakeVisible (prevButton_);
        prevButton_.setButtonText ("<");
        prevButton_.setTooltip ("Previous IR in this folder");
        prevButton_.onClick = [this] { browse (-1); };

        addAndMakeVisible (nextButton_);
        nextButton_.setButtonText (">");
        nextButton_.setTooltip ("Next IR in this folder");
        nextButton_.onClick = [this] { browse (1); };

        addAndMakeVisible (fileName_);
        fileName_.setText ("empty", juce::dontSendNotification);
        fileName_.setFont (juce::FontOptions (11.0f));
        fileName_.setColour (juce::Label::textColourId, juce::Colour (kTextDim));

        if (auto* irLoader = processor.getIrLoader())
            setDisplayedFileName (irLoader->getLoadedFileName (slot));

        buildKnob (state, "irMix" + juce::String (slot), "Mix", mix_, mixLabel_);
        buildKnob (state, "irPan" + juce::String (slot), "Pan", pan_, panLabel_);

        addAndMakeVisible (phase_);
        phase_.setButtonText ({});
        addAndMakeVisible (phaseLabel_);
        phaseLabel_.setText ("Phase", juce::dontSendNotification);
        phaseLabel_.setJustificationType (juce::Justification::centred);
        phaseLabel_.setFont (juce::FontOptions (12.0f));
        phaseLabel_.setColour (juce::Label::textColourId, juce::Colour (kText));
        phaseAttachment_ = std::make_unique<APVTS::ButtonAttachment> (
            state, "irPhase" + juce::String (slot), phase_);
    }

    void setOnBrowse (std::function<void()> callback)
    {
        onBrowse_ = std::move (callback);
    }

    bool loadFile (const juce::File& file) { return load (file); }

    void setDisplayedFileName (const juce::String& name)
    {
        const auto display = name.isNotEmpty() ? name : juce::String ("empty");

        if (fileName_.getText() != display)
        {
            fileName_.setText (display, juce::dontSendNotification);
            fileName_.setColour (juce::Label::textColourId,
                                 name.isNotEmpty() ? juce::Colour (kText)
                                                   : juce::Colour (kTextDim));
        }
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
        g.setColour (juce::Colour (dragOver_ ? 0xff2a2a2f : 0xff141417));
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (juce::Colour (dragOver_ ? kAccent : 0xff2a2a2f));
        g.drawRoundedRectangle (b, 6.0f, dragOver_ ? 2.0f : 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);

        auto top = r.removeFromTop (18);
        title_.setBounds (top.removeFromLeft (50));
        fileName_.setBounds (top);

        auto left = r.removeFromLeft (108);
        auto stack = left.withSizeKeepingCentre (102, 110);
        loadButton_.setBounds (stack.removeFromTop (56));
        stack.removeFromTop (6);

        auto arrows = stack.removeFromTop (48);
        prevButton_.setBounds (arrows.removeFromLeft (48));
        arrows.removeFromLeft (6);
        nextButton_.setBounds (arrows.removeFromLeft (48));

        auto phaseCol = r.removeFromRight (60);
        auto toggleArea = phaseCol.withTrimmedBottom (18);
        phase_.setBounds (toggleArea.withSizeKeepingCentre (48, 48));
        phaseLabel_.setBounds (phaseCol.removeFromBottom (16));

        auto knobArea = r;
        const int knobW = knobArea.getWidth() / 2;

        auto mixArea = knobArea.removeFromLeft (knobW);
        mixLabel_.setBounds (mixArea.removeFromBottom (16));
        mix_.setBounds (mixArea);

        panLabel_.setBounds (knobArea.removeFromBottom (16));
        pan_.setBounds (knobArea);
    }

private:
    void buildKnob (APVTS& state, const juce::String& id,
                    const juce::String& text, juce::Slider& slider,
                    juce::Label& label)
    {
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, juce::Colour (kText));
        addAndMakeVisible (label);

        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 50, 18);
        slider.setColour (juce::Slider::textBoxTextColourId, juce::Colour (kTextDim));
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (slider);
        mixAttachments_.push_back (std::make_unique<APVTS::SliderAttachment> (state, id, slider));
    }

    void browse (int delta)
    {
        auto* irLoader = processor_.getIrLoader();
        const auto current = currentFile_.existsAsFile()
            ? currentFile_
            : (irLoader != nullptr ? irLoader->getLoadedFile (slot_)
                                   : juce::File());

        const auto directory = current.existsAsFile()
            ? current.getParentDirectory()
            : processor_.getIrDirectory();

        const auto files = listMatchingFiles (
            directory, juce::StringArray { ".wav", ".aif", ".aiff" });

        if (files.isEmpty())
            return;

        int index = files.indexOf (current);

        if (index < 0)
            index = delta > 0 ? 0 : files.size() - 1;
        else
            index = (index + delta + files.size()) % files.size();

        load (files[index]);
    }

    bool load (const juce::File& file)
    {
        if (processor_.loadIr (slot_, file))
        {
            currentFile_ = file;
            setDisplayedFileName (file.getFileName());
            return true;
        }

        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Could not load IR",
            "NAMberry could not load: " + file.getFileName());

        return false;
    }

    void timerCallback() override
    {
        if (auto* irLoader = processor_.getIrLoader())
            setDisplayedFileName (irLoader->getLoadedFileName (slot_));
    }

    int slot_;
    NAMBerryProcessor& processor_;
    juce::File currentFile_;
    juce::Label title_, fileName_, mixLabel_, panLabel_, phaseLabel_;
    juce::TextButton prevButton_, loadButton_, nextButton_;
    juce::ToggleButton phase_;
    juce::Slider mix_, pan_;
    std::vector<std::unique_ptr<APVTS::SliderAttachment>> mixAttachments_;
    std::unique_ptr<APVTS::ButtonAttachment> phaseAttachment_;
    std::function<void()> onBrowse_;
    bool dragOver_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IrSlotRow)
};

//==============================================================================
/// 2x2 grid of IR slot cards, giving each slot much more room for its knobs.
class IrSlotGrid : public juce::Component
{
public:
    IrSlotGrid (APVTS& state, NAMBerryProcessor& processor,
                std::function<void (IrSlotRow&)> openBrowser)
    {
        for (int slot = 0; slot < 4; ++slot)
        {
            auto card = std::make_unique<IrSlotRow> (state, slot, processor);
            auto* raw = card.get();
            raw->setOnBrowse ([openBrowser, raw] { openBrowser (*raw); });
            addAndMakeVisible (*card);
            cards_.push_back (std::move (card));
        }
    }

    void resized() override
    {
        constexpr int gap = 8;
        const int cardW = (getWidth() - gap) / 2;
        const int cardH = (getHeight() - gap) / 2;

        for (int i = 0; i < 4; ++i)
            cards_[(size_t) i]->setBounds ((i % 2) * (cardW + gap),
                                           (i / 2) * (cardH + gap),
                                           cardW, cardH);
    }

private:
    std::vector<std::unique_ptr<IrSlotRow>> cards_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IrSlotGrid)
};

} // namespace

//==============================================================================
NAMBerryEditor::NAMBerryEditor (NAMBerryProcessor& p)
    : AudioProcessorEditor (&p), processor_ (p),
      tabBar_ (juce::TabbedButtonBar::TabsAtBottom)
{
    lnf_ = std::make_unique<HardwareLookAndFeel>();
    setLookAndFeel (lnf_.get());

    addAndMakeVisible (reducedMotionButton_);
    reducedMotionButton_.setButtonText ("Reduced Motion");
    reducedMotionAttachment_ = std::make_unique<APVTS::ButtonAttachment> (
        processor_.getValueTreeState(), "reducedMotion", reducedMotionButton_);

    tabBar_.addChangeListener (this);
    addAndMakeVisible (tabBar_);

    viewport_.setViewedComponent (&content_, false);
    viewport_.setScrollBarsShown (false, true);
    viewport_.setColour (juce::ScrollBar::thumbColourId, juce::Colour (kAccent).withAlpha (0.6f));
    viewport_.setColour (juce::ScrollBar::trackColourId, juce::Colour (kPanel));
    addAndMakeVisible (viewport_);

    buildUi();

    browser_ = std::make_unique<DirectoryBrowser>();
    browser_->setName ("DirectoryBrowser");
    addChildComponent (browser_.get());
    browser_->setVisible (false);

    setResizable (true, true);
    setResizeLimits (1000, 600, 4000, 1000);
    setSize (1280, 640);

    startTimerHz (30);
}

NAMBerryEditor::~NAMBerryEditor()
{
    tabBar_.removeChangeListener (this);
    stopTimer();
    setLookAndFeel (nullptr);
}

void NAMBerryEditor::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &tabBar_)
    {
        currentTabIndex_ = tabBar_.getCurrentTabIndex();
        layoutContent();
    }
}

//==============================================================================
void NAMBerryEditor::buildUi()
{
    tabBar_.clearTabs();
    tabPanels_.assign (5, {});
    allPanels_.clear();
    nameUpdaters_.clear();
    tunerUpdater_ = {};

    auto& state = processor_.getValueTreeState();
    auto reduced = [this]() -> bool
    {
        auto* value = processor_.getValueTreeState()
                          .getRawParameterValue ("reducedMotion");
        return value != nullptr && value->load() > 0.5f;
    };

    auto makePanel = [this, &reduced] (int tabIndex, const juce::String& title,
                                       UnitStyle style, int cols, int width,
                                       juce::Colour body, juce::Colour accent) -> SectionPanel*
    {
        auto panel = std::make_unique<SectionPanel> (
            title, style, cols, width, body, accent, reduced);
        auto* raw = panel.get();
        ownedControls_.push_back (std::move (panel));
        tabPanels_[tabIndex].push_back (raw);
        allPanels_.push_back (raw);
        content_.addAndMakeVisible (raw);
        return raw;
    };

    auto makeControl = [this, &state] (const juce::String& id,
                                       const juce::String& text,
                                       ParamControl::Kind kind = ParamControl::Kind::Knob,
                                       bool invert = false)
    {
        auto control = std::make_unique<ParamControl> (state, id, text, kind, invert);
        auto* raw = control.get();
        ownedControls_.push_back (std::move (control));
        return raw;
    };

    auto makeFootswitch = [this, &state] (const juce::String& id, juce::Colour led)
    {
        auto fs = std::make_unique<Footswitch> (state, id, led);
        auto* raw = fs.get();
        ownedControls_.push_back (std::move (fs));
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
            },
            [this] { return processor_.getNamDirectory(); },
            [this, pedal]
            {
                auto* loader = pedal ? processor_.getNamPedal()
                                     : processor_.getNamModel();
                return loader != nullptr ? loader->getLoadedFile() : juce::File();
            });
        auto* raw = box.get();

        if (auto* loaderNow = pedal ? processor_.getNamPedal() : processor_.getNamModel())
            raw->setDisplayedFileName (loaderNow->getLoadedFileName());

        raw->setOnBrowse ([this, raw]
        {
            openBrowser (BrowserKind::Nam, [raw] (const juce::File& file)
            {
                raw->loadFile (file);
            });
        });

        nameUpdaters_.push_back ([this, raw, pedal]
        {
            auto* loader = pedal ? processor_.getNamPedal() : processor_.getNamModel();

            if (loader != nullptr)
                raw->setDisplayedFileName (loader->getLoadedFileName());
        });

        ownedControls_.push_back (std::move (box));
        return raw;
    };

    tabBar_.clearTabs();
    tabBar_.addTab ("Pre FX", juce::Colour (0xff1c1c20), -1);
    tabBar_.addTab ("Amp", juce::Colour (0xff1c1c20), -1);
    tabBar_.addTab ("Post EQ", juce::Colour (0xff1c1c20), -1);
    tabBar_.addTab ("IR", juce::Colour (0xff1c1c20), -1);
    tabBar_.addTab ("Post FX", juce::Colour (0xff1c1c20), -1);
    tabBar_.setCurrentTabIndex (currentTabIndex_, false);

    // Unit colours.
    const juce::Colour tunerBody   (0xff0d5c58), tunerAccent   (0xff5eead4);
    const juce::Colour capoBody    (0xff7a4a10), capoAccent    (0xfffbbf24);
    const juce::Colour gateBody    (0xff34343c), gateAccent    (0xffcbd5e1);
    const juce::Colour compBody    (0xff82380f), compAccent    (0xfffb923c);
    const juce::Colour pedalBody   (0xff1f5f37), pedalAccent   (0xff4ade80);
    const juce::Colour eqBody      (0xff3f3f46), eqAccent      (0xffd4d4d8);
    const juce::Colour ampBody     (0xff141416), ampAccent     (0xffd4af37);
    const juce::Colour rackBody    (0xff26262b), rackAccent    (0xff9ca3af);
    const juce::Colour chorusBody  (0xff1e3a8a), chorusAccent  (0xff60a5fa);
    const juce::Colour flangerBody (0xff4c1d95), flangerAccent (0xffc084fc);
    const juce::Colour phaserBody  (0xff831843), phaserAccent  (0xfff472b6);
    const juce::Colour delayBody   (0xff713f12), delayAccent   (0xfffacc15);
    const juce::Colour reverbBody  (0xff1e3a5f), reverbAccent  (0xff93c5fd);
    const juce::Colour outBody     (0xff26262b), outAccent     (0xff22d3ee);

    // 1. Pre FX -------------------------------------------------------------
    if (auto* tuner = makePanel (0, "TUNER", UnitStyle::Pedal, 1, 0,
                                 tunerBody, tunerAccent))
    {
        auto wheel = std::make_unique<TuningWheel>();
        auto* rawWheel = wheel.get();
        ownedControls_.push_back (std::move (wheel));

        tuner->addRow (rawWheel, 170);
        tuner->setRowsFirst (true);

        auto reference = std::make_unique<ReferenceField> (
            state, "tuningReference", "Ref Hz");
        auto* rawReference = reference.get();
        ownedControls_.push_back (std::move (reference));
        tuner->addRow (rawReference, 70);

        if (auto* t = processor_.getDropTuner())
            rawWheel->setState (t->getCentsDeviation(),
                                t->getDetectedNote(),
                                t->getReferenceFrequency());

        tunerUpdater_ = [this, rawWheel]
        {
            if (auto* tuner = processor_.getDropTuner())
                rawWheel->setState (tuner->getCentsDeviation(),
                                    tuner->getDetectedNote(),
                                    tuner->getReferenceFrequency());
        };
    }

    if (auto* pitch = makePanel (0, "PITCH", UnitStyle::Pedal, 2, 0,
                                 capoBody, capoAccent))
    {
        pitch->addItem (makeControl ("capoFret", "Semitones"));
        pitch->addItem (makeControl ("capoMix", "Mix"));
        pitch->addItem (makeControl ("pitchEngine", "PSOLA", ParamControl::Kind::Toggle));
        pitch->setFooter (makeFootswitch ("capoBypass", capoAccent), 82);
    }

    if (auto* gate = makePanel (0, "GATE", UnitStyle::Pedal, 2, 0,
                                gateBody, gateAccent))
    {
        gate->addItem (makeControl ("gateThreshold", "Threshold"));
        gate->addItem (makeControl ("gateAttack", "Attack"));
        gate->addItem (makeControl ("gateRelease", "Release"));
        gate->setFooter (makeFootswitch ("gateBypass", gateAccent), 82);
    }

    if (auto* comp = makePanel (0, "COMP", UnitStyle::Pedal, 2, 0,
                                compBody, compAccent))
    {
        comp->addItem (makeControl ("compThreshold", "Threshold"));
        comp->addItem (makeControl ("compRatio", "Ratio"));
        comp->addItem (makeControl ("compAttack", "Attack"));
        comp->addItem (makeControl ("compRelease", "Release"));
        comp->addItem (makeControl ("compMakeup", "Makeup"));
        comp->addItem (makeControl ("compTight", "Tight", ParamControl::Kind::Toggle));
        comp->setFooter (makeFootswitch ("compBypass", compAccent), 82);
    }

    if (auto* pedal = makePanel (0, "PEDAL", UnitStyle::Pedal, 3, 0,
                                 pedalBody, pedalAccent))
    {
        pedal->addItem (makeControl ("pedalGain", "Gain"));
        pedal->addItem (makeControl ("pedalBass", "Bass"));
        pedal->addItem (makeControl ("pedalMid", "Mid"));
        pedal->addItem (makeControl ("pedalTreble", "Treble"));
        pedal->addItem (makeControl ("pedalOutput", "Output"));
        pedal->addRow (makeNamBox ("PEDAL MODEL", true), 140);
        pedal->setFooter (makeFootswitch ("pedalBypass", pedalAccent), 82);
    }

    if (auto* preEq = makePanel (0, "PRE-EQ", UnitStyle::Pedal, 3, 0,
                                 eqBody, eqAccent))
    {
        const char* bandNames[] = { "Low", "Mid", "High" };
        for (int band = 0; band < 3; ++band)
        {
            const auto i = juce::String (band);
            preEq->addItem (makeControl ("preEqFreq" + i, juce::String (bandNames[band]) + " Hz"));
            preEq->addItem (makeControl ("preEqGain" + i, juce::String (bandNames[band]) + " dB"));
            preEq->addItem (makeControl ("preEqQ" + i, juce::String (bandNames[band]) + " Q"));
        }
        preEq->setFooter (makeFootswitch ("preEqBypass", eqAccent), 82);
    }

    // 2. Amp ----------------------------------------------------------------
    if (auto* amp = makePanel (1, "AMP", UnitStyle::AmpHead, 8, 760,
                               ampBody, ampAccent))
    {
        auto addAmpControl = [&] (const juce::String& id, const juce::String& text,
                                  ParamControl::Kind kind = ParamControl::Kind::Knob,
                                  bool invert = false)
        {
            auto* control = makeControl (id, text, kind, invert);
            control->setDarkText (true);
            return control;
        };

        amp->addItem (addAmpControl ("ampBypass", "Power", ParamControl::Kind::Toggle, true));
        amp->addItem (addAmpControl ("ampGain", "Gain"));
        amp->addItem (addAmpControl ("ampBass", "Bass"));
        amp->addItem (addAmpControl ("ampMid", "Mid"));
        amp->addItem (addAmpControl ("ampTreble", "Treble"));
        amp->addItem (addAmpControl ("ampPresence", "Presence"));
        amp->addItem (addAmpControl ("ampDepth", "Depth"));
        amp->addItem (addAmpControl ("ampOutput", "Output"));
        amp->addRow (makeNamBox ("AMP MODEL", false), 140);
    }

    // 3. Post EQ rack --------------------------------------------------------
    if (auto* postEq = makePanel (2, "POST AMP EQ", UnitStyle::RackUnit, 3, 820,
                                  rackBody, rackAccent))
    {
        const char* bandNames[] = { "Low", "Mid", "High" };
        for (int band = 0; band < 3; ++band)
        {
            const auto i = juce::String (band);

            auto addBandControl = [&] (const juce::String& id, const juce::String& text)
            {
                auto* control = makeControl (id, text);
                control->setLabelFontSize (12.5f);
                postEq->addItem (control);
            };

            addBandControl ("postEqFreq" + i, juce::String (bandNames[band]) + " Hz");
            addBandControl ("postEqGain" + i, juce::String (bandNames[band]) + " dB");
            addBandControl ("postEqQ" + i, juce::String (bandNames[band]) + " Q");
        }

        auto* bypass = makeControl ("postEqBypass", "Bypass", ParamControl::Kind::Toggle, true);
        bypass->setLabelFontSize (13.0f);
        postEq->addRow (bypass, 64);

        postEq->setItemCellHeight (120);
        postEq->setPreferredHeight (480);
    }

    // 4. IR rack -------------------------------------------------------------
    if (auto* ir = makePanel (3, "IR CAB", UnitStyle::RackUnit, 1, 820,
                              rackBody, rackAccent))
    {
        auto* bypass = makeControl ("irBypass", "Bypass", ParamControl::Kind::Toggle, true);
        bypass->setLabelFontSize (12.5f);
        ir->addRow (bypass, 64);

        auto grid = std::make_unique<IrSlotGrid> (
            state, processor_,
            [this] (IrSlotRow& row)
            {
                openBrowser (BrowserKind::Ir, [this, &row] (const juce::File& file)
                {
                    row.loadFile (file);
                });
            });
        auto* rawGrid = grid.get();
        ownedControls_.push_back (std::move (grid));
        ir->addRow (rawGrid, 350);

        ir->setPreferredHeight (470);
    }

    // 5. Post FX pedals ------------------------------------------------------
    if (auto* chorus = makePanel (4, "CHORUS", UnitStyle::Pedal, 2, 0,
                                  chorusBody, chorusAccent))
    {
        chorus->addItem (makeControl ("chorusMix", "Mix"));
        chorus->addItem (makeControl ("chorusRate", "Rate"));
        chorus->addItem (makeControl ("chorusDepth", "Depth"));
        chorus->setFooter (makeFootswitch ("chorusBypass", chorusAccent), 82);
    }

    if (auto* flanger = makePanel (4, "FLANGER", UnitStyle::Pedal, 2, 0,
                                   flangerBody, flangerAccent))
    {
        flanger->addItem (makeControl ("flangerMix", "Mix"));
        flanger->addItem (makeControl ("flangerRate", "Rate"));
        flanger->addItem (makeControl ("flangerDepth", "Depth"));
        flanger->addItem (makeControl ("flangerFeedback", "Feedback"));
        flanger->setFooter (makeFootswitch ("flangerBypass", flangerAccent), 82);
    }

    if (auto* phaser = makePanel (4, "PHASER", UnitStyle::Pedal, 2, 0,
                                  phaserBody, phaserAccent))
    {
        phaser->addItem (makeControl ("phaserMix", "Mix"));
        phaser->addItem (makeControl ("phaserRate", "Rate"));
        phaser->addItem (makeControl ("phaserDepth", "Depth"));
        phaser->setFooter (makeFootswitch ("phaserBypass", phaserAccent), 82);
    }

    if (auto* delay = makePanel (4, "DELAY", UnitStyle::Pedal, 2, 0,
                                 delayBody, delayAccent))
    {
        delay->addItem (makeControl ("delayMix", "Mix"));
        delay->addItem (makeControl ("delayTimeMs", "Time"));
        delay->addItem (makeControl ("delayFeedback", "Feedback"));
        delay->setFooter (makeFootswitch ("delayBypass", delayAccent), 82);
    }

    if (auto* reverb = makePanel (4, "REVERB", UnitStyle::Pedal, 2, 0,
                                  reverbBody, reverbAccent))
    {
        reverb->addItem (makeControl ("reverbMix", "Mix"));
        reverb->addItem (makeControl ("reverbDecay", "Decay"));
        reverb->addItem (makeControl ("reverbWidth", "Width"));
        reverb->setFooter (makeFootswitch ("reverbBypass", reverbAccent), 82);
    }

    if (auto* output = makePanel (4, "OUTPUT", UnitStyle::RackUnit, 1, 160,
                                  outBody, outAccent))
    {
        output->addItem (makeControl ("masterOutput", "Master"));
        output->setPreferredHeight (230);
    }

    layoutContent();
}

//==============================================================================
void NAMBerryEditor::layoutContent()
{
    constexpr int gap = 8;
    constexpr int margin = 8;

    if (tabPanels_.empty())
        return;

    for (auto* panel : allPanels_)
        panel->setVisible (false);

    const int viewportWidth = juce::jmax (1, viewport_.getWidth());
    const int contentHeight = juce::jmax (440, viewport_.getHeight() - 2 * margin);

    const int tabIndex = juce::jlimit (0, (int) tabPanels_.size() - 1,
                                       tabBar_.getCurrentTabIndex());

    struct Entry
    {
        SectionPanel* panel;
        int width;
        int height;
    };

    std::vector<Entry> entries;
    int totalWidth = 0;

    for (auto* panel : tabPanels_[(size_t) tabIndex])
    {
        auto* section = dynamic_cast<SectionPanel*> (panel);
        if (section == nullptr)
            continue;

        int width = section->preferredWidth();

        // The amp head stretches to the full window width.
        if (section->isAmpHead())
            width = juce::jmax (width, viewportWidth - 2 * margin);

        const int wantedHeight = section->preferredHeight();
        const int height = wantedHeight > 0
            ? juce::jmin (wantedHeight, contentHeight)
            : contentHeight;

        entries.push_back ({ section, width, height });
        totalWidth += width;
    }

    if (entries.empty())
        return;

    totalWidth += gap * ((int) entries.size() - 1);

    const int startX = totalWidth < viewportWidth
        ? (viewportWidth - totalWidth) / 2
        : margin;

    int x = startX;

    for (auto& entry : entries)
    {
        entry.panel->setVisible (true);
        entry.panel->setBounds (x, margin + (contentHeight - entry.height) / 2,
                                entry.width, entry.height);
        x += entry.width + gap;
    }

    content_.setSize (juce::jmax (viewportWidth, totalWidth + 2 * margin),
                      contentHeight + 2 * margin);
}

void NAMBerryEditor::openBrowser (BrowserKind kind,
                                  std::function<void (const juce::File&)> onFileChosen)
{
    if (browser_ == nullptr)
        return;

    const bool nam = kind == BrowserKind::Nam;
    const auto root = nam ? processor_.getNamDirectory()
                          : processor_.getIrDirectory();

    juce::StringArray extensions;

    if (nam)
    {
        extensions.add (".nam");
    }
    else
    {
        extensions.add (".wav");
        extensions.add (".aif");
        extensions.add (".aiff");
    }

    browser_->open (nam ? "NAM BROWSER" : "IR BROWSER",
                    extensions,
                    root,
                    [this, nam] (const juce::File& directory)
                    {
                        if (nam)
                            processor_.setNamDirectory (directory);
                        else
                            processor_.setIrDirectory (directory);
                    },
                    std::move (onFileChosen));

    browser_->setBounds (getLocalBounds());
    browser_->setVisible (true);
    browser_->toFront (true);
}

void NAMBerryEditor::resized()
{
    auto r = getLocalBounds();

    auto header = r.removeFromTop (60);
    reducedMotionButton_.setBounds (header.removeFromRight (190).withSizeKeepingCentre (180, 48));

    auto tabArea = r.removeFromBottom (56);
    const int maxTabWidth = juce::jmax (240, tabArea.getWidth() - 12);

    // Lay the bar out at a generous width once, measure the natural button
    // group, then shrink the bar to that width and centre it. The bar's own
    // background rail then matches the buttons exactly.
    tabBar_.setBounds (tabArea.withSizeKeepingCentre (juce::jmin (maxTabWidth, 760), 48));

    int minX = tabBar_.getWidth();
    int maxRight = 0;

    for (int i = 0; i < tabBar_.getNumTabs(); ++i)
        if (auto* button = tabBar_.getTabButton (i))
            if (button->isVisible())
            {
                minX = juce::jmin (minX, button->getX());
                maxRight = juce::jmax (maxRight, button->getRight());
            }

    const int naturalWidth = juce::jmax (0, maxRight - minX);
    const int barWidth = naturalWidth > 0
        ? juce::jmax (240, juce::jmin (maxTabWidth, naturalWidth))
        : juce::jmin (maxTabWidth, 760);

    tabBar_.setBounds (tabArea.withSizeKeepingCentre (barWidth, 48));

    viewport_.setBounds (r.reduced (4, 2));
    layoutContent();

    if (browser_ != nullptr)
        browser_->setBounds (getLocalBounds());
}

void NAMBerryEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (kBg));

    auto header = getLocalBounds().removeFromTop (60);

    g.setGradientFill (juce::ColourGradient (
        juce::Colour (0xff26262b), 0.0f, 0.0f,
        juce::Colour (0xff121214), 0.0f, (float) header.getBottom(), false));
    g.fillRect (header);

    g.setColour (juce::Colours::white.withAlpha (0.025f));
    for (int y = header.getY() + 3; y < header.getBottom(); y += 3)
        g.drawHorizontalLine (y, (float) header.getX(), (float) header.getRight());

    g.setColour (juce::Colour (kAccent));
    g.fillEllipse (17.0f, 24.0f, 8.0f, 8.0f);
    g.setColour (juce::Colour (kAccent).withAlpha (0.25f));
    g.fillEllipse (13.0f, 20.0f, 16.0f, 16.0f);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("NAMBERRY", header.reduced (34, 0),
                juce::Justification::centredLeft);

    g.setColour (juce::Colour (kTextDim));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawFittedText ("PRE FX   >   AMP   >   POST EQ   >   IR   >   POST FX",
                      header.reduced (200, 0),
                      juce::Justification::centred, 1);

    g.setColour (juce::Colour (kAccent).withAlpha (0.5f));
    g.fillRect (getLocalBounds().removeFromTop (60).removeFromBottom (2));
}

void NAMBerryEditor::timerCallback()
{
    if (tunerUpdater_)
        tunerUpdater_();

    for (auto& updater : nameUpdaters_)
        updater();
}

} // namespace namberry
