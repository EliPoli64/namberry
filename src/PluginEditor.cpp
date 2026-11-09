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

        const bool bypassed = static_cast<bool> (
            button.getProperties().getWithDefault ("bypassed", false));

        if (bypassed)
            body = body.darker (0.25f);

        g.setGradientFill (juce::ColourGradient (
            body.brighter (0.12f), bounds.getX(), bounds.getY(),
            body.darker (0.18f), bounds.getX(), bounds.getBottom(), false));
        g.fillRoundedRectangle (bounds, 5.0f);

        g.setColour (front ? juce::Colour (kAccent) : juce::Colour (0xff2a2a2f));
        g.drawRoundedRectangle (bounds, 5.0f, front ? 1.6f : 1.0f);

        auto content = bounds.reduced (7.0f, 4.0f);
        auto iconArea = content.removeFromTop (27.0f);
        auto textArea = content.removeFromTop (12.0f);

        auto iconColour = front ? juce::Colours::white : juce::Colour (kTextDim);
        if (bypassed)
            iconColour = iconColour.withAlpha (0.30f);

        g.setColour (iconColour);

        const auto text = button.getButtonText();

        if (text == "Pre FX")
        {
            auto pedal = iconArea.withSizeKeepingCentre (16.0f, 21.0f);
            g.drawRoundedRectangle (pedal, 2.0f, 1.3f);
            g.fillEllipse (pedal.getCentreX() - 5.0f, pedal.getY() + 3.0f, 3.2f, 3.2f);
            g.fillEllipse (pedal.getCentreX() + 1.8f, pedal.getY() + 3.0f, 3.2f, 3.2f);
            g.drawEllipse (pedal.getCentreX() - 3.0f, pedal.getBottom() - 7.0f,
                           6.0f, 4.0f, 1.1f);
        }
        else if (text == "Amp")
        {
            auto head = iconArea.withSizeKeepingCentre (24.0f, 15.0f);
            g.drawRoundedRectangle (head, 2.0f, 1.3f);
            g.drawHorizontalLine ((int) (head.getY() + 3.0f),
                                  head.getX() + 3.0f, head.getRight() - 3.0f);
            for (int i = 0; i < 3; ++i)
                g.fillEllipse (head.getX() + 5.0f + i * 5.0f,
                               head.getY() + 6.0f, 2.6f, 2.6f);
            g.drawRect (head.getX() + 3.0f, head.getBottom() - 4.0f,
                        head.getWidth() - 6.0f, 2.0f, 0.8f);
        }
        else if (text == "Post EQ")
        {
            const float xs[] = { -7.0f, 0.0f, 7.0f };
            const float ys[] = { -7.0f, 5.0f, -2.0f };
            for (int i = 0; i < 3; ++i)
            {
                const float x = iconArea.getCentreX() + xs[i];
                g.drawVerticalLine ((int) x, iconArea.getY() + 2.0f,
                                    iconArea.getBottom() - 2.0f);
                g.fillEllipse (x - 2.5f, iconArea.getCentreY() + ys[i], 5.0f, 5.0f);
            }
        }
        else if (text == "IR")
        {
            auto cab = iconArea.withSizeKeepingCentre (20.0f, 23.0f);
            g.drawRoundedRectangle (cab, 2.0f, 1.3f);
            const auto cone = cab.reduced (4.0f, 4.0f);
            g.drawEllipse (cone, 1.2f);
            g.fillEllipse (cone.getCentreX() - 2.0f, cone.getCentreY() - 2.0f,
                           4.0f, 4.0f);
        }
        else
        {
            juce::Path sparkle;
            sparkle.addStar (iconArea.getCentre(), 4, 3.0f, 9.0f, 0.0f);
            g.fillPath (sparkle);
            g.fillEllipse (iconArea.getRight() - 6.0f, iconArea.getY() + 3.0f, 3.0f, 3.0f);
            g.fillEllipse (iconArea.getX() + 2.0f, iconArea.getBottom() - 6.0f, 2.5f, 2.5f);
        }

        auto textColour = front ? juce::Colours::white : juce::Colour (kTextDim);
        if (bypassed)
            textColour = textColour.withAlpha (0.35f);

        g.setColour (textColour);
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        g.drawFittedText (text, textArea.toNearestInt(),
                          juce::Justification::centred, 1);

        if (bypassed)
        {
            g.setColour (juce::Colour (0xffef4444));
            g.fillEllipse (bounds.getRight() - 8.0f, bounds.getY() + 4.0f, 4.0f, 4.0f);
        }
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
/// Horizontal strobe tuning guide used in the wide Pre FX tuner panel.
class TunerStrip : public juce::Component
{
public:
    explicit TunerStrip (APVTS& state)
    {
        reference_ = std::make_unique<ReferenceField> (state, "tuningReference", "Ref Hz");
        addAndMakeVisible (*reference_);
    }

    void setState (float cents, const juce::String& note, float referenceHz)
    {
        if (! std::isfinite (cents))
            cents = 0.0f;

        targetCents_ = juce::jlimit (-50.0f, 50.0f, cents);
        note_ = note;
        referenceHz_ = referenceHz;
        hasNote_ = note.isNotEmpty() && note != "---";

        if (! hasNote_)
            targetCents_ = 0.0f;

        displayCents_ += (targetCents_ - displayCents_) * 0.35f;
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto referenceArea = r.removeFromRight (126);
        reference_->setBounds (referenceArea.withSizeKeepingCentre (120, 80));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        r.removeFromRight (126);
        auto noteArea = r.removeFromLeft (58);
        auto guide = r.reduced (4, 8);

        const auto tuning = tuningColour();

        g.setColour (tuning);
        g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
        g.drawFittedText (hasNote_ ? note_ : "---",
                          noteArea.removeFromTop (34),
                          juce::Justification::centred, 1);

        juce::String centsText = "---";

        if (hasNote_)
            centsText = std::abs (targetCents_) < 2.0f
                ? "IN TUNE"
                : juce::String (targetCents_, 1) + " c";

        g.setColour (juce::Colour (kTextDim));
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawFittedText (centsText, noteArea.removeFromTop (20),
                          juce::Justification::centred, 1);

        g.setColour (juce::Colour (0xff121214));
        g.fillRoundedRectangle (guide.toFloat(), 5.0f);
        g.setColour (juce::Colour (kPanelLine));
        g.drawRoundedRectangle (guide.toFloat(), 5.0f, 1.0f);

        auto inner = guide.reduced (7, 5);
        const float centreX = inner.getCentreX();
        const float halfWidth = inner.getWidth() * 0.44f;
        const float trackY = inner.getCentreY();

        g.setColour (juce::Colour (0xff4a4a52));
        g.drawLine (static_cast<float> (inner.getX()), trackY,
                    static_cast<float> (inner.getRight()), trackY, 1.0f);

        for (int c = -50; c <= 50; c += 10)
        {
            const float x = centreX + (float) c / 50.0f * halfWidth;
            const bool major = (c == 0 || std::abs (c) == 50);
            g.setColour (major ? juce::Colour (0xffa1a1aa) : juce::Colour (0xff52525b));
            g.drawVerticalLine ((int) std::round (x),
                                major ? trackY - 8.0f : trackY - 4.0f,
                                major ? trackY + 8.0f : trackY + 4.0f);
        }

        // Moving strobe bands: they settle around the centre pointer when the
        // note is in tune.
        const float bandOffset = displayCents_ * 1.35f;
        for (int band = -8; band <= 8; ++band)
        {
            const float x = centreX + band * 11.0f + bandOffset;

            if (x < inner.getX() || x > inner.getRight())
                continue;

            g.setColour (tuning.withAlpha (hasNote_ ? 0.55f : 0.15f));
            g.drawVerticalLine ((int) std::round (x), trackY - 10.0f, trackY + 10.0f);
        }

        const float needleX = juce::jlimit (static_cast<float> (inner.getX()),
                                            static_cast<float> (inner.getRight()),
                                            centreX + (targetCents_ / 50.0f) * halfWidth);
        g.setColour (tuning);
        g.drawVerticalLine ((int) std::round (needleX), trackY - 12.0f, trackY + 12.0f);

        juce::Path pointer;
        pointer.addTriangle (centreX - 4.0f, inner.getBottom() - 8.0f,
                             centreX + 4.0f, inner.getBottom() - 8.0f,
                             centreX, inner.getBottom() - 1.0f);
        g.setColour (juce::Colour (0xfff4f4f5));
        g.fillPath (pointer);

        g.setColour (juce::Colour (kTextDim));
        g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
        g.drawText ("-50", inner.removeFromLeft (20), juce::Justification::centredLeft);
        g.drawText ("0", inner.withSizeKeepingCentre (20, 10), juce::Justification::centred);
        g.drawText ("+50", inner.removeFromRight (24), juce::Justification::centredRight);
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

    std::unique_ptr<ReferenceField> reference_;
    float targetCents_ = 0.0f;
    float displayCents_ = 0.0f;
    float referenceHz_ = 440.0f;
    juce::String note_ { "---" };
    bool hasNote_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerStrip)
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

    void setStepSize (double step)
    {
        if (sliderAttachment_ != nullptr)
            slider_.setRange (slider_.getMinimum(), slider_.getMaximum(), step);
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
/// Compact always-visible room-mix and master-output knobs beside the tabs.
class BottomKnobs : public juce::Component
{
public:
    explicit BottomKnobs (APVTS& state)
    {
        addAndMakeVisible (title_);
        title_.setText ("ROOM / MASTER", juce::dontSendNotification);
        title_.setJustificationType (juce::Justification::centred);
        title_.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        title_.setColour (juce::Label::textColourId, juce::Colour (kAccent));

        roomKnob_ = std::make_unique<ParamControl> (state, "roomBlend", "Room Mix");
        outputKnob_ = std::make_unique<ParamControl> (state, "masterOutput", "Output");
        addAndMakeVisible (*roomKnob_);
        addAndMakeVisible (*outputKnob_);
    }

    int getPreferredHeight() const { return 108; }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);

        g.setColour (juce::Colour (0xff26262b));
        g.fillRoundedRectangle (bounds, 7.0f);
        g.setColour (juce::Colour (0xff0c0c0e));
        g.drawRoundedRectangle (bounds, 7.0f, 1.2f);
        g.setColour (juce::Colour (kAccent).withAlpha (0.8f));
        g.fillRect (bounds.getX() + 10.0f, bounds.getY() + 1.0f,
                    bounds.getWidth() - 20.0f, 2.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        title_.setBounds (r.removeFromTop (14));

        constexpr int cellWidth = ParamControl::cellWidth;
        auto controls = r.removeFromTop (84)
                         .withSizeKeepingCentre (cellWidth * 2, 84);

        roomKnob_->setBounds (controls.removeFromLeft (cellWidth));
        outputKnob_->setBounds (controls.removeFromLeft (cellWidth));
    }

private:
    juce::Label title_;
    std::unique_ptr<ParamControl> roomKnob_;
    std::unique_ptr<ParamControl> outputKnob_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BottomKnobs)
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
/// Two-position pickup-direction switch for the Impersonator.
class ModeSwitch : public juce::Component
{
public:
    ModeSwitch (APVTS& state, const juce::String& paramId)
    {
        addAndMakeVisible (toggle_);
        toggle_.setButtonText ({});
        toggle_.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        toggle_.onStateChange = [this] { repaint(); };
        attachment_ = std::make_unique<APVTS::ButtonAttachment> (state, paramId, toggle_);
    }

    void resized() override { toggle_.setBounds (getLocalBounds()); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (2);
        auto title = r.removeFromTop (14);

        g.setColour (juce::Colour (kAccent));
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        g.drawText ("MODE", title, juce::Justification::centred);

        auto body = r.reduced (1);
        g.setColour (juce::Colour (0xff121214));
        g.fillRoundedRectangle (body.toFloat(), 5.0f);
        g.setColour (juce::Colour (kPanelLine));
        g.drawRoundedRectangle (body.toFloat(), 5.0f, 1.0f);

        auto drawOption = [&g] (juce::Rectangle<int> area, const juce::String& text, bool active)
        {
            if (active)
            {
                g.setColour (juce::Colour (kAccent).withAlpha (0.17f));
                g.fillRoundedRectangle (area.toFloat().reduced (2.0f, 1.0f), 4.0f);
            }

            g.setColour (active ? juce::Colour (kAccent) : juce::Colour (kTextDim));
            g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
            g.drawText (text, area, juce::Justification::centred);
        };

        auto top = body.removeFromTop (body.getHeight() / 2);
        drawOption (top, "B > N", toggle_.getToggleState());
        drawOption (body, "N > B", ! toggle_.getToggleState());
    }

private:
    InvisibleToggle toggle_;
    std::unique_ptr<APVTS::ButtonAttachment> attachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModeSwitch)
};

//==============================================================================
/// Compact amp-model browse button that sits on the amp faceplate next to Power.
class AmpBrowseButton : public juce::Component,
                        public juce::FileDragAndDropTarget
{
public:
    AmpBrowseButton (std::function<void()> onClick,
                     std::function<void()> onPrevious,
                     std::function<void()> onNext,
                     std::function<void (const juce::File&)> onFileDropped)
        : onFileDropped_ (std::move (onFileDropped))
    {
        addAndMakeVisible (label_);
        label_.setText ("AMP MODEL", juce::dontSendNotification);
        label_.setJustificationType (juce::Justification::centred);
        label_.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        label_.setColour (juce::Label::textColourId, juce::Colour (0xff3a2f18));

        addAndMakeVisible (button_);
        button_.setButtonText ("BROWSE");
        button_.setTooltip ("Browse for a NAM amp model");
        button_.onClick = std::move (onClick);

        addAndMakeVisible (prevButton_);
        prevButton_.setButtonText ("<");
        prevButton_.setTooltip ("Previous .nam file in this folder");
        prevButton_.onClick = std::move (onPrevious);

        addAndMakeVisible (nextButton_);
        nextButton_.setButtonText (">");
        nextButton_.setTooltip ("Next .nam file in this folder");
        nextButton_.onClick = std::move (onNext);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label_.setBounds (r.removeFromTop (16));
        button_.setBounds (r.removeFromTop (44).withSizeKeepingCentre (68, 38));

        auto arrows = r.removeFromTop (24).reduced (2, 1);
        prevButton_.setBounds (arrows.removeFromLeft (33));
        nextButton_.setBounds (arrows.removeFromRight (33));
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        return files.size() == 1 && files[0].endsWithIgnoreCase (".nam");
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

        if (! files.isEmpty() && onFileDropped_ != nullptr)
            onFileDropped_ (juce::File (files[0]));
    }

    void paint (juce::Graphics& g) override
    {
        if (! dragOver_)
            return;

        g.setColour (juce::Colour (0xff2dd4bf));
        g.drawRect (getLocalBounds(), 2);
    }

private:
    std::function<void (const juce::File&)> onFileDropped_;
    juce::Label label_;
    juce::TextButton button_;
    juce::TextButton prevButton_, nextButton_;
    bool dragOver_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpBrowseButton)
};

//==============================================================================
/// Centers two controls in a row while keeping the standard knob footprint.
class CenteredPairRow : public juce::Component
{
public:
    CenteredPairRow (juce::Component* first, juce::Component* second)
        : first_ (first), second_ (second)
    {
        addAndMakeVisible (first_);
        addAndMakeVisible (second_);
    }

    void resized() override
    {
        const int controlWidth = ParamControl::cellWidth;
        const int gap = 8;
        const int totalWidth = controlWidth * 2 + gap;
        const int height = juce::jmin (getHeight(), ParamControl::cellHeight);
        const int x = (getWidth() - totalWidth) / 2;
        const int y = (getHeight() - height) / 2;

        first_->setBounds (x, y, controlWidth, height);
        second_->setBounds (x + controlWidth + gap, y, controlWidth, height);
    }

private:
    juce::Component* first_;
    juce::Component* second_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CenteredPairRow)
};

//==============================================================================
/// A small hardware-style LCD in the amp grille for the current model name.
class AmpModelDisplay : public juce::Component
{
public:
    AmpModelDisplay()
    {
        setInterceptsMouseClicks (false, false);
    }

    void setName (const juce::String& name)
    {
        const auto display = name.isNotEmpty() ? name : juce::String ("No model loaded");

        if (display != name_)
        {
            name_ = display;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (2.0f);

        g.setColour (juce::Colour (0xff070c0b));
        g.fillRoundedRectangle (bounds, 6.0f);

        g.setColour (juce::Colour (0xff2dd4bf).withAlpha (0.55f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.2f);

        g.setColour (juce::Colour (0xff2dd4bf).withAlpha (0.55f));
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText ("CURRENT AMP",
                    bounds.reduced (14.0f).removeFromTop (16.0f),
                    juce::Justification::centredLeft);

        g.setColour (juce::Colour (0xffb8fff1));
        g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        const auto textArea = bounds.reduced (14.0f, 8.0f).withTrimmedTop (16.0f).toNearestInt();
        g.drawFittedText (name_, textArea,
                          juce::Justification::centred, 1, 0.72f);
    }

private:
    juce::String name_ { "No model loaded" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpModelDisplay)
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
                    rowW = juce::jmin (rowW, 460);
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
/// Places the horizontal tuner above the impersonator and pitch pedals,
/// removing the tuner's narrow vertical strip from the main row.
class PreFxTopGroup : public juce::Component
{
public:
    PreFxTopGroup (SectionPanel& tuner, SectionPanel& impersonator, SectionPanel& pitch)
        : tuner_ (tuner), impersonator_ (impersonator), pitch_ (pitch)
    {
        addAndMakeVisible (tuner_);
        addAndMakeVisible (impersonator_);
        addAndMakeVisible (pitch_);
    }

    int preferredWidth() const { return pedalWidth * 2 + pedalGap; }
    int preferredHeight() const { return tunerHeight + pedalGap + pedalHeight; }

    void resized() override
    {
        const int width = getWidth();
        tuner_.setBounds (0, 0, width, tunerHeight);

        const int pedalY = tunerHeight + pedalGap;
        const int height = juce::jmax (1, getHeight() - pedalY);

        impersonator_.setBounds (0, pedalY, pedalWidth, height);
        pitch_.setBounds (pedalWidth + pedalGap, pedalY, pedalWidth, height);
    }

private:
    static constexpr int pedalWidth = 176;
    static constexpr int pedalGap = 8;
    static constexpr int tunerHeight = 130;
    static constexpr int pedalHeight = 340;

    SectionPanel& tuner_;
    SectionPanel& impersonator_;
    SectionPanel& pitch_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreFxTopGroup)
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

    addAndMakeVisible (presetButton_);
    presetButton_.setButtonText ("PRESET");
    presetButton_.setTooltip ("Save or load a NAMBerry preset");
    presetButton_.onClick = [this] { showPresetMenu(); };

    tabBar_.addChangeListener (this);
    tabBar_.addMouseListener (this, true);
    addAndMakeVisible (tabBar_);

    viewport_.setViewedComponent (&content_, false);
    viewport_.setScrollBarsShown (false, true);
    viewport_.setColour (juce::ScrollBar::thumbColourId, juce::Colour (kAccent).withAlpha (0.6f));
    viewport_.setColour (juce::ScrollBar::trackColourId, juce::Colour (kPanel));
    addAndMakeVisible (viewport_);

    buildUi();

    bottomKnobs_ = std::make_unique<BottomKnobs> (processor_.getValueTreeState());
    addAndMakeVisible (*bottomKnobs_);

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
    tabBar_.removeMouseListener (this);
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
                                       juce::Colour body, juce::Colour accent,
                                       bool registerPanel = true) -> SectionPanel*
    {
        auto panel = std::make_unique<SectionPanel> (
            title, style, cols, width, body, accent, reduced);
        auto* raw = panel.get();
        ownedControls_.push_back (std::move (panel));

        if (registerPanel)
        {
            tabPanels_[tabIndex].push_back (raw);
            allPanels_.push_back (raw);
            content_.addAndMakeVisible (raw);
        }

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
            [this, pedal]
            {
                return pedal ? processor_.getPedalDirectory()
                             : processor_.getNamDirectory();
            },
            [this, pedal]
            {
                auto* loader = pedal ? processor_.getNamPedal()
                                     : processor_.getNamModel();
                return loader != nullptr ? loader->getLoadedFile() : juce::File();
            });
        auto* raw = box.get();

        if (auto* loaderNow = pedal ? processor_.getNamPedal() : processor_.getNamModel())
            raw->setDisplayedFileName (loaderNow->getLoadedFileName());

        raw->setOnBrowse ([this, raw, pedal]
        {
            openBrowser (pedal ? BrowserKind::Pedal : BrowserKind::Nam,
                         [raw] (const juce::File& file)
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

    const char* tabNames[] = { "Pre FX", "Amp", "Post EQ", "IR", "Post FX" };

    for (int i = 0; i < tabBar_.getNumTabs() && i < 5; ++i)
        if (auto* button = tabBar_.getTabButton (i))
            button->setTooltip (juce::String (tabNames[i])
                                + " — right-click to bypass this section");

    updateTabVisuals();

    // Unit colours.
    const juce::Colour tunerBody   (0xff0d5c58), tunerAccent   (0xff5eead4);
    const juce::Colour impersonatorBody (0xff4a2e2e), impersonatorAccent (0xfff59e0b);
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
    const juce::Colour roomBody    (0xff4a3a2a), roomAccent    (0xffe0b080);
    const juce::Colour delayBody   (0xff713f12), delayAccent   (0xfffacc15);
    const juce::Colour reverbBody  (0xff1e3a5f), reverbAccent  (0xff93c5fd);

    // 1. Pre FX -------------------------------------------------------------
    auto* tunerPanel = makePanel (0, "TUNER", UnitStyle::RackUnit, 1, 360,
                                  tunerBody, tunerAccent, false);

    if (tunerPanel != nullptr)
    {
        auto strip = std::make_unique<TunerStrip> (state);
        auto* rawStrip = strip.get();
        ownedControls_.push_back (std::move (strip));
        tunerPanel->addRow (rawStrip, 90);
        tunerPanel->setPreferredHeight (130);

        if (auto* t = processor_.getDropTuner())
            rawStrip->setState (t->getCentsDeviation(),
                                t->getDetectedNote(),
                                t->getReferenceFrequency());

        tunerUpdater_ = [this, rawStrip]
        {
            if (auto* tuner = processor_.getDropTuner())
                rawStrip->setState (tuner->getCentsDeviation(),
                                    tuner->getDetectedNote(),
                                    tuner->getReferenceFrequency());
        };
    }

    auto* impersonatorPanel = makePanel (0, "IMPERSONATOR", UnitStyle::Pedal, 2, 0,
                                         impersonatorBody, impersonatorAccent, false);

    if (impersonatorPanel != nullptr)
    {
        auto mode = std::make_unique<ModeSwitch> (state, "impersonatorToNeck");
        auto* rawMode = mode.get();
        ownedControls_.push_back (std::move (mode));
        impersonatorPanel->addItem (rawMode);
        impersonatorPanel->addItem (makeControl ("impersonatorLevel", "Level"));
        impersonatorPanel->setFooter (
            makeFootswitch ("impersonatorBypass", impersonatorAccent), 82);
        impersonatorPanel->setPreferredHeight (300);
    }

    auto* pitchPanel = makePanel (0, "PITCH", UnitStyle::Pedal, 2, 0,
                                  capoBody, capoAccent, false);

    if (pitchPanel != nullptr)
    {
        pitchPanel->addItem (makeControl ("capoFret", "Semitones"));
        pitchPanel->addItem (makeControl ("capoTone", "Tone Match"));
        pitchPanel->addItem (makeControl ("capoMix", "Mix"));
        pitchPanel->addItem (makeControl ("capoQuality", "Quality", ParamControl::Kind::Toggle));
        pitchPanel->setFooter (makeFootswitch ("capoBypass", capoAccent), 82);
        pitchPanel->setPreferredHeight (340);
    }

    if (tunerPanel != nullptr && impersonatorPanel != nullptr && pitchPanel != nullptr)
    {
        auto group = std::make_unique<PreFxTopGroup> (
            *tunerPanel, *impersonatorPanel, *pitchPanel);
        auto* rawGroup = group.get();
        ownedControls_.push_back (std::move (group));
        tabPanels_[0].push_back (rawGroup);
        allPanels_.push_back (rawGroup);
        content_.addAndMakeVisible (rawGroup);
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
    if (auto* amp = makePanel (1, "AMP", UnitStyle::AmpHead, 9, 760,
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

        auto stepAmp = [this] (int delta)
        {
            auto* loader = processor_.getNamModel();

            if (loader == nullptr)
                return;

            const auto current = loader->getLoadedFile();
            const auto directory = current.existsAsFile()
                ? current.getParentDirectory()
                : processor_.getNamDirectory();
            const auto files = listMatchingFiles (directory, juce::StringArray { ".nam" });

            if (files.isEmpty())
                return;

            int index = files.indexOf (current);

            if (index < 0)
                index = delta > 0 ? 0 : files.size() - 1;
            else
                index = (index + delta + files.size()) % files.size();

            processor_.loadNamModel (files[index]);
        };

        auto browse = std::make_unique<AmpBrowseButton> (
            [this]
            {
                openBrowser (BrowserKind::Nam, [this] (const juce::File& file)
                {
                    processor_.loadNamModel (file);
                });
            },
            [stepAmp] { stepAmp (-1); },
            [stepAmp] { stepAmp (1); },
            [this] (const juce::File& file)
            {
                processor_.loadNamModel (file);
            });
        auto* rawBrowse = browse.get();
        ownedControls_.push_back (std::move (browse));

        amp->addItem (rawBrowse);

        amp->addItem (addAmpControl ("ampBypass", "Power", ParamControl::Kind::Toggle, true));
        amp->addItem (addAmpControl ("ampGain", "Gain"));
        amp->addItem (addAmpControl ("ampBass", "Bass"));
        amp->addItem (addAmpControl ("ampMid", "Mid"));
        amp->addItem (addAmpControl ("ampTreble", "Treble"));
        amp->addItem (addAmpControl ("ampPresence", "Presence"));
        amp->addItem (addAmpControl ("ampDepth", "Depth"));
        amp->addItem (addAmpControl ("ampOutput", "Output"));

        auto display = std::make_unique<AmpModelDisplay>();
        auto* rawDisplay = display.get();
        ownedControls_.push_back (std::move (display));

        if (auto* loader = processor_.getNamModel())
            rawDisplay->setName (loader->getLoadedFileName());

        nameUpdaters_.push_back ([this, rawDisplay]
        {
            if (auto* loader = processor_.getNamModel())
                rawDisplay->setName (loader->getLoadedFileName());
        });

        amp->addRow (rawDisplay, 92);
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

        auto passRow = std::make_unique<CenteredPairRow> (
            makeControl ("postEqHpfFreq", "HPF"),
            makeControl ("postEqLpfFreq", "LPF"));
        auto* rawPassRow = passRow.get();
        ownedControls_.push_back (std::move (passRow));
        postEq->addRow (rawPassRow, 84);

        auto* bypass = makeControl ("postEqBypass", "Bypass", ParamControl::Kind::Toggle, true);
        bypass->setLabelFontSize (13.0f);
        postEq->addRow (bypass, 48);

        postEq->setItemCellHeight (84);
        postEq->setPreferredHeight (500);
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
        ir->addRow (rawGrid, 330);

        ir->setPreferredHeight (450);
    }

    // 5. Post FX pedals ------------------------------------------------------
    if (auto* chorus = makePanel (4, "CHORUS", UnitStyle::Pedal, 2, 0,
                                  chorusBody, chorusAccent))
    {
        chorus->addItem (makeControl ("chorusRate", "Rate"));
        chorus->addItem (makeControl ("chorusDepth", "Depth"));
        chorus->addItem (makeControl ("chorusFeedback", "Feedback"));
        chorus->addItem (makeControl ("chorusDelay", "Delay"));
        chorus->addItem (makeControl ("chorusMix", "Mix"));
        chorus->setFooter (makeFootswitch ("chorusBypass", chorusAccent), 82);
    }

    if (auto* flanger = makePanel (4, "FLANGER", UnitStyle::Pedal, 2, 0,
                                   flangerBody, flangerAccent))
    {
        flanger->addItem (makeControl ("flangerRate", "Rate"));
        flanger->addItem (makeControl ("flangerDepth", "Depth"));
        flanger->addItem (makeControl ("flangerManual", "Manual"));
        flanger->addItem (makeControl ("flangerFeedback", "Feedback"));
        flanger->addItem (makeControl ("flangerMix", "Mix"));
        flanger->setFooter (makeFootswitch ("flangerBypass", flangerAccent), 82);
    }

    if (auto* phaser = makePanel (4, "PHASER", UnitStyle::Pedal, 2, 0,
                                  phaserBody, phaserAccent))
    {
        phaser->addItem (makeControl ("phaserRate", "Rate"));
        phaser->addItem (makeControl ("phaserDepth", "Depth"));
        phaser->addItem (makeControl ("phaserFeedback", "Feedback"));

        auto* stages = makeControl ("phaserStages", "Stages");
        stages->setStepSize (4.0);
        phaser->addItem (stages);

        phaser->addItem (makeControl ("phaserMix", "Mix"));
        phaser->setFooter (makeFootswitch ("phaserBypass", phaserAccent), 82);
    }

    if (auto* room = makePanel (4, "ROOM", UnitStyle::Pedal, 1, 176,
                                roomBody, roomAccent))
    {
        room->addItem (makeControl ("roomSize", "Size"));
        room->setPreferredHeight (180);
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
        juce::Component* panel;
        int width;
        int height;
    };

    std::vector<Entry> entries;
    int totalWidth = 0;

    for (auto* panel : tabPanels_[(size_t) tabIndex])
    {
        int width = 0;
        int wantedHeight = 0;

        if (auto* section = dynamic_cast<SectionPanel*> (panel))
        {
            width = section->preferredWidth();

            // The amp head stretches to the full window width.
            if (section->isAmpHead())
                width = juce::jmax (width, viewportWidth - 2 * margin);

            wantedHeight = section->preferredHeight();
        }
        else if (auto* group = dynamic_cast<PreFxTopGroup*> (panel))
        {
            width = group->preferredWidth();
            wantedHeight = group->preferredHeight();
        }
        else
        {
            continue;
        }

        const int height = wantedHeight > 0
            ? juce::jmin (wantedHeight, contentHeight)
            : contentHeight;

        entries.push_back ({ panel, width, height });
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

    const bool amp = kind == BrowserKind::Nam;
    const bool pedal = kind == BrowserKind::Pedal;

    const auto root = amp ? processor_.getNamDirectory()
                      : pedal ? processor_.getPedalDirectory()
                              : processor_.getIrDirectory();

    juce::StringArray extensions;

    if (amp || pedal)
        extensions.add (".nam");
    else
    {
        extensions.add (".wav");
        extensions.add (".aif");
        extensions.add (".aiff");
    }

    browser_->open (amp ? "AMP BROWSER" : pedal ? "PEDAL BROWSER" : "IR BROWSER",
                    extensions,
                    root,
                    [this, amp, pedal] (const juce::File& directory)
                    {
                        if (amp)
                            processor_.setNamDirectory (directory);
                        else if (pedal)
                            processor_.setPedalDirectory (directory);
                        else
                            processor_.setIrDirectory (directory);
                    },
                    std::move (onFileChosen));

    browser_->setBounds (getLocalBounds());
    browser_->setVisible (true);
    browser_->toFront (true);
}

void NAMBerryEditor::showPresetMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Load Preset...");
    menu.addItem (2, "Save Preset...");
    menu.addSeparator();
    menu.addItem (3, "Open Preset Folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton_),
                        [this] (int result)
                        {
                            if (result == 1)
                                loadPresetFromFile();
                            else if (result == 2)
                                savePresetAs();
                            else if (result == 3)
                                processor_.getPresetDirectory().revealToUser();
                        });
}

void NAMBerryEditor::savePresetAs()
{
    const auto directory = processor_.getPresetDirectory();
    const auto startFile = lastPresetFile_.existsAsFile()
        ? lastPresetFile_
        : directory.getChildFile ("NAMberry Preset.namberrypreset");

    presetChooser_ = std::make_unique<juce::FileChooser> (
        "Save NAMBerry Preset", startFile, "*.namberrypreset");

    presetChooser_->launchAsync (
        juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();

            if (file == juce::File())
                return;

            if (! file.hasFileExtension ("namberrypreset"))
                file = file.withFileExtension ("namberrypreset");

            if (processor_.savePreset (file))
            {
                lastPresetFile_ = file;
                presetButton_.setTooltip ("Preset: " + file.getFileName());
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon,
                    "Could not save preset",
                    "NAMberry could not write:\n" + file.getFullPathName());
            }
        });
}

void NAMBerryEditor::loadPresetFromFile()
{
    presetChooser_ = std::make_unique<juce::FileChooser> (
        "Load NAMBerry Preset", processor_.getPresetDirectory(), "*.namberrypreset");

    presetChooser_->launchAsync (
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();

            if (! file.existsAsFile())
                return;

            const bool fullyLoaded = processor_.loadPreset (file);
            lastPresetFile_ = file;
            presetButton_.setTooltip ("Preset: " + file.getFileName());

            if (! fullyLoaded)
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon,
                    "Could not fully load preset",
                    "The preset may be invalid or may reference model/IR "
                    "files that are no longer available.");
            }
        });
}

void NAMBerryEditor::resized()
{
    auto r = getLocalBounds();

    auto header = r.removeFromTop (60);
    auto headerControls = header.removeFromRight (360);
    reducedMotionButton_.setBounds (headerControls.removeFromRight (190).withSizeKeepingCentre (180, 48));
    presetButton_.setBounds (headerControls.removeFromRight (150).withSizeKeepingCentre (140, 44));

    constexpr int bottomHeight = 112;
    constexpr int knobsWidth = 176;
    constexpr int knobsHeight = 108;
    constexpr int tabGap = 12;

    auto tabArea = r.removeFromBottom (bottomHeight);
    const int maxTabWidth = juce::jmax (240, tabArea.getWidth() - knobsWidth - tabGap - 16);

    // Lay the bar out at a generous width once, measure the natural button
    // group, then shrink the bar to that width and place the room/master
    // knobs immediately to its right.
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

    const int totalWidth = barWidth + tabGap + knobsWidth;
    const int startX = tabArea.getX()
        + juce::jmax (0, (tabArea.getWidth() - totalWidth) / 2);
    const int bottomY = tabArea.getBottom();

    tabBar_.setBounds (startX, bottomY - 48, barWidth, 48);

    if (bottomKnobs_ != nullptr)
        bottomKnobs_->setBounds (startX + barWidth + tabGap,
                                 bottomY - knobsHeight,
                                 knobsWidth, knobsHeight);

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
                      header.withTrimmedLeft (200).withTrimmedRight (390),
                      juce::Justification::centred, 1);

    g.setColour (juce::Colour (kAccent).withAlpha (0.5f));
    g.fillRect (getLocalBounds().removeFromTop (60).removeFromBottom (2));
}

void NAMBerryEditor::mouseDown (const juce::MouseEvent& event)
{
    if (! event.mods.isPopupMenu())
        return;

    // Resolve the clicked tab in tab-bar coordinates. The deep listener can
    // receive the event relative to the tab button, not the bar itself.
    const auto position = event.getEventRelativeTo (&tabBar_).getPosition();

    for (int i = 0; i < tabBar_.getNumTabs(); ++i)
    {
        if (auto* button = tabBar_.getTabButton (i))
        {
            if (button->getBounds().contains (position))
            {
                toggleTabBypass (i);
                break;
            }
        }
    }
}

void NAMBerryEditor::toggleTabBypass (int tabIndex)
{
    static const char* ids[] = {
        "preFxBypass", "ampBypass", "postEqBypass", "irBypass", "postFxBypass"
    };

    if (tabIndex < 0 || tabIndex >= 5)
        return;

    auto& state = processor_.getValueTreeState();

    if (auto* parameter = state.getParameter (ids[tabIndex]))
    {
        const bool bypassed = parameter->getValue() > 0.5f;
        parameter->setValueNotifyingHost (bypassed ? 0.0f : 1.0f);
    }

    updateTabVisuals();
}

void NAMBerryEditor::updateTabVisuals()
{
    static const char* ids[] = {
        "preFxBypass", "ampBypass", "postEqBypass", "irBypass", "postFxBypass"
    };

    auto& state = processor_.getValueTreeState();

    for (int i = 0; i < tabBar_.getNumTabs() && i < 5; ++i)
    {
        auto* button = tabBar_.getTabButton (i);
        auto* value = state.getRawParameterValue (ids[i]);

        if (button == nullptr || value == nullptr)
            continue;

        const bool bypassed = value->load() > 0.5f;
        const bool wasBypassed = static_cast<bool> (
            button->getProperties().getWithDefault ("bypassed", false));

        if (bypassed != wasBypassed)
        {
            button->getProperties().set ("bypassed", bypassed);
            button->repaint();
        }
    }
}

void NAMBerryEditor::timerCallback()
{
    if (tunerUpdater_)
        tunerUpdater_();

    for (auto& updater : nameUpdaters_)
        updater();

    updateTabVisuals();
}

} // namespace namberry
