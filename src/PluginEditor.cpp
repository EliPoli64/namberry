#include "PluginEditor.h"

#include "namberry/PostAmpEQ.h"
#include "namberry/SignalSpectrum.h"

#include <BinaryData.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <utility>

namespace namberry {

namespace {

//==============================================================================
// Palette — light studio bench with the brand indigo (#0E0052) and white.
// The gear carries the colour: gold, tolex, brushed metal and one bright
// die-cast hue per effect. Dark LCD readouts keep text contrast high.
constexpr juce::uint32 kBg              = 0xffe4e7f1;
constexpr juce::uint32 kPanel           = 0xfff5f6fa;
constexpr juce::uint32 kSurfaceRaised   = 0xff0e0052;
constexpr juce::uint32 kPanelLine       = 0xffbfc5da;
constexpr juce::uint32 kPanelLineStrong = 0xff8f96b3;
constexpr juce::uint32 kKnobBody        = 0xff2b2b33;
constexpr juce::uint32 kAccent          = 0xff0e0052;
constexpr juce::uint32 kText            = 0xff191430;
constexpr juce::uint32 kTextDim         = 0xff4c4870;

// Dark LCD screens (filename readouts, tuner, EQ graph): bright text on
// near-black reads clearly on every chassis colour.
constexpr juce::uint32 kScreen       = 0xff12151c;
constexpr juce::uint32 kScreenLine   = 0xff2d3345;
constexpr juce::uint32 kScreenText   = 0xffe8ecf6;
constexpr juce::uint32 kScreenDim    = 0xff8d95b2;
constexpr juce::uint32 kScreenGlow   = 0xff9db4ff;
// Header wordmark, embedded from src/logoNameColored.svg. The SVG is parsed
// once into a Drawable so it stays crisp at any header size.
const juce::Drawable* headerLogoNameColored()
{
    static const std::unique_ptr<juce::Drawable> logo = []
    {
        return juce::Drawable::createFromImageData (
            BinaryData::logoNameColored_svg, (size_t) BinaryData::logoNameColored_svgSize);
    }();
    return logo.get();
}

/// Baumans typeface used for every label in the editor.
static juce::Typeface::Ptr baumansTypeface()
{
    static juce::Typeface::Ptr typeface = juce::Typeface::createSystemTypefaceFor (
        BinaryData::BaumansRegular_ttf, (size_t) BinaryData::BaumansRegular_ttfSize);
    return typeface;
}

static juce::Font baumansFont (float size)
{
    if (auto tf = baumansTypeface())
        return juce::Font (juce::FontOptions (tf).withHeight (size));
    return juce::Font (juce::FontOptions (size));
}

/// Seven-segment typeface used for NAM/IR selection displays.
static juce::Typeface::Ptr sevenSegmentTypeface()
{
    static juce::Typeface::Ptr typeface = juce::Typeface::createSystemTypefaceFor (
        BinaryData::SevenSegment_ttf, (size_t) BinaryData::SevenSegment_ttfSize);
    return typeface;
}

static juce::Font sevenSegmentFont (float size)
{
    if (auto tf = sevenSegmentTypeface())
        return juce::Font (juce::FontOptions (tf).withHeight (size));
    return juce::Font (juce::FontOptions (size));
}

/// Square app icon rendered from src/icon.svg for the standalone window.
const juce::Image& appIconImage()
{
    static const juce::Image icon = []
    {
        const std::unique_ptr<juce::Drawable> drawable =
            juce::Drawable::createFromImageData (
                BinaryData::icon_svg, (size_t) BinaryData::icon_svgSize);

        juce::Image image (juce::Image::ARGB, 256, 256, true);

        if (drawable != nullptr)
        {
            juce::Graphics g (image);
            drawable->drawWithin (g, image.getBounds().toFloat(),
                                  juce::RectanglePlacement::centred, 1.0f);
        }

        return image;
    }();

    return icon;
}

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

    // Fine horizontal brushing.
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    for (float y = b.getY() + 2.0f; y < b.getBottom(); y += 3.0f)
        g.drawHorizontalLine ((int) y, b.getX() + 2.0f, b.getRight() - 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.05f));
    for (float y = b.getY() + 3.5f; y < b.getBottom(); y += 3.0f)
        g.drawHorizontalLine ((int) y, b.getX() + 2.0f, b.getRight() - 2.0f);
}

//==============================================================================
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

void showLoadFailure (const juce::String& kind, const juce::File& file)
{
    juce::AlertWindow::showMessageBoxAsync (
        juce::MessageBoxIconType::WarningIcon,
        "Could not load " + kind,
        "NAMberry could not load: " + file.getFileName());
}

/// The folder a picker should list: the loaded file's folder, or the fallback.
juce::File folderOf (const juce::File& current, const juce::File& fallback)
{
    return current.existsAsFile() ? current.getParentDirectory() : fallback;
}

//==============================================================================
/// Metal knobs, toggle switches and brushed buttons for the whole editor.
class HardwareLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HardwareLookAndFeel()
    {
        auto scheme = getLightColourScheme();
        scheme.setUIColour (ColourScheme::windowBackground, juce::Colour (kBg));
        scheme.setUIColour (ColourScheme::widgetBackground, juce::Colour (kPanel));
        setColourScheme (scheme);

        setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (kAccent));
        setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (kPanelLine));
        setColour (juce::Slider::thumbColourId, juce::Colour (kAccent));
        setColour (juce::Label::textColourId, juce::Colour (kText));
        setColour (juce::ToggleButton::textColourId, juce::Colour (kText));
        setColour (juce::TextButton::buttonColourId, juce::Colour (0xffeef0f6));
        setColour (juce::TextButton::buttonOnColourId, juce::Colour (kSurfaceRaised));
        setColour (juce::TextButton::textColourOffId, juce::Colour (kText));
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::TabbedButtonBar::tabOutlineColourId, juce::Colour (kPanelLine));
        setColour (juce::TabbedButtonBar::frontTextColourId, juce::Colours::white);
        setColour (juce::TabbedButtonBar::tabTextColourId, juce::Colour (kText));
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
        const bool accentKnob = static_cast<int> (
            slider.getProperties().getWithDefault ("accentKnob", 0)) != 0;

        // Per-unit accent: the LED hue of the pedal this knob lives on.
        const auto accent = juce::Colour ((juce::uint32) (juce::int64)
            slider.getProperties().getWithDefault ("knobAccent", (juce::int64) kAccent));

        // Tick ring.
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        for (int i = 0; i <= 10; ++i)
        {
            const auto a = rotaryStartAngle
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
            juce::Colour (0xff54545c), centre.x, skirt.getY(),
            juce::Colour (0xff18181b), centre.x, skirt.getBottom(), false));
        g.fillEllipse (skirt);
        g.setColour (juce::Colour (0xff0b0b0d));
        g.drawEllipse (skirt, 1.3f);

        // Cap.
        auto cap = skirt.reduced (radius * 0.17f);
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xff6a6a74), cap.getX(), cap.getY(),
            juce::Colour (0xff242428), cap.getX(), cap.getBottom(), false));
        g.fillEllipse (cap);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawEllipse (cap.reduced (1.0f), 1.0f);

        // Subtle accent rim so every knob ties to its chassis colour.
        g.setColour (accent.withAlpha (0.55f));
        g.drawEllipse (skirt.reduced (radius * 0.06f), 1.4f);

        // Accent ring for primary controls.
        if (accentKnob)
        {
            g.setColour (accent.withAlpha (0.95f));
            g.drawEllipse (skirt.expanded (3.0f), 2.4f);
        }

        // Pointer.
        const auto dir = juce::Point<float> (std::sin (angle), -std::cos (angle));
        const auto p1 = centre + dir * (radius * 0.20f);
        const auto p2 = centre + dir * (radius * 0.86f);
        g.setColour (accentKnob ? accent : juce::Colour (0xfff4f4f5));
        g.drawLine ({ p1, p2 }, accentKnob ? 3.4f : 2.6f);

        if (slider.hasKeyboardFocus (true))
        {
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.drawEllipse (skirt.expanded (2.5f), 1.5f);
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.drawEllipse (skirt.expanded (4.0f), 1.0f);
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
                            : bounds.removeFromLeft (38.0f);

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
            g.setColour (button.findColour (juce::ToggleButton::textColourId));
            g.setFont (baumansFont (12.0f));
            g.drawFittedText (text, bounds.toNearestInt(),
                              juce::Justification::centredLeft, 1);
        }

        if (button.hasKeyboardFocus (true))
        {
            g.setColour (juce::Colours::white.withAlpha (0.9f));
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
            base.brighter (0.12f), bounds.getX(), bounds.getY(),
            base.darker (0.16f), bounds.getX(), bounds.getBottom(), false));
        g.fillRoundedRectangle (bounds, 6.0f);

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (bounds, 6.0f, 1.0f);

        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawHorizontalLine ((int) bounds.getY() + 2,
                              bounds.getX() + 6.0f, bounds.getRight() - 6.0f);
    }

    juce::Font getLabelFont (juce::Label& label) override
    {
        // Bigger numeric readouts for the rack controls.
        return baumansFont (juce::jlimit (12.0f, 17.0f,
                                          (float) label.getHeight() * 0.85f));
    }

    int getTabButtonBestWidth (juce::TabBarButton&, int tabDepth) override
    {
        // Compact horizontal tab size for the top tab bar.
        return tabDepth + 92;
    }

    void drawTabButton (juce::TabBarButton& button, juce::Graphics& g,
                        bool isMouseOver, bool isMouseDown) override
    {
        auto bounds = button.getLocalBounds().toFloat();
        const bool front = button.isFrontTab();

        const bool bypassed = static_cast<bool> (
            button.getProperties().getWithDefault ("bypassed", false));

        auto body = front ? juce::Colour (kSurfaceRaised) : juce::Colour (kPanel);
        if (isMouseOver && ! front) body = body.brighter (0.06f);
        if (isMouseDown)            body = body.darker (0.10f);
        if (bypassed)               body = body.darker (0.10f);

        g.setGradientFill (juce::ColourGradient (
            body.brighter (front ? 0.16f : 0.04f), bounds.getX(), bounds.getY(),
            body.darker (front ? 0.10f : 0.04f), bounds.getX(), bounds.getBottom(), false));
        g.fillRoundedRectangle (bounds.reduced (3.0f, 4.0f), 8.0f);

        if (! front)
        {
            g.setColour (juce::Colour (kPanelLine));
            g.drawRoundedRectangle (bounds.reduced (3.0f, 4.0f), 8.0f, 1.0f);
        }

        // Active indicator: a clean white line inside the chip.
        if (front)
        {
            auto indicator = bounds.reduced (12.0f, 4.0f).removeFromBottom (3.0f);
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.fillRoundedRectangle (indicator, 1.5f);
        }

        auto content = bounds.reduced (8.0f, 5.0f);
        auto iconArea = content.removeFromTop (content.getHeight() * 0.55f);
        auto textArea = content;

        auto iconColour = front ? juce::Colours::white : juce::Colour (kTextDim);
        if (bypassed)
            iconColour = iconColour.withAlpha (0.35f);

        g.setColour (iconColour);

        const auto text = button.getButtonText();

        if (text == "Pre FX")
        {
            auto pedal = iconArea.withSizeKeepingCentre (18.0f, 22.0f);
            g.drawRoundedRectangle (pedal, 2.0f, 1.2f);
            g.fillEllipse (pedal.getCentreX() - 5.0f, pedal.getY() + 3.5f, 3.2f, 3.2f);
            g.fillEllipse (pedal.getCentreX() + 1.8f, pedal.getY() + 3.5f, 3.2f, 3.2f);
            g.drawEllipse (pedal.getCentreX() - 3.0f, pedal.getBottom() - 7.0f,
                           6.0f, 4.0f, 1.0f);
        }
        else if (text == "Amp")
        {
            auto head = iconArea.withSizeKeepingCentre (24.0f, 15.0f);
            g.drawRoundedRectangle (head, 2.0f, 1.2f);
            g.drawHorizontalLine ((int) (head.getY() + 3.0f),
                                  head.getX() + 3.0f, head.getRight() - 3.0f);
            for (int i = 0; i < 3; ++i)
                g.fillEllipse (head.getX() + 5.0f + i * 5.0f,
                               head.getY() + 6.0f, 2.6f, 2.6f);
            g.drawRect (head.getX() + 3.0f, head.getBottom() - 4.0f,
                        head.getWidth() - 6.0f, 2.0f, 0.7f);
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
            auto cab = iconArea.withSizeKeepingCentre (20.0f, 24.0f);
            g.drawRoundedRectangle (cab, 2.0f, 1.2f);
            const auto cone = cab.reduced (4.0f, 4.0f);
            g.drawEllipse (cone, 1.0f);
            g.fillEllipse (cone.getCentreX() - 2.0f, cone.getCentreY() - 2.0f,
                           4.0f, 4.0f);
        }
        else
        {
            juce::Path spiral;
            const auto centre = iconArea.getCentre();
            const float maxRadius = juce::jmin (iconArea.getWidth(), iconArea.getHeight()) * 0.38f;
            float angle = 0.0f;
            float radius = 1.5f;
            bool started = false;

            while (radius < maxRadius)
            {
                const auto p = centre + juce::Point<float> (std::sin (angle) * radius,
                                                            -std::cos (angle) * radius);

                if (! started)
                {
                    spiral.startNewSubPath (p);
                    started = true;
                }
                else
                {
                    spiral.lineTo (p);
                }

                angle += 0.95f;
                radius += 0.8f;
            }

            g.strokePath (spiral, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved));
        }

        auto textColour = front ? juce::Colours::white : juce::Colour (kText);
        if (bypassed)
            textColour = textColour.withAlpha (0.35f);

        g.setColour (textColour);
        g.setFont (baumansFont (11.0f));
        g.drawFittedText (text, textArea.toNearestInt(),
                          juce::Justification::centred, 1);

        if (bypassed)
        {
            g.setColour (juce::Colour (0xffef4444));
            g.fillEllipse (bounds.getRight() - 10.0f, bounds.getY() + 6.0f, 5.0f, 5.0f);
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
        label_.setFont (baumansFont (11.0f));
        label_.setColour (juce::Label::textColourId, juce::Colour (kScreenDim));

        addAndMakeVisible (editor_);
        editor_.setMultiLine (false);
        editor_.setJustification (juce::Justification::centred);
        editor_.setFont (baumansFont (18.0f));
        editor_.setInputRestrictions (7, "0123456789.");
        editor_.setSelectAllWhenFocused (true);
        editor_.setColour (juce::TextEditor::backgroundColourId,
                           juce::Colour (kScreen));
        editor_.setColour (juce::TextEditor::textColourId,
                           juce::Colour (kScreenText));
        editor_.setColour (juce::TextEditor::outlineColourId,
                           juce::Colour (kScreenLine));
        editor_.setColour (juce::TextEditor::focusedOutlineColourId,
                           juce::Colour (kScreenGlow));
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

        editor_.setBounds (r.withSizeKeepingCentre (
            juce::jmin (120, r.getWidth()),
            juce::jmin (42, r.getHeight())));
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
/// Compact strobe tuning guide that lives permanently in the editor header.
class TunerStrip : public juce::Component
{
public:
    explicit TunerStrip (APVTS& state)
    {
        reference_ = std::make_unique<ReferenceField> (state, "tuningReference", "Ref Hz");
        addAndMakeVisible (*reference_);

        reducedMotionParam_ = state.getRawParameterValue ("reducedMotion");
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
        auto referenceArea = r.removeFromRight (112);
        reference_->setBounds (referenceArea.withSizeKeepingCentre (108, 46));
    }

    void paint (juce::Graphics& g) override
    {
        auto bg = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (kScreen));
        g.fillRoundedRectangle (bg, 6.0f);
        g.setColour (juce::Colour (kScreenLine));
        g.drawRoundedRectangle (bg, 6.0f, 1.0f);

        auto r = getLocalBounds().reduced (6, 2);
        r.removeFromRight (108);
        auto noteArea = r.removeFromLeft (62);
        auto guide = r.reduced (2, 4);

        const auto tuning = tuningColour();


        g.setColour (tuning);
        g.setFont (baumansFont (20.0f));
        g.drawFittedText (hasNote_ ? note_ : "---",
                          noteArea.removeFromTop (23),
                          juce::Justification::centredLeft, 1);

        juce::String centsText = "---";

        if (hasNote_)
            centsText = std::abs (targetCents_) < 2.0f
                ? "IN TUNE"
                : juce::String (targetCents_, 1) + " c";

        g.setColour (juce::Colour (kScreenDim));
        g.setFont (baumansFont (9.5f));
        g.drawFittedText (centsText, noteArea.removeFromTop (14),
                          juce::Justification::centredLeft, 1);

        g.setColour (juce::Colour (kScreen));
        g.fillRoundedRectangle (guide.toFloat(), 5.0f);
        g.setColour (juce::Colour (kScreenLine));
        g.drawRoundedRectangle (guide.toFloat(), 5.0f, 1.0f);

        auto inner = guide.reduced (7, 4);

        const float centreX = inner.getCentreX();
        const float halfWidth = inner.getWidth() * 0.44f;
        const float trackY = inner.getCentreY();

        g.setColour (juce::Colour (0xff3b4360));
        g.drawLine (static_cast<float> (inner.getX()), trackY,
                    static_cast<float> (inner.getRight()), trackY, 1.0f);

        for (int c = -50; c <= 50; c += 10)
        {
            const float x = centreX + (float) c / 50.0f * halfWidth;
            const bool major = (c == 0 || std::abs (c) == 50);
            g.setColour (major ? juce::Colour (kScreenText) : juce::Colour (0xff525b7d));
            g.drawVerticalLine ((int) std::round (x),
                                major ? trackY - 8.0f : trackY - 4.0f,
                                major ? trackY + 8.0f : trackY + 4.0f);
        }

        // Moving strobe bands: they settle around the centre pointer when the
        // note is in tune. Skipped in Reduced Motion / Performance mode to save
        // a little paint CPU each frame.
        if (reducedMotionParam_ == nullptr || reducedMotionParam_->load() <= 0.5f)
        {
            const float bandOffset = displayCents_ * 1.35f;
            for (int band = -8; band <= 8; ++band)
            {
                const float x = centreX + band * 11.0f + bandOffset;

                if (x < inner.getX() || x > inner.getRight())
                    continue;

                g.setColour (tuning.withAlpha (hasNote_ ? 0.55f : 0.15f));
                g.drawVerticalLine ((int) std::round (x), trackY - 10.0f, trackY + 10.0f);
            }
        }

        const float needleX = juce::jlimit (static_cast<float> (inner.getX()),
                                            static_cast<float> (inner.getRight()),
                                            centreX + (targetCents_ / 50.0f) * halfWidth);
        g.setColour (tuning);
        // Thicker current-pitch needle (a thin filled rect rather than a 1px line).
        g.fillRect (needleX - 1.5f, trackY - 12.0f, 3.0f, 24.0f);

        // Two arrows that face into the centre line, one from the top and one
        // from the bottom. The old single arrow pointed down and away from the
        // line; these point inward so they bracket the needle.
        juce::Path topArrow;
        topArrow.addTriangle (centreX - 4.0f, inner.getY() + 1.0f,
                              centreX + 4.0f, inner.getY() + 1.0f,
                              centreX, inner.getY() + 8.0f);
        juce::Path bottomArrow;
        bottomArrow.addTriangle (centreX - 4.0f, inner.getBottom() - 1.0f,
                                 centreX + 4.0f, inner.getBottom() - 1.0f,
                                 centreX, inner.getBottom() - 8.0f);
        g.setColour (juce::Colour (0xfff4f4f5));
        g.fillPath (topArrow);
        g.fillPath (bottomArrow);

        g.setColour (juce::Colour (kScreenDim));
        g.setFont (baumansFont (8.0f));
        const float labelY = inner.getBottom() - 11.0f;
        g.drawText ("-50", juce::Rectangle<float> (inner.getX(), labelY, 20.0f, 10.0f),
                    juce::Justification::centredLeft);
        // The "0" stays centred on the needle/arrow pair regardless of the
        // left/right trims used for "-50" and "+50" above.
        g.drawText ("0", juce::Rectangle<float> (centreX - 10.0f, labelY, 20.0f, 10.0f),
                    juce::Justification::centred);
        g.drawText ("+50", juce::Rectangle<float> (inner.getRight() - 24.0f, labelY, 24.0f, 10.0f),
                    juce::Justification::centredRight);
    }

private:
    juce::Colour tuningColour() const
    {
        if (! hasNote_)
            return juce::Colour (kScreenDim);

        if (std::abs (targetCents_) < 2.0f)
            return juce::Colour (0xff4ade80);

        if (std::abs (targetCents_) < 10.0f)
            return juce::Colour (0xfffbbf24);

        return juce::Colour (0xfff87171);
    }

    std::unique_ptr<ReferenceField> reference_;
    const std::atomic<float>* reducedMotionParam_ = nullptr;
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
        label_.setFont (baumansFont (14.5f));
        label_.setColour (juce::Label::textColourId, juce::Colours::white);

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
            slider_.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
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
        preferDarkText_ = dark;
        label_.setColour (juce::Label::textColourId,
                          dark ? juce::Colours::black : juce::Colours::white);
        slider_.setColour (juce::Slider::textBoxTextColourId,
                           dark ? juce::Colours::black : juce::Colours::white);
    }

    /// Adopts the chassis colour of the unit it lives on: white lettering on
    /// dark die-cast bodies, dark lettering on light metal and gold panels.
    /// The accent becomes the knob ring / pointer colour.
    void setUnitTheme (juce::Colour body, juce::Colour accent)
    {
        juce::ignoreUnused (body);
        const auto textColour = preferDarkText_ ? juce::Colours::black : juce::Colours::white;
        label_.setColour (juce::Label::textColourId, textColour);
        slider_.setColour (juce::Slider::textBoxTextColourId, textColour);
        slider_.getProperties().set ("knobAccent", (juce::int64) accent.getARGB());
    }

    void setLabelFontSize (float size)
    {
        label_.setFont (baumansFont (size));
    }

    void setStepSize (double step)
    {
        if (sliderAttachment_ != nullptr)
            slider_.setRange (slider_.getMinimum(), slider_.getMaximum(), step);
    }

    /// Highlights a control that is the primary way to shape this pedal.
    void setAccentKnob()
    {
        slider_.getProperties().set ("accentKnob", 1);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label_.setBounds (r.removeFromTop (18));

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
    bool preferDarkText_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParamControl)
};

//==============================================================================
/// Compact always-visible room and master-output knobs beside the tabs.
class BottomKnobs : public juce::Component
{
public:
    explicit BottomKnobs (APVTS& state)
    {
        roomKnob_ = std::make_unique<ParamControl> (state, "roomAmount", "Room");
        outputKnob_ = std::make_unique<ParamControl> (state, "masterOutput", "Master");
        roomKnob_->setDarkText (true);
        outputKnob_->setDarkText (true);
        addAndMakeVisible (*roomKnob_);
        addAndMakeVisible (*outputKnob_);
    }

    int getPreferredHeight() const { return ParamControl::cellHeight; }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (kPanel));
        g.fillRoundedRectangle (bounds, 7.0f);
        g.setColour (juce::Colour (kPanelLine));
        g.drawRoundedRectangle (bounds, 7.0f, 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4, 2);
        constexpr int cellWidth = ParamControl::cellWidth;
        auto controls = r.withSizeKeepingCentre (cellWidth * 2, r.getHeight());
        roomKnob_->setBounds (controls.removeFromLeft (cellWidth));
        outputKnob_->setBounds (controls.removeFromLeft (cellWidth));
    }

private:
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
        // One light plate over the whole switch so the caption stays legible
        // on any die-cast body colour.
        auto plate = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (kPanel));
        g.fillRoundedRectangle (plate, 5.0f);
        g.setColour (juce::Colour (kPanelLineStrong));
        g.drawRoundedRectangle (plate, 5.0f, 1.0f);

        auto r = getLocalBounds().reduced (2);
        auto title = r.removeFromTop (18);

        g.setColour (juce::Colour (kTextDim));
        g.setFont (baumansFont (13.0f));
        g.drawText ("MODE", title, juce::Justification::centred);

        auto body = r.reduced (1);

        auto drawOption = [&g] (juce::Rectangle<int> area, const juce::String& text, bool active)
        {
            if (active)
            {
                g.setColour (juce::Colour (kAccent).withAlpha (0.17f));
                g.fillRoundedRectangle (area.toFloat().reduced (2.0f, 1.0f), 4.0f);
            }

            g.setColour (active ? juce::Colour (kAccent) : juce::Colour (kTextDim));
            g.setFont (baumansFont (13.0f));
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
/// Four-position mode selector for the Acoustic Sim (Standard, Jumbo,
/// Enhanced, Piezo), bound to an AudioParameterChoice.
class AcousticModeSwitch : public juce::Component
{
public:
    AcousticModeSwitch (APVTS& state, const juce::String& paramId)
    {
        if (auto* param = state.getParameter (paramId))
        {
            param_ = param;
            attachment_ = std::make_unique<juce::ParameterAttachment> (
                *param, [this] (float) { repaint(); });
            attachment_->sendInitialUpdate();
        }

        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        const int index = optionAt (event.getPosition());

        if (index >= 0 && attachment_ != nullptr)
            attachment_->setValueAsCompleteGesture (static_cast<float> (index));
    }

    void paint (juce::Graphics& g) override
    {
        // Light plate so the selector reads on any die-cast body colour.
        auto plate = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (kPanel));
        g.fillRoundedRectangle (plate, 5.0f);
        g.setColour (juce::Colour (kPanelLineStrong));
        g.drawRoundedRectangle (plate, 5.0f, 1.0f);

        auto r = getLocalBounds().reduced (2);
        auto title = r.removeFromTop (18);

        g.setColour (juce::Colour (kTextDim));
        g.setFont (baumansFont (13.0f));
        g.drawText ("MODE", title, juce::Justification::centred);

        auto body = r.reduced (1);

        const int active = currentIndex();

        for (int i = 0; i < numOptions; ++i)
        {
            auto area = body.removeFromTop (body.getHeight() / (numOptions - i));
            const bool isActive = (i == active);

            if (isActive)
            {
                g.setColour (juce::Colour (kAccent).withAlpha (0.17f));
                g.fillRoundedRectangle (area.toFloat().reduced (2.0f, 1.0f), 4.0f);
            }

            g.setColour (isActive ? juce::Colour (kAccent) : juce::Colour (kTextDim));
            g.setFont (baumansFont (13.0f));
            g.drawText (options_[i], area, juce::Justification::centred);
        }
    }

private:
    static constexpr int numOptions = 4;
    static constexpr const char* options_[numOptions] = {
        "Standard", "Jumbo", "Enhanced", "Piezo"
    };

    /// Replicates the paint layout so clicks hit the option under the cursor.
    int optionAt (juce::Point<int> position) const
    {
        auto r = getLocalBounds().reduced (2);
        r.removeFromTop (18);
        auto body = r.reduced (1);

        for (int i = 0; i < numOptions; ++i)
        {
            auto area = body.removeFromTop (body.getHeight() / (numOptions - i));

            if (area.contains (position))
                return i;
        }

        return -1;
    }

    int currentIndex() const
    {
        if (param_ == nullptr)
            return 0;

        return juce::roundToInt (param_->convertFrom0to1 (param_->getValue()));
    }

    juce::RangedAudioParameter* param_ = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AcousticModeSwitch)
};

//==============================================================================
/// Compact amp-model browse button that sits under the grille screen.
class AmpBrowseButton : public juce::Component,
                        public juce::FileDragAndDropTarget
{
public:
    AmpBrowseButton (std::function<void()> onClick,
                     std::function<void (const juce::File&)> onFileDropped)
        : onFileDropped_ (std::move (onFileDropped))
    {
        addAndMakeVisible (button_);
        button_.setButtonText ("LOAD");
        button_.setTooltip ("Choose a NAM amp model from disk");
        button_.onClick = std::move (onClick);
    }

    void resized() override
    {
        button_.setBounds (getLocalBounds().withSizeKeepingCentre (86, 34));
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

        g.setColour (juce::Colour (kAccent));
        g.drawRect (getLocalBounds(), 2);
    }

private:
    std::function<void (const juce::File&)> onFileDropped_;
    juce::TextButton button_;
    bool dragOver_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpBrowseButton)
};

//==============================================================================
/// Hardware-style filename LCD. Clicking it opens the popup that lists the
/// files in the folder, so the screen doubles as the file picker.
class FileScreen : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    FileScreen (juce::String caption, juce::String placeholder)
        : caption_ (std::move (caption)), placeholder_ (std::move (placeholder))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip (caption.isNotEmpty() ? caption + " — click to list the folder"
                                        : "Click to list the folder");
    }

    void setOnOpenPopup (std::function<void (juce::Component&)> callback)
    {
        onOpenPopup_ = std::move (callback);
    }

    void setFileName (const juce::String& name)
    {
        const auto display = name.isNotEmpty() ? name : placeholder_;
        const bool hasFile = name.isNotEmpty();

        if (display != text_ || hasFile != hasFile_)
        {
            text_ = display;
            hasFile_ = hasFile;
            repaint();
        }
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (event.mods.isLeftButtonDown() && onOpenPopup_ != nullptr)
            onOpenPopup_ (*this);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.5f);

        g.setColour (juce::Colour (kScreen));
        g.fillRoundedRectangle (bounds, 5.0f);

        g.setColour (juce::Colour (kScreenLine)
                     .withAlpha (hasFile_ ? 1.0f : 0.55f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);

        auto content = bounds.reduced (10.0f, 6.0f);

        if (caption_.isNotEmpty())
        {
            g.setColour (juce::Colour (kScreenDim));
            g.setFont (baumansFont (9.0f));
            g.drawText (caption_, content.toNearestInt().removeFromTop (14),
                        juce::Justification::centredLeft);
        }

        g.setColour (hasFile_ ? juce::Colour (kScreenText)
                              : juce::Colour (0xff59617e));
        g.setFont (sevenSegmentFont (juce::jlimit (12.0f, 24.0f, content.getHeight() * 0.60f)));
        g.drawFittedText (text_, content.toNearestInt(),
                          juce::Justification::centred, 1, 0.72f);
    }

private:
    juce::String caption_;
    juce::String placeholder_;
    juce::String text_;
    bool hasFile_ = false;
    std::function<void (juce::Component&)> onOpenPopup_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FileScreen)
};

//==============================================================================
/// Chrome stomp switch + status LED, bound to a unit's bypass parameter.
class Footswitch : public juce::Component
{
public:
    Footswitch (APVTS& state, const juce::String& paramId, juce::Colour ledColour,
                juce::Colour bodyColour = juce::Colour (0xff191430),
                bool invertState = true)
        : ledColour_ (ledColour), bodyColour_ (bodyColour), invert_ (invertState)
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
        r.removeFromTop (26);
        button_.setBounds (r.withSizeKeepingCentre (52, 52));
    }

    void paint (juce::Graphics& g) override
    {
        const bool on = invert_ ? ! button_.getToggleState()
                                : button_.getToggleState();

        auto ledArea = getLocalBounds().removeFromTop (26);
        const auto ledCentre = ledArea.getCentre().toFloat();

        if (on)
        {
            g.setColour (ledColour_.withAlpha (0.45f));
            g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (ledCentre));
        }

        g.setColour (on ? ledColour_ : juce::Colour (0xff2b2b33));
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (ledCentre));
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (ledCentre), 1.0f);


        auto sw = getLocalBounds().withTrimmedTop (26)
                      .withSizeKeepingCentre (52, 52).toFloat();

        // Chrome stomp switch.
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
            on ? juce::Colour (0xff5c5c66) : juce::Colour (0xff414147),
            cap.getCentreX(), cap.getY(),
            juce::Colour (0xff151517), cap.getCentreX(), cap.getBottom(), false));
        g.fillEllipse (cap);

        auto inner = cap.reduced (cap.getWidth() * 0.24f);
        g.setColour (on ? ledColour_.withAlpha (0.45f) : juce::Colour (0xff232327));
        g.fillEllipse (inner);
        g.setColour (juce::Colour (0xff55555e));
        g.drawEllipse (inner, 1.0f);
    }

private:
    InvisibleToggle button_;
    std::unique_ptr<APVTS::ButtonAttachment> attachment_;
    juce::Colour ledColour_;
    juce::Colour bodyColour_;
    bool invert_ = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Footswitch)
};

//==============================================================================
/// Footer row that pairs the bypass footswitch with one extra knob, for pedals
/// that carry a dedicated output level.
class FooterWithKnob : public juce::Component
{
public:
    FooterWithKnob (juce::Component& footswitch, juce::Component& knob)
        : footswitch_ (footswitch), knob_ (knob)
    {
        addAndMakeVisible (footswitch_);
        addAndMakeVisible (knob_);
    }

    static constexpr int preferredWidth = ParamControl::cellWidth + 8 + 64;

    /// The wrapped knob, so the owner can apply its unit theme.
    juce::Component* knobControl() const { return &knob_; }

    void resized() override
    {
        auto r = getLocalBounds();
        knob_.setBounds (r.removeFromRight (ParamControl::cellWidth));
        footswitch_.setBounds (r.withSizeKeepingCentre (64, r.getHeight()));
    }

private:
    juce::Component& footswitch_;
    juce::Component& knob_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FooterWithKnob)
};

//==============================================================================
/// A labelled metal knob that can be retargeted to the focused EQ band.
class FocusKnob : public juce::Component
{
public:
    FocusKnob (const juce::String& knobName, const juce::String& labelText,
               const juce::String& suffix, double minimum, double maximum,
               double interval, double defaultValue)
    {
        setName (knobName);

        addAndMakeVisible (label_);
        label_.setText (labelText, juce::dontSendNotification);
        label_.setJustificationType (juce::Justification::centred);
        label_.setFont (baumansFont (11.0f));
        label_.setColour (juce::Label::textColourId, juce::Colour (kTextDim));

        addAndMakeVisible (slider_);
        slider_.setName (knobName + "Slider");
        slider_.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider_.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 18);
        slider_.setColour (juce::Slider::textBoxTextColourId, juce::Colour (kTextDim));
        slider_.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider_.setNumDecimalPlacesToDisplay (interval < 0.1 ? 2 : 1);
        slider_.setTextValueSuffix (suffix);
        slider_.setRange (minimum, maximum, interval);
        slider_.setValue (defaultValue, juce::dontSendNotification);
        slider_.setDoubleClickReturnValue (true, defaultValue);
        slider_.setTooltip (labelText);

        slider_.onValueChange = [this]
        {
            if (onValueChanged != nullptr)
                onValueChanged (static_cast<float> (slider_.getValue()));
        };

        slider_.onDragStart = [this]
        {
            if (onGestureStart != nullptr)
                onGestureStart();
        };

        slider_.onDragEnd = [this]
        {
            if (onGestureEnd != nullptr)
                onGestureEnd();
        };
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label_.setBounds (r.removeFromTop (15));
        slider_.setBounds (r.reduced (0, 1));
    }

    void setValue (float value)
    {
        if (std::abs (slider_.getValue() - value) > 1.0e-6)
            slider_.setValue (value, juce::dontSendNotification);
    }

    void setDefaultValue (float value)
    {
        slider_.setDoubleClickReturnValue (true, value);
    }

    void setActive (bool active)
    {
        setAlpha (active ? 1.0f : 0.32f);
        slider_.setEnabled (active);
    }

    void setLabelColour (juce::Colour colour)
    {
        label_.setColour (juce::Label::textColourId, colour);
    }

    std::function<void (float)> onValueChanged;
    std::function<void()> onGestureStart;
    std::function<void()> onGestureEnd;

private:
    juce::Label label_;
    juce::Slider slider_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FocusKnob)
};

//==============================================================================
/// Three-way curve-type switch for the focused post-EQ band.
class BandTypeSelector : public juce::Component
{
public:
    BandTypeSelector()
    {
        setName ("PostEqTypeSelector");

        label_.setText ("TYPE", juce::dontSendNotification);
        label_.setJustificationType (juce::Justification::centred);
        label_.setFont (baumansFont (8.0f));
        label_.setColour (juce::Label::textColourId, juce::Colour (kTextDim));
        addAndMakeVisible (label_);

        const char* labels[] = { "LOW", "HIGH", "BAND" };
        const char* tooltips[] = {
            "Low-pass shelf curve",
            "High-pass shelf curve",
            "Peaking band curve"
        };
        const char* ids[] = {
            "PostEqTypeLow", "PostEqTypeHigh", "PostEqTypeBand"
        };

        for (size_t i = 0; i < buttons_.size(); ++i)
        {
            auto& button = buttons_[i];
            button.setName (ids[i]);
            button.setButtonText (labels[i]);
            button.setTooltip (tooltips[i]);
            button.setClickingTogglesState (true);
            button.setMouseCursor (juce::MouseCursor::PointingHandCursor);
            button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffe8eaf2));
            button.setColour (juce::TextButton::buttonOnColourId,
                              juce::Colour (0xffcfd4ee));
            button.setColour (juce::TextButton::textColourOffId, juce::Colour (kTextDim));
            button.setColour (juce::TextButton::textColourOnId, juce::Colour (kText));
            button.onClick = [this, i]
            {
                if (onTypeChanged != nullptr)
                    onTypeChanged ((int) i);
            };
            addAndMakeVisible (button);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label_.setBounds (r.removeFromTop (11));
        r.removeFromTop (2);

        const int gap = 2;
        const int width = (r.getWidth() - gap * (static_cast<int> (buttons_.size()) - 1))
                          / static_cast<int> (buttons_.size());

        for (auto& button : buttons_)
            button.setBounds (r.removeFromLeft (width));
    }

    void setSelected (int index)
    {
        for (size_t i = 0; i < buttons_.size(); ++i)
            buttons_[i].setToggleState ((int) i == index, juce::dontSendNotification);
    }

    void setActive (bool active)
    {
        setAlpha (active ? 1.0f : 0.32f);

        for (auto& button : buttons_)
            button.setEnabled (active);
    }

    std::function<void (int)> onTypeChanged;

private:
    juce::Label label_;
    std::array<juce::TextButton, 3> buttons_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandTypeSelector)
};

//==============================================================================
/// Full-width graphical editor for the eight-band post-amp EQ.
class PostEqEditor : public juce::Component,
                     private juce::Timer
{
public:
    PostEqEditor (APVTS& state, SignalSpectrum& spectrum,
                  std::function<double()> sampleRateProvider,
                  std::function<bool()> performanceMode)
        : state_ (state), spectrum_ (spectrum),
          sampleRateProvider_ (std::move (sampleRateProvider)),
          performanceMode_ (std::move (performanceMode)),
          boostKnob_ ("PostEqBoost", "BOOST", " dB", -24.0, 24.0, 0.1, 0.0),
          qKnob_ ("PostEqQ", "Q", {}, 0.1, 10.0, 0.01, 1.0)
    {
        setName ("PostEqGraph");
        setWantsKeyboardFocus (true);

        for (int band = 0; band < (int) bandFreqIds_.size(); ++band)
        {
            const auto index = juce::String (band);
            bandFreqIds_[(size_t) band] = "postEqBand" + index + "Freq";
            bandGainIds_[(size_t) band] = "postEqBand" + index + "Gain";
            bandQIds_[(size_t) band] = "postEqBand" + index + "Q";
            bandTypeIds_[(size_t) band] = "postEqBand" + index + "Type";

            bandFreqParams_[(size_t) band] = state_.getParameter (bandFreqIds_[(size_t) band]);
            bandGainParams_[(size_t) band] = state_.getParameter (bandGainIds_[(size_t) band]);
            bandQParams_[(size_t) band] = state_.getParameter (bandQIds_[(size_t) band]);
            bandTypeParams_[(size_t) band] = state_.getParameter (bandTypeIds_[(size_t) band]);
        }

        highPassFreqParam_ = state_.getParameter ("postEqHpfFreq");
        highPassQParam_ = state_.getParameter ("postEqHpfQ");
        lowPassFreqParam_ = state_.getParameter ("postEqLpfFreq");
        lowPassQParam_ = state_.getParameter ("postEqLpfQ");

        bypassButton_.setButtonText ("Bypass");
        bypassButton_.setTooltip ("Bypass the post-amp EQ");
        bypassButton_.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        bypassButton_.onStateChange = [this]
        {
            updateCache();
            repaint();
        };
        addAndMakeVisible (bypassButton_);
        bypassAttachment_ = std::make_unique<APVTS::ButtonAttachment> (
            state_, "postEqBypass", bypassButton_);

        flatButton_.setButtonText ("Flat");
        flatButton_.setTooltip ("Flat the EQ gains, HPF and LPF");
        flatButton_.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        flatButton_.onClick = [this] { flatten(); };
        addAndMakeVisible (flatButton_);

        addAndMakeVisible (boostKnob_);
        addAndMakeVisible (qKnob_);
        addAndMakeVisible (typeSelector_);

        typeSelector_.onTypeChanged = [this] (int type)
        {
            if (selectedBand_ < 0)
                return;

            const auto index = (size_t) selectedBand_;
            auto* parameter = bandTypeParams_[index];

            beginGesture (parameter);
            setParameterValue (parameter, static_cast<float> (type));
            endGesture (parameter);
            updateCache();
        };

        boostKnob_.onGestureStart = [this]
        {
            boostGestureParam_ = focusedGainParameter();
            beginGesture (boostGestureParam_);
        };
        boostKnob_.onGestureEnd = [this]
        {
            endGesture (boostGestureParam_);
            boostGestureParam_ = nullptr;
        };
        boostKnob_.onValueChanged = [this] (float value)
        {
            if (auto* parameter = focusedGainParameter())
            {
                setParameterValue (parameter, value);
                updateCache();
            }
        };

        qKnob_.onGestureStart = [this]
        {
            qGestureParam_ = focusedQParameter();
            beginGesture (qGestureParam_);
        };
        qKnob_.onGestureEnd = [this]
        {
            endGesture (qGestureParam_);
            qGestureParam_ = nullptr;
        };
        qKnob_.onValueChanged = [this] (float value)
        {
            if (auto* parameter = focusedQParameter())
            {
                setParameterValue (parameter, value);
                updateCache();
            }
        };

        cachedSettings_ = readSettings();
        cachedBypassed_ = isBypassedInState();
        updateFocusControls();
        startTimerHz (30);
    }

    ~PostEqEditor() override
    {
        endActiveGesture();
        endFocusGestures();
        stopTimer();
    }

    void visibilityChanged() override
    {
        if (! isVisible())
        {
            endActiveGesture();
            endFocusGestures();
            dragTarget_ = DragTarget::None;
            activeBand_ = -1;
        }
        else
        {
            spectrum_.update();
            spectrumRevision_ = spectrum_.getRevision();
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 8);
        topArea_ = r.removeFromTop (30);
        hintArea_ = r.removeFromBottom (22);

        auto focus = r.removeFromRight (108);
        r.removeFromRight (10);
        focusArea_ = focus;
        screenArea_ = r.toFloat();

        auto plot = r;
        plot.removeFromLeft (38);
        plot.removeFromRight (8);
        plot.removeFromTop (8);
        plot.removeFromBottom (18);
        plotArea_ = plot.toFloat();

        auto focusContent = focusArea_.reduced (6, 4);
        focusTitleArea_ = focusContent.removeFromTop (32);
        focusContent.removeFromTop (4);
        typeSelector_.setBounds (focusContent.removeFromTop (26));
        focusContent.removeFromTop (4);

        const int knobHeight = juce::jlimit (60, 110,
                                            (focusContent.getHeight() - 6) / 2);
        boostKnob_.setBounds (focusContent.removeFromTop (knobHeight));
        focusContent.removeFromTop (6);
        qKnob_.setBounds (focusContent.removeFromTop (knobHeight));

        auto controls = topArea_;
        auto buttonArea = controls.removeFromRight (176);
        flatButton_.setBounds (buttonArea.removeFromRight (56).reduced (2, 3));
        bypassButton_.setBounds (buttonArea.removeFromRight (112).reduced (2, 3));

        // Refresh here too so the graph is populated on first paint even when
        // the message loop has not ticked the timer yet (headless snapshots).
        if (! isPerformanceMode())
        {
            spectrum_.update();
            spectrumRevision_ = spectrum_.getRevision();
        }
    }

    void paint (juce::Graphics& g) override
    {
        if (screenArea_.isEmpty() || plotArea_.isEmpty())
            return;

        // Recessed screen.
        g.setColour (juce::Colour (kScreen));
        g.fillRoundedRectangle (screenArea_, 6.0f);
        g.setColour (juce::Colour (kScreenLine));
        g.drawRoundedRectangle (screenArea_, 6.0f, 1.0f);

        drawStatusLine (g);
        drawFocusPanel (g);
        drawGrid (g);
        drawCutRegions (g);
        drawSpectrum (g);
        drawCurve (g);
        drawPassHandles (g);
        drawNodes (g);

        if (cachedBypassed_)
        {
            g.setColour (juce::Colours::black.withAlpha (0.42f));
            g.fillRoundedRectangle (screenArea_, 6.0f);
            g.setColour (juce::Colour (kScreenText));
            g.setFont (baumansFont (18.0f));
            g.drawText ("BYPASSED", screenArea_, juce::Justification::centred);
        }

        g.setColour (juce::Colour (kTextDim).withAlpha (0.82f));
        g.setFont (baumansFont (9.5f));
        g.drawFittedText ("DRAG: FREQ / GAIN    WHEEL: Q    DOUBLE-CLICK: DISABLE BAND    RIGHT-CLICK: MENU    KNOBS + TYPE: FOCUSED BAND / FILTER",
                          hintArea_, juce::Justification::centredLeft, 1, 0.85f);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (! plotArea_.contains (event.position))
            return;

        const int band = bandAt (event.position);
        const auto pass = passAt (event.position);

        if (event.mods.isPopupMenu())
        {
            if (band >= 0)
            {
                selectedBand_ = band;
                selectedPass_ = Pass::None;
            }
            else if (pass != Pass::None)
            {
                selectedBand_ = -1;
                selectedPass_ = pass;
            }

            updateFocusControls();
            repaint();
            showContextMenu (band, pass);
            return;
        }

        if (! event.mods.isLeftButtonDown())
            return;

        if (band >= 0)
        {
            selectedBand_ = band;
            selectedPass_ = Pass::None;
            hoverBand_ = band;
            hoverPass_ = Pass::None;
            activeBand_ = band;
            dragTarget_ = DragTarget::Band;

            beginGesture (bandFreqParams_[(size_t) band]);
            beginGesture (bandGainParams_[(size_t) band]);
            updateFocusControls();
            repaint();
            return;
        }

        if (pass != Pass::None)
        {
            selectedBand_ = -1;
            selectedPass_ = pass;
            hoverBand_ = -1;
            hoverPass_ = pass;
            dragTarget_ = pass == Pass::HighPass ? DragTarget::HighPass
                                                 : DragTarget::LowPass;

            beginGesture (pass == Pass::HighPass ? highPassFreqParam_ : lowPassFreqParam_);
            updateFocusControls();
            repaint();
            return;
        }

        selectedBand_ = -1;
        selectedPass_ = Pass::None;
        dragTarget_ = DragTarget::None;
        updateFocusControls();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (dragTarget_ == DragTarget::Band && activeBand_ >= 0)
        {
            const auto index = (size_t) activeBand_;
            setParameterValue (bandFreqParams_[index],
                               xToFrequency (event.position.x));
            setParameterValue (bandGainParams_[index],
                               yToGain (event.position.y));
            updateCache();
            return;
        }

        if (dragTarget_ == DragTarget::HighPass || dragTarget_ == DragTarget::LowPass)
        {
            float frequency = xToFrequency (event.position.x);

            if (dragTarget_ == DragTarget::HighPass)
                frequency = juce::jlimit (20.0f, 2000.0f, frequency);
            else
                frequency = juce::jlimit (2000.0f, 20000.0f, frequency);

            setParameterValue (dragTarget_ == DragTarget::HighPass
                                   ? highPassFreqParam_
                                   : lowPassFreqParam_,
                               frequency);
            updateCache();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        endActiveGesture();
        dragTarget_ = DragTarget::None;
        activeBand_ = -1;
    }

    void mouseMove (const juce::MouseEvent& event) override
    {
        if (dragTarget_ != DragTarget::None)
            return;

        const int band = bandAt (event.position);
        const auto pass = passAt (event.position);

        if (band != hoverBand_ || pass != hoverPass_)
        {
            hoverBand_ = band;
            hoverPass_ = pass;
            repaint();
        }

        setMouseCursor (band >= 0 || pass != Pass::None
                            ? juce::MouseCursor::PointingHandCursor
                            : juce::MouseCursor::NormalCursor);
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hoverBand_ != -1 || hoverPass_ != Pass::None)
        {
            hoverBand_ = -1;
            hoverPass_ = Pass::None;
            repaint();
        }

        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    void mouseDoubleClick (const juce::MouseEvent& event) override
    {
        const int band = bandAt (event.position);

        if (band >= 0)
        {
            toggleBandEnabled (band);
            return;
        }

        const auto pass = passAt (event.position);

        if (pass != Pass::None)
        {
            auto* param = pass == Pass::HighPass ? highPassFreqParam_ : lowPassFreqParam_;
            beginGesture (param);
            setParameterValue (param, pass == Pass::HighPass ? 20.0f : 20000.0f);
            endGesture (param);
            updateCache();
        }
    }

    void mouseWheelMove (const juce::MouseEvent& event,
                         const juce::MouseWheelDetails& wheel) override
    {
        const float direction = wheel.deltaY > 0.0f ? 1.0f : -1.0f;
        const float step = event.mods.isShiftDown() ? 0.45f : 1.8f;
        const float factor = std::pow (1.15f, direction * step);

        const int band = bandAt (event.position);
        const auto pass = passAt (event.position);

        if (band >= 0)
        {
            auto* param = bandQParams_[(size_t) band];
            const auto current = rawValue (bandQIds_[(size_t) band], 1.0f);

            beginGesture (param);
            setParameterValue (param, current * factor);
            endGesture (param);
            updateCache();
            return;
        }

        if (pass != Pass::None)
        {
            auto* param = pass == Pass::HighPass ? highPassQParam_ : lowPassQParam_;
            const auto current = pass == Pass::HighPass
                ? rawValue ("postEqHpfQ", 0.707f)
                : rawValue ("postEqLpfQ", 0.707f);

            beginGesture (param);
            setParameterValue (param, current * factor);
            endGesture (param);
            updateCache();
        }
    }

private:
    enum class DragTarget { None, Band, HighPass, LowPass };
    enum class Pass { None, HighPass, LowPass };

    static constexpr int numBands = (int) PostAmpEQ::kNumBands;

    static juce::Colour bandColour (int band)
    {
        // One vivid hue per band so nodes, curves and the focus strip stay
        // recognisable at a glance on the dark EQ screen.
        static const juce::uint32 colours[] = {
            0xff22d3ee, 0xff34d399, 0xffa3e635, 0xfffbbf24,
            0xfffb923c, 0xfff472b6, 0xffc084fc, 0xff60a5fa
        };

        return juce::Colour (colours[(size_t) juce::jlimit (0, numBands - 1, band)]);
    }

    juce::Colour focusColour() const
    {
        if (selectedBand_ >= 0)
            return bandColour (selectedBand_);

        if (selectedPass_ != Pass::None)
            return juce::Colour (0xff93c5fd);

        return juce::Colour (kScreenDim);
    }

    juce::String focusTitle() const
    {
        if (selectedBand_ >= 0)
            return "BAND " + juce::String (selectedBand_ + 1);

        if (selectedPass_ == Pass::HighPass)
            return "HPF";

        if (selectedPass_ == Pass::LowPass)
            return "LPF";

        return "NONE";
    }

    //==========================================================================
    PostAmpEQ::Settings readSettings() const
    {
        PostAmpEQ::Settings settings;

        for (int band = 0; band < numBands; ++band)
        {
            const auto index = (size_t) band;
            settings.bands[index] = {
                rawValue (bandFreqIds_[index], settings.bands[index].frequency),
                rawValue (bandGainIds_[index], 0.0f),
                rawValue (bandQIds_[index], settings.bands[index].q),
                PostAmpEQ::bandTypeFromInt (
                    (int) rawValue (bandTypeIds_[index],
                                    (float) PostAmpEQ::bandTypeToInt (
                                        settings.bands[index].type)))
            };
        }

        settings.highPassFrequency = rawValue ("postEqHpfFreq", 20.0f);
        settings.highPassQ = rawValue ("postEqHpfQ", 0.707f);
        settings.lowPassFrequency = rawValue ("postEqLpfFreq", 20000.0f);
        settings.lowPassQ = rawValue ("postEqLpfQ", 0.707f);
        return settings;
    }

    float rawValue (const juce::String& id, float fallback) const
    {
        if (auto* value = state_.getRawParameterValue (id))
            return value->load();

        return fallback;
    }

    bool isBypassedInState() const
    {
        return rawValue ("postEqBypass", 0.0f) > 0.5f;
    }

    static bool settingsEqual (const PostAmpEQ::Settings& a,
                               const PostAmpEQ::Settings& b)
    {
        for (size_t i = 0; i < a.bands.size(); ++i)
        {
            if (a.bands[i].type != b.bands[i].type
                || ! juce::approximatelyEqual (a.bands[i].frequency, b.bands[i].frequency)
                || ! juce::approximatelyEqual (a.bands[i].gainDb, b.bands[i].gainDb)
                || ! juce::approximatelyEqual (a.bands[i].q, b.bands[i].q))
                return false;
        }

        return juce::approximatelyEqual (a.highPassFrequency, b.highPassFrequency)
            && juce::approximatelyEqual (a.highPassQ, b.highPassQ)
            && juce::approximatelyEqual (a.lowPassFrequency, b.lowPassFrequency)
            && juce::approximatelyEqual (a.lowPassQ, b.lowPassQ);
    }

    void updateCache()
    {
        cachedSettings_ = readSettings();
        cachedBypassed_ = isBypassedInState();
        updateFocusControls();
        repaint();
    }

    void timerCallback() override
    {
        const auto settings = readSettings();
        const bool bypassed = isBypassedInState();

        if (! settingsEqual (settings, cachedSettings_) || bypassed != cachedBypassed_)
        {
            cachedSettings_ = settings;
            cachedBypassed_ = bypassed;
            updateFocusControls();
            repaint();
        }

        if (! isVisible())
            return;

        // Performance mode skips the FFT and the spectrum repaint entirely.
        if (isPerformanceMode())
            return;

        spectrum_.update();

        if (spectrum_.getRevision() != spectrumRevision_)
        {
            spectrumRevision_ = spectrum_.getRevision();
            repaint (plotArea_.getSmallestIntegerContainer().expanded (2));
        }
    }

    juce::RangedAudioParameter* focusedGainParameter() const
    {
        if (selectedBand_ < 0)
            return nullptr;

        return bandGainParams_[(size_t) selectedBand_];
    }

    juce::RangedAudioParameter* focusedQParameter() const
    {
        if (selectedBand_ >= 0)
            return bandQParams_[(size_t) selectedBand_];

        if (selectedPass_ == Pass::HighPass)
            return highPassQParam_;

        if (selectedPass_ == Pass::LowPass)
            return lowPassQParam_;

        return nullptr;
    }

    void updateFocusControls()
    {
        const bool hasBand = selectedBand_ >= 0;
        const bool hasPass = selectedPass_ != Pass::None;
        const auto highlight = focusColour().darker (0.35f);

        boostKnob_.setActive (hasBand);
        qKnob_.setActive (hasBand || hasPass);
        typeSelector_.setActive (hasBand);
        boostKnob_.setLabelColour (hasBand ? highlight : juce::Colour (0xff1a1a1a));
        qKnob_.setLabelColour (hasBand || hasPass ? highlight : juce::Colour (0xff1a1a1a));

        if (hasBand)
        {
            const auto index = (size_t) selectedBand_;
            const auto& band = cachedSettings_.bands[index];
            const PostAmpEQ::Settings defaults;

            typeSelector_.setSelected (PostAmpEQ::bandTypeToInt (band.type));
            boostKnob_.setValue (band.gainDb);
            boostKnob_.setDefaultValue (defaults.bands[index].gainDb);
            qKnob_.setValue (band.q);
            qKnob_.setDefaultValue (defaults.bands[index].q);
        }
        else if (hasPass)
        {
            const auto q = selectedPass_ == Pass::HighPass
                ? cachedSettings_.highPassQ
                : cachedSettings_.lowPassQ;

            boostKnob_.setValue (0.0f);
            qKnob_.setValue (q);
            qKnob_.setDefaultValue (0.707f);
        }
        else
        {
            boostKnob_.setValue (0.0f);
            qKnob_.setValue (1.0f);
            qKnob_.setDefaultValue (1.0f);
        }

        repaint (focusArea_);
    }

    static void setParameterValue (juce::RangedAudioParameter* parameter, float value)
    {
        if (parameter == nullptr)
            return;

        const auto& range = parameter->getNormalisableRange();
        const auto legal = range.snapToLegalValue (
            juce::jlimit (range.start, range.end, value));
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (legal));
    }

    static void beginGesture (juce::RangedAudioParameter* parameter)
    {
        if (parameter != nullptr)
            parameter->beginChangeGesture();
    }

    static void endGesture (juce::RangedAudioParameter* parameter)
    {
        if (parameter != nullptr)
            parameter->endChangeGesture();
    }

    void endActiveGesture()
    {
        if (dragTarget_ == DragTarget::Band && activeBand_ >= 0)
        {
            const auto index = (size_t) activeBand_;
            endGesture (bandFreqParams_[index]);
            endGesture (bandGainParams_[index]);
        }
        else if (dragTarget_ == DragTarget::HighPass)
        {
            endGesture (highPassFreqParam_);
        }
        else if (dragTarget_ == DragTarget::LowPass)
        {
            endGesture (lowPassFreqParam_);
        }
    }

    void endFocusGestures()
    {
        endGesture (boostGestureParam_);
        boostGestureParam_ = nullptr;
        endGesture (qGestureParam_);
        qGestureParam_ = nullptr;
    }

    //==========================================================================
    double sampleRate() const
    {
        const auto rate = sampleRateProvider_ != nullptr ? sampleRateProvider_() : 0.0;
        return rate > 0.0 ? rate : 48000.0;
    }

    bool isPerformanceMode() const
    {
        return performanceMode_ != nullptr && performanceMode_();
    }

    float frequencyToX (float frequency) const
    {
        const auto clamped = juce::jlimit (20.0f, 20000.0f, frequency);
        const auto normalized = std::log (clamped / 20.0f) / std::log (1000.0f);
        return plotArea_.getX() + normalized * plotArea_.getWidth();
    }

    float xToFrequency (float x) const
    {
        const auto normalized = juce::jlimit (
            0.0f, 1.0f, (x - plotArea_.getX()) / juce::jmax (1.0f, plotArea_.getWidth()));
        return 20.0f * std::pow (1000.0f, normalized);
    }

    float gainToY (float gainDb) const
    {
        const auto normalized = (juce::jlimit (-24.0f, 24.0f, gainDb) + 24.0f) / 48.0f;
        return plotArea_.getBottom() - normalized * plotArea_.getHeight();
    }

    float yToGain (float y) const
    {
        const auto normalized = juce::jlimit (
            0.0f, 1.0f, (plotArea_.getBottom() - y) / juce::jmax (1.0f, plotArea_.getHeight()));
        return normalized * 48.0f - 24.0f;
    }

    juce::Point<float> bandNodeCentre (int band) const
    {
        const auto& value = cachedSettings_.bands[(size_t) band];
        const auto x = juce::jlimit (plotArea_.getX() + 9.0f,
                                     plotArea_.getRight() - 9.0f,
                                     frequencyToX (value.frequency));
        return { x, gainToY (value.gainDb) };
    }

    int bandAt (juce::Point<float> position) const
    {
        if (! plotArea_.expanded (14.0f).contains (position))
            return -1;

        for (int band = numBands - 1; band >= 0; --band)
            if (bandNodeCentre (band).getDistanceFrom (position) <= 14.0f)
                return band;

        return -1;
    }

    juce::Rectangle<float> passHandleBounds (Pass pass) const
    {
        const auto frequency = pass == Pass::HighPass
            ? cachedSettings_.highPassFrequency
            : cachedSettings_.lowPassFrequency;
        const auto x = juce::jlimit (plotArea_.getX() + 15.0f,
                                     plotArea_.getRight() - 15.0f,
                                     frequencyToX (frequency));
        return { x - 15.0f, plotArea_.getBottom() - 17.0f, 30.0f, 15.0f };
    }

    Pass passAt (juce::Point<float> position) const
    {
        if (passHandleBounds (Pass::HighPass).expanded (4.0f).contains (position))
            return Pass::HighPass;

        if (passHandleBounds (Pass::LowPass).expanded (4.0f).contains (position))
            return Pass::LowPass;

        return Pass::None;
    }

    //==========================================================================
    void drawStatusLine (juce::Graphics& g)
    {
        const int statusWidth = 320;
        const int statusHeight = 22;
        auto area = juce::Rectangle<int> (
            topArea_.getCentreX() - statusWidth / 2,
            topArea_.getCentreY() - statusHeight / 2,
            statusWidth, statusHeight);

        if (area.isEmpty())
            return;

        // Light panel background with black text, matching the other control
        // labels in the editor.
        g.setColour (juce::Colour (kPanel));
        g.fillRoundedRectangle (area.toFloat(), 5.0f);
        g.setColour (juce::Colour (kPanelLine));
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 5.0f, 1.0f);

        const bool hasSelection = selectedBand_ >= 0 || selectedPass_ != Pass::None;
        auto colour = hasSelection ? focusColour() : juce::Colour (kScreenDim);

        if (cachedBypassed_)
            colour = colour.withAlpha (0.55f);

        if (hasSelection)
        {
            g.setColour (colour.withAlpha (0.9f));
            g.fillEllipse ((float) area.getX() + 8.0f,
                           (float) area.getCentreY() - 4.0f, 8.0f, 8.0f);
        }

        g.setColour (hasSelection ? juce::Colour (0xff1a1a1a) : juce::Colour (kTextDim));
        g.setFont (sevenSegmentFont (15.0f));
        g.drawFittedText (selectionText(), area,
                          juce::Justification::centred, 1, 0.8f);
    }

    void drawFocusPanel (juce::Graphics& g)
    {
        const auto area = focusArea_.toFloat();

        if (area.isEmpty())
            return;

        g.setColour (juce::Colour (kPanel));
        g.fillRoundedRectangle (area, 6.0f);
        g.setColour (juce::Colour (kPanelLine));
        g.drawRoundedRectangle (area, 6.0f, 1.0f);

        g.setColour (focusColour().darker (0.25f).withAlpha (0.8f));
        g.fillRoundedRectangle (area.getX() + 8.0f, area.getY() + 2.0f,
                                area.getWidth() - 16.0f, 2.0f, 1.0f);

        g.setColour (juce::Colour (0xff1a1a1a));
        g.setFont (sevenSegmentFont (12.0f));
        g.drawText (focusTitle(), focusTitleArea_.toFloat(), juce::Justification::centred);
    }

    void drawGrid (juce::Graphics& g)
    {
        static const float minorFrequencies[] = {
            20.0f, 30.0f, 50.0f, 70.0f, 100.0f, 200.0f, 300.0f, 500.0f,
            700.0f, 1000.0f, 2000.0f, 3000.0f, 5000.0f, 7000.0f, 10000.0f, 20000.0f
        };
        static const float labelFrequencies[] = {
            20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f,
            2000.0f, 5000.0f, 10000.0f, 20000.0f
        };

        for (float frequency : minorFrequencies)
        {
            const auto x = frequencyToX (frequency);
            const bool edge = frequency <= 20.0f || frequency >= 20000.0f;
            g.setColour (juce::Colours::white.withAlpha (edge ? 0.10f : 0.055f));
            g.drawVerticalLine ((int) std::round (x), plotArea_.getY(), plotArea_.getBottom());
        }

        for (float frequency : labelFrequencies)
        {
            const auto x = frequencyToX (frequency);
            auto label = juce::Rectangle<float> (x - 22.0f, plotArea_.getBottom() + 2.0f,
                                                 44.0f, 14.0f);
            g.setColour (juce::Colour (kScreenDim).withAlpha (0.8f));
            g.setFont (baumansFont (8.5f));
            g.drawText (frequencyLabel (frequency), label,
                        juce::Justification::centred);
        }

        for (int db = -24; db <= 24; db += 6)
        {
            const auto y = gainToY ((float) db);
            const bool zero = db == 0;

            g.setColour (zero ? juce::Colour (kScreenGlow).withAlpha (0.30f)
                              : juce::Colours::white.withAlpha (0.055f));
            g.drawHorizontalLine ((int) std::round (y), plotArea_.getX(), plotArea_.getRight());

            if (db % 12 == 0 || zero)
            {
                const auto label = db > 0 ? "+" + juce::String (db)
                                          : juce::String (db);
                g.setColour (juce::Colour (kScreenDim).withAlpha (0.8f));
                g.setFont (baumansFont (8.5f));
                g.drawText (label,
                            juce::Rectangle<float> (screenArea_.getX() + 5.0f, y - 7.0f,
                                                    30.0f, 14.0f),
                            juce::Justification::centredRight);
            }
        }
    }

    void drawCutRegions (juce::Graphics& g)
    {
        const auto highPassX = frequencyToX (cachedSettings_.highPassFrequency);
        const auto lowPassX = frequencyToX (cachedSettings_.lowPassFrequency);

        g.setColour (juce::Colour (kScreenGlow).withAlpha (cachedBypassed_ ? 0.025f : 0.05f));
        g.fillRect (juce::Rectangle<float> (plotArea_.getX(), plotArea_.getY(),
                                            highPassX - plotArea_.getX(),
                                            plotArea_.getHeight()));
        g.fillRect (juce::Rectangle<float> (lowPassX, plotArea_.getY(),
                                            plotArea_.getRight() - lowPassX,
                                            plotArea_.getHeight()));
    }

    float spectrumDbToY (float db) const
    {
        const auto normalized = juce::jlimit (0.0f, 1.0f, (db + 72.0f) / 72.0f);
        return plotArea_.getBottom() - normalized * plotArea_.getHeight();
    }

    /// Live magnitude trace of the audio on the Post EQ output, drawn behind
    /// the response curve. It is intentionally unlabelled on the dB axis: the
    /// graph's ±24 dB scale belongs to the EQ response, the trace is a meter.
    void drawSpectrum (juce::Graphics& g)
    {
        if (! spectrum_.hasSignal())
            return;

        const int numBins = spectrum_.getNumBins();
        const auto sampleRateHz = spectrum_.getSampleRate();

        if (numBins < 4 || sampleRateHz <= 0.0)
            return;

        const auto binHz = sampleRateHz / (double) SignalSpectrum::kFftSize;
        const int columns = juce::jlimit (2, 1600, (int) std::round (plotArea_.getWidth()));

        // Sample the linear FFT bins at each log-spaced pixel column, then
        // soften the result so low-frequency bins read as a curve instead of
        // a staircase.
        std::vector<float> levels ((size_t) columns, -100.0f);

        for (int column = 0; column < columns; ++column)
        {
            const auto x = plotArea_.getX() + (float) column + 0.5f;
            const auto frequency = (double) xToFrequency (x);
            const auto position = frequency / binHz;

            const int lowBin = juce::jlimit (1, numBins - 2, (int) std::floor (position));
            const auto fraction = (float) juce::jlimit (0.0, 1.0, position - (double) lowBin);

            const auto lowDb = spectrum_.getBinDecibels (lowBin);
            const auto highDb = spectrum_.getBinDecibels (lowBin + 1);
            levels[(size_t) column] = lowDb + (highDb - lowDb) * fraction;
        }

        const int radius = juce::jlimit (1, 12, columns / 110);
        std::vector<float> smoothed ((size_t) columns, -100.0f);

        for (int column = 0; column < columns; ++column)
        {
            float sum = 0.0f;
            int count = 0;

            for (int offset = -radius; offset <= radius; ++offset)
            {
                const int index = juce::jlimit (0, columns - 1, column + offset);
                sum += levels[(size_t) index];
                ++count;
            }

            smoothed[(size_t) column] = sum / (float) count;
        }

        juce::Path curve;
        bool started = false;

        for (int column = 0; column < columns; ++column)
        {
            const auto x = plotArea_.getX() + (float) column + 0.5f;
            const auto y = spectrumDbToY (smoothed[(size_t) column]);

            if (! started)
            {
                curve.startNewSubPath (x, y);
                started = true;
            }
            else
            {
                curve.lineTo (x, y);
            }
        }

        if (! started)
            return;

        juce::Path fill (curve);
        fill.lineTo (plotArea_.getRight(), plotArea_.getBottom());
        fill.lineTo (plotArea_.getX(), plotArea_.getBottom());
        fill.closeSubPath();

        const auto trace = juce::Colour (kScreenGlow);
        g.setGradientFill (juce::ColourGradient (
            trace.withAlpha (cachedBypassed_ ? 0.05f : 0.12f),
            plotArea_.getX(), plotArea_.getY(),
            trace.withAlpha (0.01f), plotArea_.getX(), plotArea_.getBottom(), false));
        g.fillPath (fill);

        g.setColour (trace.withAlpha (cachedBypassed_ ? 0.20f : 0.45f));
        g.strokePath (curve, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        auto legend = juce::Rectangle<float> (plotArea_.getRight() - 104.0f,
                                              plotArea_.getY() + 4.0f, 100.0f, 12.0f);
        g.setColour (trace.withAlpha (cachedBypassed_ ? 0.25f : 0.55f));
        g.setFont (baumansFont (8.5f));
        g.drawText ("POST-EQ SIGNAL", legend, juce::Justification::centredRight);
    }

    void drawCurve (juce::Graphics& g)
    {
        const int columns = juce::jlimit (2, 1600, (int) std::round (plotArea_.getWidth()));

        std::vector<double> frequencies ((size_t) columns);
        std::vector<float> response ((size_t) columns);

        for (int column = 0; column < columns; ++column)
            frequencies[(size_t) column] = xToFrequency (
                plotArea_.getX() + (float) column + 0.5f);

        PostAmpEQ::getResponseDb (cachedSettings_, sampleRate(),
                                  frequencies.data(), response.data(), columns);

        juce::Path curve;
        bool started = false;

        for (int column = 0; column < columns; ++column)
        {
            const auto x = plotArea_.getX() + (float) column;
            const auto y = juce::jlimit (plotArea_.getY(), plotArea_.getBottom(),
                                        gainToY (response[(size_t) column]));

            if (! started)
            {
                curve.startNewSubPath (x, y);
                started = true;
            }
            else
            {
                curve.lineTo (x, y);
            }
        }

        if (! started)
            return;

        juce::Path fill (curve);
        fill.lineTo (plotArea_.getRight(), plotArea_.getBottom());
        fill.lineTo (plotArea_.getX(), plotArea_.getBottom());
        fill.closeSubPath();

        const auto accent = juce::Colour (0xffe2e8f0);
        const auto alpha = cachedBypassed_ ? 0.05f : 0.16f;
        g.setGradientFill (juce::ColourGradient (
            accent.withAlpha (alpha), plotArea_.getX(), plotArea_.getY(),
            accent.withAlpha (0.015f), plotArea_.getX(), plotArea_.getBottom(), false));
        g.fillPath (fill);

        g.setColour (accent.withAlpha (cachedBypassed_ ? 0.30f : 0.95f));
        g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    void drawPassHandles (juce::Graphics& g)
    {
        drawPassHandle (g, Pass::HighPass);
        drawPassHandle (g, Pass::LowPass);
    }

    void drawPassHandle (juce::Graphics& g, Pass pass)
    {
        const bool isHighPass = pass == Pass::HighPass;
        const auto frequency = isHighPass
            ? cachedSettings_.highPassFrequency
            : cachedSettings_.lowPassFrequency;
        // Clamp the position the same way passHandleBounds does so the vertical
        // line runs through the centre of the HPF/LPF label rectangle, even
        // when the filter is at the edge of the plot (fully disabled).
        const auto x = juce::jlimit (plotArea_.getX() + 15.0f,
                                     plotArea_.getRight() - 15.0f,
                                     frequencyToX (frequency));
        const auto selected = selectedPass_ == pass;

        g.setColour (juce::Colour (kScreenGlow).withAlpha (
            cachedBypassed_ ? 0.15f : (selected ? 0.70f : 0.35f)));
        g.drawVerticalLine ((int) std::round (x), plotArea_.getY(), plotArea_.getBottom());

        auto handle = passHandleBounds (pass);
        g.setColour (selected ? juce::Colour (0xffbfdbfe) : juce::Colour (0xff1e3a5f));
        g.fillRoundedRectangle (handle, 4.0f);
        g.setColour (juce::Colour (0xff93c5fd).withAlpha (selected ? 0.95f : 0.55f));
        g.drawRoundedRectangle (handle, 4.0f, 1.0f);

        g.setColour (selected ? juce::Colour (0xff06121f) : juce::Colour (0xffdbeafe));
        g.setFont (baumansFont (8.5f));
        g.drawText (isHighPass ? "HPF" : "LPF", handle, juce::Justification::centred);
    }

    void drawNodes (juce::Graphics& g)
    {
        for (int band = 0; band < numBands; ++band)
        {
            const auto centre = bandNodeCentre (band);
            const bool selected = selectedBand_ == band;
            const bool hovered = hoverBand_ == band;
            const auto colour = bandColour (band);
            const bool disabled = bandIsDisabled (cachedSettings_.bands[(size_t) band]);
            const float radius = selected ? 11.5f : (hovered ? 10.5f : 9.0f);

            if (selected || hovered)
            {
                const auto alpha = disabled ? (selected ? 0.09f : 0.05f)
                                            : (selected ? 0.18f : 0.11f);
                g.setColour (colour.withAlpha (alpha));
                g.fillEllipse (juce::Rectangle<float> (radius * 3.0f, radius * 3.0f)
                                   .withCentre (centre));
            }

            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f)
                               .withCentre (centre).translated (0.0f, 1.5f));

            if (disabled)
            {
                // Hollow node: the band is at 0 dB and does not affect the sound.
                g.setColour (juce::Colour (kKnobBody));
                g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f)
                                   .withCentre (centre));
                g.setColour (colour.withAlpha (cachedBypassed_ ? 0.22f : 0.55f));
                g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f)
                                   .withCentre (centre), selected ? 2.0f : 1.3f);
            }
            else
            {
                g.setColour (cachedBypassed_ ? colour.withAlpha (0.35f) : colour);
                g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f)
                                   .withCentre (centre));

                g.setColour (selected ? juce::Colours::white.withAlpha (0.92f)
                                      : juce::Colours::black.withAlpha (0.45f));
                g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f)
                                   .withCentre (centre), selected ? 2.0f : 1.0f);
            }

            g.setColour (disabled ? colour.withAlpha (cachedBypassed_ ? 0.35f : 0.80f)
                                  : juce::Colour (0xff111113));
            g.setFont (baumansFont (9.0f));
            g.drawText (juce::String (band + 1),
                        juce::Rectangle<float> (radius * 2.0f, radius * 2.0f)
                            .withCentre (centre),
                        juce::Justification::centred);
        }
    }

    //==========================================================================
    juce::String frequencyLabel (float frequency) const
    {
        if (frequency >= 1000.0f)
            return juce::String (frequency / 1000.0f, 0) + "k";

        return juce::String (juce::roundToInt (frequency));
    }

    static juce::String formatFrequency (float frequency)
    {
        if (frequency >= 1000.0f)
        {
            const auto value = frequency / 1000.0f;
            return juce::String (value, value >= 10.0f ? 1 : 2) + " kHz";
        }

        return juce::String (juce::roundToInt (frequency)) + " Hz";
    }

    juce::String selectionText() const
    {
        if (selectedPass_ == Pass::HighPass)
            return "HPF  ·  " + formatFrequency (cachedSettings_.highPassFrequency);

        if (selectedPass_ == Pass::LowPass)
            return "LPF  ·  " + formatFrequency (cachedSettings_.lowPassFrequency);

        if (selectedBand_ >= 0)
        {
            const auto& band = cachedSettings_.bands[(size_t) selectedBand_];
            auto text = "BAND " + juce::String (selectedBand_ + 1)
                        + "  ·  " + formatFrequency (band.frequency);

            if (bandIsDisabled (band))
                text << "  ·  OFF";

            return text;
        }

        return "8-BAND GRAPHIC EQ";
    }

    static bool bandIsDisabled (const PostAmpEQ::BandParams& band)
    {
        // The DSP has no per-band bypass: a flat gain is exactly an inactive
        // band, so "disabled" is derived from the gain and survives reloads.
        return std::abs (band.gainDb) < 0.05f;
    }

    void toggleBandEnabled (int band)
    {
        const auto index = (size_t) band;
        auto* gainParameter = bandGainParams_[index];

        if (gainParameter == nullptr)
            return;

        float target = 0.0f;

        if (bandIsDisabled (cachedSettings_.bands[index]))
        {
            // Re-enable only when a previous non-flat gain is remembered.
            if (std::abs (previousGains_[index]) < 0.05f)
            {
                selectedBand_ = band;
                selectedPass_ = Pass::None;
                updateCache();
                return;
            }

            target = previousGains_[index];
        }
        else
        {
            previousGains_[index] = cachedSettings_.bands[index].gainDb;
        }

        beginGesture (gainParameter);
        setParameterValue (gainParameter, target);
        endGesture (gainParameter);

        selectedBand_ = band;
        selectedPass_ = Pass::None;
        updateCache();
    }

    void showContextMenu (int band, Pass pass)
    {
        juce::PopupMenu menu;

        if (band >= 0)
            menu.addItem (1, "Reset band " + juce::String (band + 1));
        else if (pass != Pass::None)
            menu.addItem (1, pass == Pass::HighPass ? "Reset high-pass" : "Reset low-pass");

        menu.addItem (2, "Reset all bands");
        menu.addItem (3, "Flat EQ");

        juce::Component::SafePointer<PostEqEditor> safeThis (this);

        menu.showMenuAsync (juce::PopupMenu::Options()
                                .withTargetComponent (this)
                                .withMousePosition(),
                            [safeThis, band, pass] (int result)
                            {
                                if (safeThis == nullptr)
                                    return;

                                if (result == 1)
                                {
                                    if (band >= 0)
                                        safeThis->resetBand (band);
                                    else if (pass != Pass::None)
                                        safeThis->resetPass (pass);
                                }
                                else if (result == 2)
                                {
                                    safeThis->resetAllBands();
                                }
                                else if (result == 3)
                                {
                                    safeThis->flatten();
                                }
                            });
    }

    void resetBand (int band)
    {
        const PostAmpEQ::Settings defaults;
        const auto index = (size_t) band;

        beginGesture (bandFreqParams_[index]);
        beginGesture (bandGainParams_[index]);
        beginGesture (bandQParams_[index]);
        beginGesture (bandTypeParams_[index]);
        setParameterValue (bandFreqParams_[index], defaults.bands[index].frequency);
        setParameterValue (bandGainParams_[index], defaults.bands[index].gainDb);
        setParameterValue (bandQParams_[index], defaults.bands[index].q);
        setParameterValue (bandTypeParams_[index],
                           (float) PostAmpEQ::bandTypeToInt (defaults.bands[index].type));
        endGesture (bandTypeParams_[index]);
        endGesture (bandQParams_[index]);
        endGesture (bandGainParams_[index]);
        endGesture (bandFreqParams_[index]);
        updateCache();
    }

    void resetPass (Pass pass)
    {
        auto* frequency = pass == Pass::HighPass ? highPassFreqParam_ : lowPassFreqParam_;
        auto* q = pass == Pass::HighPass ? highPassQParam_ : lowPassQParam_;

        beginGesture (frequency);
        beginGesture (q);
        setParameterValue (frequency, pass == Pass::HighPass ? 20.0f : 20000.0f);
        setParameterValue (q, 0.707f);
        endGesture (q);
        endGesture (frequency);
        updateCache();
    }

    void resetAllBands()
    {
        const PostAmpEQ::Settings defaults;

        for (int band = 0; band < numBands; ++band)
        {
            const auto index = (size_t) band;
            setParameterValue (bandFreqParams_[index], defaults.bands[index].frequency);
            setParameterValue (bandGainParams_[index], defaults.bands[index].gainDb);
            setParameterValue (bandQParams_[index], defaults.bands[index].q);
            setParameterValue (bandTypeParams_[index],
                               (float) PostAmpEQ::bandTypeToInt (defaults.bands[index].type));
        }

        setParameterValue (highPassFreqParam_, defaults.highPassFrequency);
        setParameterValue (highPassQParam_, defaults.highPassQ);
        setParameterValue (lowPassFreqParam_, defaults.lowPassFrequency);
        setParameterValue (lowPassQParam_, defaults.lowPassQ);
        updateCache();
    }

    void flatten()
    {
        for (int band = 0; band < numBands; ++band)
            setParameterValue (bandGainParams_[(size_t) band], 0.0f);

        setParameterValue (highPassFreqParam_, 20.0f);
        setParameterValue (lowPassFreqParam_, 20000.0f);
        updateCache();
    }

    //==========================================================================
    APVTS& state_;
    SignalSpectrum& spectrum_;
    std::function<double()> sampleRateProvider_;
    std::function<bool()> performanceMode_;

    FocusKnob boostKnob_;
    FocusKnob qKnob_;
    BandTypeSelector typeSelector_;
    juce::RangedAudioParameter* boostGestureParam_ = nullptr;
    juce::RangedAudioParameter* qGestureParam_ = nullptr;

    std::array<juce::String, PostAmpEQ::kNumBands> bandFreqIds_;
    std::array<juce::String, PostAmpEQ::kNumBands> bandGainIds_;
    std::array<juce::String, PostAmpEQ::kNumBands> bandQIds_;
    std::array<juce::String, PostAmpEQ::kNumBands> bandTypeIds_;

    std::array<juce::RangedAudioParameter*, PostAmpEQ::kNumBands> bandFreqParams_ {};
    std::array<juce::RangedAudioParameter*, PostAmpEQ::kNumBands> bandGainParams_ {};
    std::array<juce::RangedAudioParameter*, PostAmpEQ::kNumBands> bandQParams_ {};
    std::array<juce::RangedAudioParameter*, PostAmpEQ::kNumBands> bandTypeParams_ {};

    juce::RangedAudioParameter* highPassFreqParam_ = nullptr;
    juce::RangedAudioParameter* highPassQParam_ = nullptr;
    juce::RangedAudioParameter* lowPassFreqParam_ = nullptr;
    juce::RangedAudioParameter* lowPassQParam_ = nullptr;

    juce::ToggleButton bypassButton_;
    juce::TextButton flatButton_;
    std::unique_ptr<APVTS::ButtonAttachment> bypassAttachment_;

    juce::Rectangle<int> topArea_;
    juce::Rectangle<int> hintArea_;
    juce::Rectangle<int> focusArea_;
    juce::Rectangle<int> focusTitleArea_;
    juce::Rectangle<float> screenArea_;
    juce::Rectangle<float> plotArea_;

    PostAmpEQ::Settings cachedSettings_;
    bool cachedBypassed_ = false;
    int spectrumRevision_ = 0;
    std::array<float, PostAmpEQ::kNumBands> previousGains_ {};

    int selectedBand_ = 0;
    Pass selectedPass_ = Pass::None;
    int hoverBand_ = -1;
    Pass hoverPass_ = Pass::None;
    int activeBand_ = -1;
    DragTarget dragTarget_ = DragTarget::None;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PostEqEditor)
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
        applyUnitTheme (item);
    }

    /// Controls pick up the chassis colours of their unit: lettering flips to
    /// dark on light metal and gold panels, and knobs inherit the LED accent.
    void applyUnitTheme (juce::Component* item)
    {
        if (auto* control = dynamic_cast<ParamControl*> (item))
        {
            control->setUnitTheme (bodyColour_, accentColour_);
        }
        else if (auto* withKnob = dynamic_cast<FooterWithKnob*> (item))
        {
            if (auto* knob = dynamic_cast<ParamControl*> (withKnob->knobControl()))
                knob->setUnitTheme (bodyColour_, accentColour_);
        }
    }

    /// Performance mode stops the decorative hover timer; the glow snaps to
    /// the final state instead of animating.
    void setPerformanceMode (bool enabled)
    {
        if (performanceMode_ == enabled)
            return;

        performanceMode_ = enabled;
        highlight_ = hovered_ ? 1.0f : 0.0f;

        if (enabled)
            stopTimer();
        else
            startTimerHz (30);

        repaint();
    }

    void addRow (juce::Component* row, int height)
    {
        rows_.push_back ({ row, height, false });
        addAndMakeVisible (row);
        applyUnitTheme (row);
    }

    /// Adds a row that absorbs leftover vertical space. The supplied height is
    /// the minimum; extra space is divided between all flexible rows.
    void addFlexibleRow (juce::Component* row, int minimumHeight)
    {
        rows_.push_back ({ row, minimumHeight, true });
        addAndMakeVisible (row);
        applyUnitTheme (row);
    }

    void setFooter (juce::Component* footer, int height, int width = 64)
    {
        footer_ = footer;
        footerHeight_ = height;
        footerWidth_ = width;
        if (footer_ != nullptr)
        {
            addAndMakeVisible (footer_);
            applyUnitTheme (footer_);
        }
    }

    void setPreferredHeight (int height) { preferredHeight_ = height; }
    int preferredHeight() const { return preferredHeight_; }

    void setStretchesToWidth (bool shouldStretch) { stretchesToWidth_ = shouldStretch; }
    bool stretchesToWidth() const { return stretchesToWidth_; }

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

        if (style_ == UnitStyle::RackUnit)
        {
            g.setColour (accentColour_);
            g.setFont (baumansFont (12.0f));
            g.drawText (title_, getLocalBounds().withTrimmedLeft (30).removeFromTop (titleHeight()),
                        juce::Justification::centredLeft);
        }

        if (highlight_ > 0.001f)
        {
            g.setColour (accentColour_.withAlpha (0.22f * highlight_));
            g.drawRoundedRectangle (bounds.reduced (1.0f), 10.0f, 2.0f);
        }
    }

    void mouseEnter (const juce::MouseEvent&) override { hovered_ = true; }
    void mouseExit  (const juce::MouseEvent&) override { hovered_ = false; }

    void resized() override
    {
        auto r = getLocalBounds().reduced (pad_, pad_ / 2);
        r.removeFromTop (titleHeight());

        if (style_ == UnitStyle::Pedal)
        {
            if (footer_ != nullptr)
            {
                auto footerArea = r.removeFromBottom (footerHeight_);
                footer_->setBounds (footerArea.withSizeKeepingCentre (footerWidth_, footerHeight_));
            }

            const int count = (int) items_.size();
            const int gridRows = (count + columns_ - 1) / std::max (1, columns_);
            const int gridWidth = columns_ * ParamControl::cellWidth;
            const int gx = r.getX() + juce::jmax (0, (r.getWidth() - gridWidth) / 2);
            const int cellHeight = itemCellHeight_ > 0
                ? itemCellHeight_
                : ParamControl::cellHeight;

            const int contentHeight = gridRows * cellHeight + 12;

            auto layoutItems = [&] (int startY)
            {
                for (int row = 0; row < gridRows; ++row)
                {
                    const int rowStart = row * columns_;
                    const int rowCount = juce::jmin (columns_, count - rowStart);
                    const int rowWidth = rowCount * ParamControl::cellWidth;
                    const int rowGx = gx + (gridWidth - rowWidth) / 2;

                    for (int col = 0; col < rowCount; ++col)
                    {
                        const int index = rowStart + col;
                        items_[(size_t) index]->setBounds (
                            rowGx + col * ParamControl::cellWidth,
                            startY + row * cellHeight,
                            ParamControl::cellWidth,
                            cellHeight);
                    }
                }

                return startY + contentHeight;
            };

            auto layoutRows = [&] (int startY)
            {
                const auto heights = rowHeightsFor (juce::jmax (0, r.getBottom() - startY));
                int y = startY;
                size_t index = 0;

                for (auto& row : rows_)
                {
                    const int height = index < heights.size()
                        ? heights[index]
                        : row.height;

                    row.component->setBounds (r.getX(), y, r.getWidth(), height);
                    y += height + 12;
                    ++index;
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
            const auto heights = rowHeightsFor (juce::jmax (0, r.getBottom() - y));

            // The amp display sits on the grille cloth, so centre it against the
            // same painted region instead of pinning it under the faceplate.
            if (style_ == UnitStyle::AmpHead && !rows_.empty())
            {
                const auto cabinet = getLocalBounds().toFloat().reduced (3.0f);
                const auto grille = cabinet.reduced (12.0f)
                                        .withTrimmedTop (160.0f)
                                        .withTrimmedBottom (14.0f);
                const int rowHeight = heights.empty() ? rows_[0].height : heights[0];
                const int centredY = juce::roundToInt (grille.getCentreY())
                                     - rowHeight / 2;
                y = juce::jmax (y, centredY);
            }

            size_t rowIndex = 0;

            for (auto& row : rows_)
            {
                int rowX = r.getX() + 22;
                int rowW = r.getWidth() - 44;

                if (style_ == UnitStyle::AmpHead)
                {
                    // Keep the header row centred on the painted grille and
                    // let the model screen run nearly the full cloth width.
                    const auto cabinet = getLocalBounds().toFloat().reduced (3.0f);
                    const auto grille = cabinet.reduced (12.0f)
                                            .withTrimmedTop (160.0f)
                                            .withTrimmedBottom (14.0f);
                    rowW = juce::jmin (rowW, (int) grille.getWidth() - 56, 780);
                    rowX = juce::roundToInt (grille.getCentreX()) - rowW / 2;
                }

                const int height = rowIndex < heights.size()
                    ? heights[rowIndex]
                    : row.height;

                row.component->setBounds (rowX, y, rowW, height);
                y += height + 6;
                ++rowIndex;
            }
        }
    }

private:
    struct RowSpec
    {
        juce::Component* component;
        int height;
        bool flexible;
    };

    std::vector<int> rowHeightsFor (int availableHeight) const
    {
        std::vector<int> heights;
        heights.reserve (rows_.size());

        int needed = 0;
        int flexibleRows = 0;

        for (const auto& row : rows_)
        {
            heights.push_back (row.height);
            needed += row.height + 6;

            if (row.flexible)
                ++flexibleRows;
        }

        if (flexibleRows > 0)
        {
            const int extra = juce::jmax (0, availableHeight - needed) / flexibleRows;
            size_t index = 0;

            for (const auto& row : rows_)
            {
                if (row.flexible)
                    heights[index] += extra;

                ++index;
            }
        }

        return heights;
    }

    int titleHeight() const
    {
        switch (style_)
        {
            case UnitStyle::Pedal:    return 30;
            case UnitStyle::AmpHead:  return 34;
            default:                  return 26;
        }
    }

    void paintPedal (juce::Graphics& g, juce::Rectangle<float> b)
    {
        // Shadow on the board.
        g.setColour (juce::Colours::black.withAlpha (0.50f));
        g.fillRoundedRectangle (b.translated (0.0f, 6.0f), 16.0f);

        // Die-cast enclosure: lit at the top, richer at the bottom, with
        // side shading for a three-dimensional look.
        {
            juce::ColourGradient gradient (bodyColour_.brighter (0.28f),
                                          b.getCentreX(), b.getY(),
                                          bodyColour_.darker (0.42f),
                                          b.getCentreX(), b.getBottom(), false);
            gradient.addColour (0.5, bodyColour_);
            g.setGradientFill (gradient);
            g.fillRoundedRectangle (b, 16.0f);
        }

        // Side shading: darker at the left and right edges.
        {
            juce::ColourGradient side (juce::Colours::black.withAlpha (0.22f),
                                      b.getX(), b.getCentreY(),
                                      juce::Colours::transparentBlack,
                                      b.getX() + b.getWidth() * 0.18f,
                                      b.getCentreY(), false);
            side.addColour (1.0, juce::Colours::black.withAlpha (0.22f));
            g.setGradientFill (side);
            g.fillRoundedRectangle (b, 16.0f);
        }

        // Top bevel highlight and dark outline.
        g.setColour (juce::Colours::white.withAlpha (0.26f));
        g.drawRoundedRectangle (b.reduced (2.0f), 14.0f, 1.2f);
        g.setColour (juce::Colours::black.withAlpha (0.60f));
        g.drawRoundedRectangle (b, 16.0f, 1.5f);

        // Screen-printed title, placed right on top of the footswitch with a
        // much larger font so it reads like a pedal name plate. Multi-word
        // names are split across two centred lines.
        // Place the name plate right above the footswitch at the bottom of
        // the pedal. (withBottom + withHeight keeps the top fixed, so compute
        // the top explicitly instead of chaining.)
        auto titleArea = juce::Rectangle<float> (
            b.getX() + 13.0f,
            b.getBottom() - 140.0f,
            b.getWidth() - 26.0f,
            40.0f);
        const juce::String titleText = title_.replace (" ", "\n");

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.setFont (baumansFont (22.0f));
        g.drawFittedText (titleText, titleArea.translated (0.0f, 1.5f).toNearestInt(),
                          juce::Justification::centred, 2, 0.7f);

        g.setColour (juce::Colours::white.withAlpha (0.95f));
        g.setFont (baumansFont (22.0f));
        g.drawFittedText (titleText, titleArea.toNearestInt(),
                          juce::Justification::centred, 2, 0.7f);

        // Stronger LED jewel with a glow halo.
        // (LED is drawn by the Footswitch; nothing extra here.)

        drawScrew (g, b.getX() + 11.0f, b.getY() + 11.0f, 3.5f);
        drawScrew (g, b.getRight() - 11.0f, b.getY() + 11.0f, 3.5f);
        drawScrew (g, b.getX() + 11.0f, b.getBottom() - 11.0f, 3.5f);
        drawScrew (g, b.getRight() - 11.0f, b.getBottom() - 11.0f, 3.5f);
    }

    void paintAmp (juce::Graphics& g, juce::Rectangle<float> b)
    {
        g.setColour (juce::Colours::black.withAlpha (0.50f));
        g.fillRoundedRectangle (b.translated (0.0f, 5.0f), 12.0f);

        // Tolex cabinet with a subtle pebble texture.
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xff232329), b.getX(), b.getY(),
            juce::Colour (0xff0c0c0e), b.getX(), b.getBottom(), false));
        g.fillRoundedRectangle (b, 12.0f);
        g.setColour (juce::Colours::white.withAlpha (0.03f));
        for (float y = b.getY() + 4.0f; y < b.getBottom(); y += 5.0f)
            g.drawHorizontalLine ((int) y, b.getX() + 6.0f, b.getRight() - 6.0f);

        // Gold faceplate behind the knobs.
        auto face = b.reduced (10.0f).withTrimmedTop (6.0f).withHeight (145.0f);
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xffe6d4a2), face.getX(), face.getY(),
            juce::Colour (0xff9c8655), face.getX(), face.getBottom(), false));
        g.fillRoundedRectangle (face, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        for (float y = face.getY() + 3.0f; y < face.getBottom(); y += 3.0f)
            g.drawHorizontalLine ((int) y, face.getX() + 4.0f, face.getRight() - 4.0f);
        g.setColour (juce::Colour (0xff5a4a28));
        g.drawRoundedRectangle (face, 6.0f, 1.2f);

        // Etched model name on the faceplate.
        g.setColour (juce::Colour (0xff4a3a1c));
        g.setFont (baumansFont (12.0f));
        g.drawText (title_, face.withTrimmedBottom (111.0f).reduced (12.0f, 0.0f),
                    juce::Justification::centredLeft);
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawHorizontalLine ((int) (face.getY() + 34.0f),
                              face.getX() + 8.0f, face.getRight() - 8.0f);

        // Grille cloth with a woven crosshatch.
        auto grille = b.reduced (12.0f).withTrimmedTop (160.0f).withTrimmedBottom (14.0f);
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xff1a1a1e), grille.getX(), grille.getY(),
            juce::Colour (0xff09090b), grille.getX(), grille.getBottom(), false));
        g.fillRoundedRectangle (grille, 7.0f);
        g.setColour (juce::Colour (0xff232329));
        for (float x = grille.getX() + 6.0f; x < grille.getRight(); x += 9.0f)
            g.drawLine (x, grille.getY() + 6.0f, x, grille.getBottom() - 6.0f, 0.7f);
        for (float y = grille.getY() + 6.0f; y < grille.getBottom(); y += 9.0f)
            g.drawLine (grille.getX() + 6.0f, y, grille.getRight() - 6.0f, y, 0.7f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawRoundedRectangle (grille, 7.0f, 1.2f);

        // Jewel pilot light.
        const auto jewel = juce::Rectangle<float> (9.0f, 9.0f)
                               .withCentre ({ b.getRight() - 30.0f, b.getY() + 21.0f });
        g.setColour (juce::Colour (0xffff5a3c).withAlpha (0.35f));
        g.fillEllipse (jewel.expanded (5.0f));
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xffff9c6e), jewel.getX(), jewel.getY(),
            juce::Colour (0xffc22b12), jewel.getX(), jewel.getBottom(), false));
        g.fillEllipse (jewel);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.fillEllipse (jewel.getX() + 2.0f, jewel.getY() + 1.5f, 2.6f, 2.6f);

        drawScrew (g, b.getX() + 10.0f, b.getY() + 10.0f, 3.5f);
        drawScrew (g, b.getRight() - 10.0f, b.getY() + 10.0f, 3.5f);
        drawScrew (g, b.getX() + 10.0f, b.getBottom() - 10.0f, 3.5f);
        drawScrew (g, b.getRight() - 10.0f, b.getBottom() - 10.0f, 3.5f);
    }

    void paintRack (juce::Graphics& g, juce::Rectangle<float> b)
    {
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (b.translated (0.0f, 4.0f), 6.0f);

        // Brushed aluminium front panel.
        drawBrushedPanel (g, b, juce::Colour (0xffe2e6f0), juce::Colour (0xffb3bacb), 6.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (b, 6.0f, 1.5f);

        // Rack ears.
        auto leftEar = b.withWidth (22.0f);
        auto rightEar = b.withTrimmedLeft (b.getWidth() - 22.0f);
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xffc9cfdd), leftEar.getX(), leftEar.getY(),
            juce::Colour (0xff8f96ab), leftEar.getX(), leftEar.getBottom(), false));
        g.fillRect (leftEar);
        g.fillRect (rightEar);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawLine (leftEar.getRight(), leftEar.getY(), leftEar.getRight(), leftEar.getBottom());
        g.drawLine (rightEar.getX(), rightEar.getY(), rightEar.getX(), rightEar.getBottom());

        drawScrew (g, leftEar.getCentreX(), b.getY() + 18.0f, 3.4f);
        drawScrew (g, leftEar.getCentreX(), b.getBottom() - 18.0f, 3.4f);
        drawScrew (g, rightEar.getCentreX(), b.getY() + 18.0f, 3.4f);
        drawScrew (g, rightEar.getCentreX(), b.getBottom() - 18.0f, 3.4f);

        // Accent strip under the title.
        g.setColour (accentColour_);
        g.fillRect (b.getX() + 26.0f, b.getY() + 22.0f, b.getWidth() - 52.0f, 2.0f);

        // Vent slots bottom-right.
        g.setColour (juce::Colours::black.withAlpha (0.30f));
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
    bool performanceMode_ = false;
    std::vector<juce::Component*> items_;
    std::vector<RowSpec> rows_;
    juce::Component* footer_ = nullptr;
    int footerHeight_ = 0;
    int footerWidth_ = 64;
    int preferredHeight_ = 0;
    int itemCellHeight_ = 0;
    bool rowsFirst_ = false;
    bool stretchesToWidth_ = false;
    float highlight_ = 0.0f;
    bool hovered_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SectionPanel)
};

//==============================================================================
/// Pedal model picker: an LCD screen that opens the folder popup, a Load
/// button for the system file chooser, previous/next arrows and drag-and-drop.
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
        screen_ = std::make_unique<FileScreen> (juce::String(), "No model loaded");
        screen_->setName ("ModelScreen");
        addAndMakeVisible (*screen_);

        addAndMakeVisible (loadButton_);
        loadButton_.setButtonText ("LOAD");
        loadButton_.setTooltip ("Choose a .nam file from disk");
        loadButton_.onClick = [this]
        {
            if (onLoad_ != nullptr)
                onLoad_();
        };

        addAndMakeVisible (prevButton_);
        prevButton_.setButtonText ("<");
        prevButton_.setTooltip ("Previous .nam file in this folder");
        prevButton_.onClick = [this] { browse (-1); };

        addAndMakeVisible (nextButton_);
        nextButton_.setButtonText (">");
        nextButton_.setTooltip ("Next .nam file in this folder");
        nextButton_.onClick = [this] { browse (1); };
    }

    void setOnOpenPopup (std::function<void (juce::Component&)> callback)
    {
        screen_->setOnOpenPopup (std::move (callback));
    }

    void setOnLoad (std::function<void()> callback)
    {
        onLoad_ = std::move (callback);
    }

    bool loadFile (const juce::File& file) { return load (file); }

    void setDisplayedFileName (const juce::String& name)
    {
        screen_->setFileName (name);
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

        g.setColour (juce::Colour (dragOver_ ? 0xff242b3a : 0xff1a1f2b));
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (juce::Colour (dragOver_ ? kScreenGlow : kScreenLine));
        g.drawRoundedRectangle (bounds, 6.0f, dragOver_ ? 2.0f : 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4);
        r.removeFromTop (12);

        screen_->setBounds (r.removeFromTop (42));
        r.removeFromTop (4);

        loadButton_.setBounds (r.removeFromTop (34));
        r.removeFromTop (4);

        // Slim arrow buttons beside the load button.
        const int arrowWidth = 28;
        const int gap = 6;
        const int pairWidth = arrowWidth * 2 + gap;
        auto arrows = r.withSizeKeepingCentre (pairWidth, r.getHeight());
        prevButton_.setBounds (arrows.removeFromLeft (arrowWidth));
        arrows.removeFromLeft (gap);
        nextButton_.setBounds (arrows.removeFromLeft (arrowWidth));
    }

private:
    void browse (int delta)
    {
        const auto current = getCurrentFile_ != nullptr ? getCurrentFile_()
                                                        : juce::File();
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
    std::function<void()> onLoad_;
    std::unique_ptr<FileScreen> screen_;
    juce::TextButton prevButton_, loadButton_, nextButton_;
    bool dragOver_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamLoadBox)
};

//==============================================================================
/// An IR picker row: an LCD screen that opens the folder popup, a Load button
/// for the system file chooser, arrows and drag-and-drop. Used by both cabinet
/// slots and the single-slot reverb IR pedal.
class IrPickRow : public juce::Component,
                  public juce::FileDragAndDropTarget,
                  private juce::Timer
{
public:
    IrPickRow (juce::String caption,
               juce::String screenName,
               std::function<bool (const juce::File&)> loader,
               std::function<juce::File()> getRootDirectory,
               std::function<juce::File()> getCurrentFile)
        : caption_ (std::move (caption)),
          loader_ (std::move (loader)),
          getRootDirectory_ (std::move (getRootDirectory)),
          getCurrentFile_ (std::move (getCurrentFile))
    {
        startTimer (250);

        screen_ = std::make_unique<FileScreen> (caption_, "No IR loaded");
        screen_->setName (std::move (screenName));
        addAndMakeVisible (*screen_);

        addAndMakeVisible (loadButton_);
        loadButton_.setButtonText ("LOAD");
        loadButton_.setTooltip ("Choose an IR file from disk");
        loadButton_.onClick = [this]
        {
            if (onLoad_ != nullptr)
                onLoad_();
        };

        addAndMakeVisible (prevButton_);
        prevButton_.setButtonText ("<");
        prevButton_.setTooltip ("Previous IR in this folder");
        prevButton_.onClick = [this] { browse (-1); };

        addAndMakeVisible (nextButton_);
        nextButton_.setButtonText (">");
        nextButton_.setTooltip ("Next IR in this folder");
        nextButton_.onClick = [this] { browse (1); };

        refreshDisplay();
    }

    void setOnOpenPopup (std::function<void (juce::Component&)> callback)
    {
        screen_->setOnOpenPopup (std::move (callback));
    }

    void setOnLoad (std::function<void()> callback)
    {
        onLoad_ = std::move (callback);
    }

    bool loadFile (const juce::File& file) { return load (file); }

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
        g.setColour (juce::Colour (dragOver_ ? 0xffd7d9f2 : kPanel));
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (juce::Colour (dragOver_ ? kAccent : kPanelLine));
        g.drawRoundedRectangle (b, 6.0f, dragOver_ ? 2.0f : 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);

        // Keep the filename screen as wide as possible; the file controls sit
        // in a narrow strip so long IR names remain readable.
        auto controls = r.removeFromRight (66);
        loadButton_.setBounds (controls.removeFromTop (26));
        controls.removeFromTop (3);

        auto left = controls.removeFromLeft (controls.getWidth() / 2);
        prevButton_.setBounds (left.withTrimmedRight (3));
        nextButton_.setBounds (controls.withTrimmedLeft (3));

        screen_->setBounds (r);
    }

private:
    void browse (int delta)
    {
        const auto current = getCurrentFile_ != nullptr ? getCurrentFile_()
                                                        : juce::File();
        const auto root = getRootDirectory_ != nullptr ? getRootDirectory_()
                                                       : juce::File();
        const auto directory = current.existsAsFile()
            ? current.getParentDirectory()
            : root;

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
        if (loader_ != nullptr && loader_ (file))
        {
            refreshDisplay();
            return true;
        }

        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Could not load IR",
            "NAMberry could not load: " + file.getFileName());

        return false;
    }

    void refreshDisplay()
    {
        if (getCurrentFile_ != nullptr)
            screen_->setFileName (getCurrentFile_().getFileName());
    }

    void timerCallback() override { refreshDisplay(); }

    juce::String caption_;
    std::function<bool (const juce::File&)> loader_;
    std::function<juce::File()> getRootDirectory_;
    std::function<juce::File()> getCurrentFile_;
    std::unique_ptr<FileScreen> screen_;
    juce::TextButton prevButton_, loadButton_, nextButton_;
    std::function<void()> onLoad_;
    bool dragOver_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IrPickRow)
};

//==============================================================================
/// Centers a single control in a row while keeping the standard knob footprint.
class CenteredRow : public juce::Component
{
public:
    explicit CenteredRow (juce::Component* item) : item_ (item)
    {
        addAndMakeVisible (item_);
    }

    void resized() override
    {
        item_->setBounds (getLocalBounds().withSizeKeepingCentre (
            ParamControl::cellWidth, ParamControl::cellHeight));
    }

private:
    juce::Component* item_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CenteredRow)
};

/// Top row of the amp head: the wide "current amp" screen centred on the
/// grille cloth with the previous/next arrows flanking it and the load button
/// tucked underneath.
class AmpHeaderRow : public juce::Component
{
public:
    AmpHeaderRow (juce::Component& screen, juce::Component& browse,
                  std::function<void()> onPrevious, std::function<void()> onNext)
        : screen_ (screen), browse_ (browse)
    {
        addAndMakeVisible (screen_);
        addAndMakeVisible (browse_);

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

        auto loadRow = r.removeFromBottom (36);
        browse_.setBounds (loadRow.withSizeKeepingCentre (110, 34));
        r.removeFromBottom (8);

        layout (r);
    }

private:
    void layout (juce::Rectangle<int> r)
    {
        const int arrowW = juce::jmin (48, juce::jmax (34, r.getWidth() / 12));
        const int gap = 10;
        const int buttonH = juce::jmin (44, r.getHeight());

        prevButton_.setBounds (r.getX(), r.getCentreY() - buttonH / 2, arrowW, buttonH);
        nextButton_.setBounds (r.getRight() - arrowW, r.getCentreY() - buttonH / 2,
                               arrowW, buttonH);

        const int screenX = r.getX() + arrowW + gap;
        screen_.setBounds (screenX, r.getY(),
                           juce::jmax (0, r.getRight() - arrowW - gap - screenX),
                           r.getHeight());
    }

    juce::Component& screen_;
    juce::Component& browse_;
    juce::TextButton prevButton_, nextButton_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpHeaderRow)
};

} // namespace

//==============================================================================
/// Full-editor overlay showing a deployable list of the files in a folder.
/// Clicking a file loads it; clicking anywhere outside the panel closes it.
class FilePopup : public juce::Component,
                  private juce::ListBoxModel
{
public:
    FilePopup ()
    {
        setName ("FilePopup");
        setWantsKeyboardFocus (true);

        addAndMakeVisible (list_);
        list_.setModel (this);
        list_.setRowHeight (30);
        list_.setMultipleSelectionEnabled (false);
        list_.setColour (juce::ListBox::backgroundColourId,
                         juce::Colours::transparentBlack);
        list_.setColour (juce::ListBox::textColourId, juce::Colour (kText));
        list_.setColour (juce::ListBox::outlineColourId, juce::Colour (kPanelLine));
        list_.setOutlineThickness (1);

        setVisible (false);
    }

    void open (juce::Rectangle<int> anchor,
               const juce::File& directory,
               juce::StringArray extensions,
               std::function<void (const juce::File&)> onFileChosen,
               std::function<void (const juce::File&)> onDirectoryChanged)
    {
        anchor_ = anchor;
        extensions_ = std::move (extensions);
        onFileChosen_ = std::move (onFileChosen);
        onDirectoryChanged_ = std::move (onDirectoryChanged);

        current_ = directory.isDirectory()
            ? directory
            : juce::File::getSpecialLocation (juce::File::userHomeDirectory);

        setVisible (true);
        resized();
        refresh();
        toFront (true);
        grabKeyboardFocus();
    }

    void close()
    {
        if (! isVisible())
            return;

        // Remember the folder the user was browsing as the next starting point.
        if (current_.isDirectory() && onDirectoryChanged_ != nullptr)
            onDirectoryChanged_ (current_);

        setVisible (false);
    }

    bool isOpen() const { return isVisible(); }

    void paint (juce::Graphics& g) override
    {
        if (! isVisible())
            return;

        g.fillAll (juce::Colours::black.withAlpha (0.22f));

        auto panel = panelBounds_.toFloat();

        g.setGradientFill (juce::ColourGradient (
            juce::Colour (kPanel), panel.getX(), panel.getY(),
            juce::Colour (0xffe7e9f2), panel.getX(), panel.getBottom(), false));
        g.fillRoundedRectangle (panel, 10.0f);

        g.setColour (juce::Colour (kPanelLineStrong));
        g.drawRoundedRectangle (panel, 10.0f, 1.0f);

        auto header = panelBounds_.reduced (14, 10);
        header.removeFromBottom (8);

        g.setColour (juce::Colour (kText));
        g.setFont (baumansFont (15.0f));
        g.drawFittedText (current_.getFileName().isNotEmpty()
                              ? current_.getFileName()
                              : current_.getFullPathName(),
                          header.removeFromTop (20),
                          juce::Justification::centredLeft, 1);

        g.setColour (juce::Colour (kTextDim));
        g.setFont (baumansFont (9.5f));
        g.drawFittedText (current_.getFullPathName(), header.removeFromTop (14),
                          juce::Justification::centredLeft, 1);

        if (entries_.empty())
        {
            g.setColour (juce::Colour (kTextDim));
            g.setFont (baumansFont (12.0f));
            g.drawFittedText ("No matching files in this folder",
                              list_.getBounds(), juce::Justification::centred, 2);
        }
    }

    void resized() override
    {
        const int panelW = juce::jlimit (220, 380,
                                         juce::jmax (220, getWidth() - 32));
        const int panelH = juce::jlimit (170, 420,
                                         juce::jmax (170, getHeight() - 32));

        int x = juce::jlimit (8, juce::jmax (8, getWidth() - panelW - 8),
                              anchor_.getX());
        int y = anchor_.getBottom() + 6;

        if (y + panelH > getHeight() - 8)
            y = anchor_.getY() - panelH - 6;

        y = juce::jlimit (8, juce::jmax (8, getHeight() - panelH - 8), y);

        panelBounds_ = juce::Rectangle<int> (x, y, panelW, panelH);

        auto r = panelBounds_.reduced (14, 10);
        r.removeFromTop (34);
        list_.setBounds (r.withTrimmedBottom (8));
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            close();
            return true;
        }

        return false;
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        // Any touch outside the panel dismisses the menu.
        if (! panelBounds_.contains (event.getPosition()))
            close();
    }

private:
    struct Entry
    {
        juce::File file;
        juce::String name;
        bool isDirectory = false;
    };

    int getNumRows() override { return static_cast<int> (entries_.size()); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height,
                           bool selected) override
    {
        if (row < 0 || row >= static_cast<int> (entries_.size()))
            return;

        const auto& entry = entries_[static_cast<size_t> (row)];
        auto bounds = juce::Rectangle<int> (0, 0, width, height);

        if (selected)
            g.fillAll (juce::Colour (0xffcfd4ee));
        else if (row % 2 == 1)
            g.fillAll (juce::Colour (0x0a000030));

        auto iconArea = bounds.removeFromLeft (30).reduced (7, 6);

        if (entry.isDirectory)
        {
            g.setColour (juce::Colour (kAccent));
            g.fillRoundedRectangle (iconArea.toFloat(), 2.0f);
            g.setColour (juce::Colour (kPanel));
            g.fillRect (iconArea.getX(), iconArea.getY() - 2,
                        iconArea.getWidth() / 2, 3);
        }
        else
        {
            g.setColour (juce::Colour (kTextDim));
            g.drawRoundedRectangle (iconArea.toFloat().reduced (2.0f), 2.0f, 1.0f);
            g.drawLine (iconArea.getX() + 4.0f,
                        static_cast<float> (iconArea.getCentreY()),
                        iconArea.getRight() - 4.0f,
                        static_cast<float> (iconArea.getCentreY()), 1.0f);
        }

        g.setColour (entry.isDirectory ? juce::Colour (kText)
                                       : juce::Colour (kTextDim));
        g.setFont (baumansFont (13.0f));
        g.drawFittedText (entry.name, bounds.reduced (6, 0),
                          juce::Justification::centredLeft, 1);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        if (row < 0 || row >= static_cast<int> (entries_.size()))
            return;

        const auto entry = entries_[static_cast<size_t> (row)];

        if (entry.isDirectory)
        {
            navigateTo (entry.file);
            return;
        }

        if (onFileChosen_ != nullptr)
            onFileChosen_ (entry.file);

        close();
    }

    void navigateTo (const juce::File& directory)
    {
        if (! directory.isDirectory() || directory == current_)
            return;

        current_ = directory;
        refresh();
        repaint();
    }

    void refresh()
    {
        entries_.clear();

        if (auto parent = current_.getParentDirectory();
            parent.isDirectory() && parent != current_)
        {
            entries_.push_back ({ parent, "..", true });
        }

        std::vector<Entry> directories;
        std::vector<Entry> files;

        for (auto entry : juce::RangedDirectoryIterator (
                 current_, false, "*", juce::File::findFiles | juce::File::findDirectories))
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

        list_.deselectAllRows();
        list_.updateContent();
    }

    bool hasMatchingExtension (const juce::File& file) const
    {
        if (extensions_.isEmpty())
            return true;

        for (const auto& extension : extensions_)
            if (file.hasFileExtension (extension))
                return true;

        return false;
    }

    juce::File current_;
    juce::Rectangle<int> anchor_;
    juce::Rectangle<int> panelBounds_;
    juce::StringArray extensions_;
    std::vector<Entry> entries_;

    std::function<void (const juce::File&)> onFileChosen_;
    std::function<void (const juce::File&)> onDirectoryChanged_;

    juce::ListBox list_ { "files", nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilePopup)
};

//==============================================================================
NAMBerryEditor::NAMBerryEditor (NAMBerryProcessor& p)
    : AudioProcessorEditor (&p), processor_ (p),
      tabBar_ (juce::TabbedButtonBar::TabsAtTop)
{
    lnf_ = std::make_unique<HardwareLookAndFeel>();
    setLookAndFeel (lnf_.get());

    addAndMakeVisible (reducedMotionButton_);
    reducedMotionButton_.setButtonText ("Performance");
    reducedMotionButton_.setTooltip ("Performance mode: stops UI animations and the live spectrum to save CPU");
    // Dark lettering: the toggle now sits on the light header chrome.
    reducedMotionButton_.setColour (juce::ToggleButton::textColourId,
                                    juce::Colour (kText));
    reducedMotionAttachment_ = std::make_unique<APVTS::ButtonAttachment> (
        processor_.getValueTreeState(), "reducedMotion", reducedMotionButton_);
    performanceParam_ = processor_.getValueTreeState()
                            .getRawParameterValue ("reducedMotion");

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

    performanceMode_ = performanceParam_ != nullptr && performanceParam_->load() > 0.5f;

    if (performanceMode_)
        for (auto* panel : allPanels_)
            if (auto* section = dynamic_cast<SectionPanel*> (panel))
                section->setPerformanceMode (true);

    bottomKnobs_ = std::make_unique<BottomKnobs> (processor_.getValueTreeState());
    addAndMakeVisible (*bottomKnobs_);

    inputGainKnob_ = std::make_unique<ParamControl> (processor_.getValueTreeState(),
                                                     "inputGain", "In");
    if (auto* control = dynamic_cast<ParamControl*> (inputGainKnob_.get()))
        control->setDarkText (true);
    addAndMakeVisible (*inputGainKnob_);

    setResizable (true, true);
    setResizeLimits (1000, 650, 4000, 1200);
    setSize (1280, 720);

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

    auto makeFootswitch = [this, &state] (const juce::String& id, juce::Colour led,
                                          juce::Colour body)
    {
        auto fs = std::make_unique<Footswitch> (state, id, led, body);
        auto* raw = fs.get();
        ownedControls_.push_back (std::move (fs));
        return raw;
    };

    // Screen + Load button wiring for one kind of file (amp, pedal, IR).
    struct PickerCallbacks
    {
        std::function<void (juce::Component&)> openPopup;
        std::function<void()> openLoad;
    };

    auto makePicker = [this] (const juce::String& chooserTitle,
                              juce::StringArray extensions,
                              std::function<juce::File()> getFolder,
                              std::function<void (const juce::File&)> setFolder,
                              std::function<bool (const juce::File&)> load) -> PickerCallbacks
    {
        auto wildcard = [extensions]
        {
            juce::String pattern;

            for (const auto& extension : extensions)
                pattern << (pattern.isEmpty() ? "" : ";") << "*" << extension;

            return pattern.isNotEmpty() ? pattern : juce::String ("*");
        };

        PickerCallbacks callbacks;

        callbacks.openPopup = [this, extensions, getFolder, setFolder, load]
                              (juce::Component& anchor)
        {
            openFilePopup (anchor, getFolder(), extensions,
                           [load] (const juce::File& file) { load (file); },
                           [setFolder] (const juce::File& directory)
                           {
                               setFolder (directory);
                           });
        };

        callbacks.openLoad = [this, chooserTitle, wildcard, getFolder, setFolder, load]
        {
            openFileChooser (chooserTitle, getFolder(), wildcard(),
                             [load, setFolder] (const juce::File& file)
                             {
                                 if (load (file))
                                     setFolder (file.getParentDirectory());
                             });
        };

        return callbacks;
    };

    auto makeNamBox = [this, &makePicker] (const juce::String& caption, bool pedal)
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

        const auto picker = makePicker (
            pedal ? "Choose a NAM pedal model" : "Choose a NAM amp model",
            juce::StringArray { ".nam" },
            [this, pedal]
            {
                auto* loader = pedal ? processor_.getNamPedal()
                                     : processor_.getNamModel();
                const auto current = loader != nullptr ? loader->getLoadedFile()
                                                       : juce::File();

                return folderOf (current, pedal ? processor_.getPedalDirectory()
                                                : processor_.getNamDirectory());
            },
            [this, pedal] (const juce::File& directory)
            {
                if (pedal)
                    processor_.setPedalDirectory (directory);
                else
                    processor_.setNamDirectory (directory);
            },
            [raw] (const juce::File& file) { return raw->loadFile (file); });

        raw->setOnOpenPopup (picker.openPopup);
        raw->setOnLoad (picker.openLoad);

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
    tabBar_.addTab ("Pre FX", juce::Colours::transparentBlack, -1);
    tabBar_.addTab ("Amp", juce::Colours::transparentBlack, -1);
    tabBar_.addTab ("Post EQ", juce::Colours::transparentBlack, -1);
    tabBar_.addTab ("IR", juce::Colours::transparentBlack, -1);
    tabBar_.addTab ("Post FX", juce::Colours::transparentBlack, -1);
    tabBar_.setCurrentTabIndex (currentTabIndex_, false);

    const char* tabNames[] = { "Pre FX", "Amp", "Post EQ", "IR", "Post FX" };

    for (int i = 0; i < tabBar_.getNumTabs() && i < 5; ++i)
        if (auto* button = tabBar_.getTabButton (i))
            button->setTooltip (juce::String (tabNames[i])
                                + " — right-click to bypass this section");

    updateTabVisuals();

    // Unit colours. Body hues are bright die-cast stompbox finishes that stay
    // distinct against the white and indigo chrome; accents are the LED /
    // knob-ring colours and stay light enough to glow on dark bodies.
    const juce::Colour impersonatorBody (0xffc74736), impersonatorAccent (0xffff8f66);
    const juce::Colour capoBody     (juce::Colour (0xffc98322).darker (0.30f)), capoAccent     (0xffffc94d);
    const juce::Colour gateBody     (0xff5c6a85), gateAccent     (0xffc3cfe6);
    const juce::Colour compBody     (juce::Colour (0xffc25f22).darker (0.25f)), compAccent     (0xffffa25c);
    const juce::Colour acousticBody (juce::Colour (0xffc9995e).darker (0.35f)), acousticAccent (0xfff2d9ae);
    const juce::Colour pedalBody    (0xff2e8f57), pedalAccent    (0xff6ee39a);
    const juce::Colour eqBody       (0xff5b62b8), eqAccent       (0xffa3a9f2);
    const juce::Colour faceplateGold (0xffd8c08a), ampAccent     (0xffd4af37);
    const juce::Colour rackBody     (0xffdfe3ee), rackAccent     (0xff0e0052);
    const juce::Colour chorusBody   (0xff3584c9), chorusAccent   (0xff7cc4f5);
    const juce::Colour flangerBody  (0xff7a4fc9), flangerAccent  (0xffb794f6);
    const juce::Colour phaserBody   (juce::Colour (0xffc2477f).darker (0.20f)), phaserAccent   (0xfff47ab3);
    const juce::Colour delayBody    (juce::Colour (0xffd9a441).darker (0.35f)), delayAccent    (0xffffcf5c);
    const juce::Colour reverbBody   (0xff2f5fbf), reverbAccent   (0xff74a7f2);
    const juce::Colour reverbIrBody (0xff1d8496), reverbIrAccent (0xff66d9e8);

    // 1. Pre FX -------------------------------------------------------------
    // The tuner sits permanently in the editor header, so the pedalboard keeps
    // its full height for the pedals.
    {
        auto tuner = std::make_unique<TunerStrip> (state);
        auto* rawTuner = tuner.get();
        ownedControls_.push_back (std::move (tuner));
        addAndMakeVisible (*rawTuner);
        headerTuner_ = rawTuner;

        if (auto* t = processor_.getDropTuner())
            rawTuner->setState (t->getCentsDeviation(),
                                t->getDetectedNote(),
                                t->getReferenceFrequency());

        tunerUpdater_ = [this, rawTuner]
        {
            if (auto* tunerPtr = processor_.getDropTuner())
                rawTuner->setState (tunerPtr->getCentsDeviation(),
                                    tunerPtr->getDetectedNote(),
                                    tunerPtr->getReferenceFrequency());
        };
    }

    if (auto* impersonatorPanel = makePanel (0, "ALTER EGO", UnitStyle::Pedal, 1, 120,
                                             impersonatorBody, impersonatorAccent))
    {
        auto mode = std::make_unique<ModeSwitch> (state, "impersonatorToNeck");
        auto* rawMode = mode.get();
        ownedControls_.push_back (std::move (mode));
        impersonatorPanel->setItemCellHeight (148);
        impersonatorPanel->addItem (rawMode);
        impersonatorPanel->addItem (makeControl ("impersonatorLevel", "Level"));
        impersonatorPanel->setFooter (
            makeFootswitch ("impersonatorBypass", impersonatorAccent, impersonatorBody), 92);
    }

    if (auto* pitchPanel = makePanel (0, "PITCH", UnitStyle::Pedal, 2, 164,
                                      capoBody, capoAccent))
    {
        pitchPanel->addItem (makeControl ("capoFret", "Semitones"));
        pitchPanel->addItem (makeControl ("capoTone", "Tone Match"));
        pitchPanel->addItem (makeControl ("capoMix", "Mix"));
        pitchPanel->addItem (makeControl ("capoQuality", "Quality", ParamControl::Kind::Toggle));
        pitchPanel->setFooter (makeFootswitch ("capoBypass", capoAccent, capoBody), 92);
    }

    if (auto* gate = makePanel (0, "GATE", UnitStyle::Pedal, 1, 120,
                                gateBody, gateAccent))
    {
        gate->addItem (makeControl ("gateThreshold", "Threshold"));
        gate->addItem (makeControl ("gateAttack", "Attack"));
        gate->addItem (makeControl ("gateRelease", "Release"));
        gate->setFooter (makeFootswitch ("gateBypass", gateAccent, gateBody), 92);
    }

    if (auto* acoustic = makePanel (0, "ACOUSTIC", UnitStyle::Pedal, 2, 164,
                                    acousticBody, acousticAccent))
    {
        auto mode = makeControl ("acousticSimMode", "Mode");
        mode->setStepSize (1.0);
        acoustic->setItemCellHeight (120);
        acoustic->addItem (mode);
        acoustic->addItem (makeControl ("acousticSimTop", "Top"));
        acoustic->addItem (makeControl ("acousticSimBody", "Body"));
        acoustic->addItem (makeControl ("acousticSimPiezo", "Piezo"));
        acoustic->addItem (makeControl ("acousticSimLevel", "Level"));
        acoustic->setFooter (makeFootswitch ("acousticSimBypass", acousticAccent, acousticBody), 92);
    }

    if (auto* comp = makePanel (0, "COMP", UnitStyle::Pedal, 2, 164,
                                compBody, compAccent))
    {
        comp->addItem (makeControl ("compThreshold", "Threshold"));
        comp->addItem (makeControl ("compRatio", "Ratio"));
        comp->addItem (makeControl ("compAttack", "Attack"));
        comp->addItem (makeControl ("compRelease", "Release"));
        comp->addItem (makeControl ("compMakeup", "Makeup"));
        comp->addItem (makeControl ("compTight", "Tight", ParamControl::Kind::Toggle));
        comp->setFooter (makeFootswitch ("compBypass", compAccent, compBody), 92);
    }

    if (auto* pedal = makePanel (0, "PEDAL", UnitStyle::Pedal, 3, 238,
                                 pedalBody, pedalAccent))
    {
        // Top row: tone stack. Bottom row: gain and output level. Compact cells
        // leave room for the model box at the default editor height.
        pedal->setItemCellHeight (80);
        pedal->addItem (makeControl ("pedalBass", "Bass"));
        pedal->addItem (makeControl ("pedalMid", "Mid"));
        pedal->addItem (makeControl ("pedalTreble", "Treble"));
        pedal->addItem (makeControl ("pedalGain", "Gain"));
        pedal->addItem (makeControl ("pedalOutput", "Volume"));
        pedal->addRow (makeNamBox ("PEDAL MODEL", true), 140);
        pedal->setFooter (makeFootswitch ("pedalBypass", pedalAccent, pedalBody), 92);
    }

    if (auto* preEq = makePanel (0, "PRE-EQ", UnitStyle::Pedal, 3, 238,
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

        auto volume = std::make_unique<FooterWithKnob> (
            *makeFootswitch ("preEqBypass", eqAccent, eqBody),
            *makeControl ("preEqVolume", "Volume"));
        auto* rawVolume = volume.get();
        ownedControls_.push_back (std::move (volume));
        preEq->setFooter (rawVolume, 82, FooterWithKnob::preferredWidth);
    }

    // 2. Amp ----------------------------------------------------------------
    // The body colour is the gold faceplate the knobs sit on, so lettering
    // turns dark automatically over the brushed gold.
    if (auto* amp = makePanel (1, "AMP", UnitStyle::AmpHead, 8, 760,
                               faceplateGold, ampAccent))
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

        const auto ampPicker = makePicker (
            "Choose a NAM amp model",
            juce::StringArray { ".nam" },
            [this]
            {
                auto* loader = processor_.getNamModel();
                const auto current = loader != nullptr ? loader->getLoadedFile()
                                                       : juce::File();
                return folderOf (current, processor_.getNamDirectory());
            },
            [this] (const juce::File& directory)
            {
                processor_.setNamDirectory (directory);
            },
            [this] (const juce::File& file)
            {
                if (processor_.loadNamModel (file))
                    return true;

                showLoadFailure ("model", file);
                return false;
            });

        auto browse = std::make_unique<AmpBrowseButton> (
            [ampPicker] { ampPicker.openLoad(); },
            [this] (const juce::File& file)
            {
                if (! processor_.loadNamModel (file))
                    showLoadFailure ("model", file);
            });
        auto* rawBrowse = browse.get();
        ownedControls_.push_back (std::move (browse));

        // The load button and arrows live in the header row next to the amp
        // screen (see AmpHeaderRow below), not in the knob grid.

        amp->addItem (addAmpControl ("ampBypass", "Power", ParamControl::Kind::Toggle, true));
        amp->addItem (addAmpControl ("ampGain", "Gain"));
        amp->addItem (addAmpControl ("ampBass", "Bass"));
        amp->addItem (addAmpControl ("ampMid", "Mid"));
        amp->addItem (addAmpControl ("ampTreble", "Treble"));
        amp->addItem (addAmpControl ("ampPresence", "Presence"));
        amp->addItem (addAmpControl ("ampDepth", "Depth"));
        amp->addItem (addAmpControl ("ampOutput", "Output"));

        // The grille screen is the amp's file picker: tapping it lists the
        // folder next to the loaded model.
        auto display = std::make_unique<FileScreen> ("", "No model loaded");
        display->setName ("AmpScreen");
        auto* rawDisplay = display.get();
        rawDisplay->setOnOpenPopup (ampPicker.openPopup);
        ownedControls_.push_back (std::move (display));

        if (auto* loader = processor_.getNamModel())
            rawDisplay->setFileName (loader->getLoadedFileName());

        nameUpdaters_.push_back ([this, rawDisplay]
        {
            if (auto* loader = processor_.getNamModel())
                rawDisplay->setFileName (loader->getLoadedFileName());
        });

        auto header = std::make_unique<AmpHeaderRow> (*rawDisplay, *rawBrowse,
                                                     [stepAmp] { stepAmp (-1); },
                                                     [stepAmp] { stepAmp (1); });
        auto* rawHeader = header.get();
        ownedControls_.push_back (std::move (header));
        amp->addRow (rawHeader, 136);
    }

    // 3. Post EQ rack --------------------------------------------------------
    if (auto* postEq = makePanel (2, "POST AMP EQ", UnitStyle::RackUnit, 1, 0,
                                  rackBody, rackAccent))
    {
        auto eq = std::make_unique<PostEqEditor> (
            state, processor_.getPostEqSpectrum(),
            [this] { return processor_.getSampleRate(); }, reduced);
        auto* rawEq = eq.get();
        ownedControls_.push_back (std::move (eq));

        postEq->addFlexibleRow (rawEq, 280);
        postEq->setStretchesToWidth (true);
        postEq->setPreferredHeight (0);
    }

    // 4. IR rack -------------------------------------------------------------
    if (auto* ir = makePanel (3, "IR CAB", UnitStyle::RackUnit, 1, 600,
                              rackBody, rackAccent))
    {
        auto* bypass = makeControl ("irBypass", "Bypass", ParamControl::Kind::Toggle, true);
        bypass->setLabelFontSize (12.5f);
        bypass->setDarkText (true);
        ir->addRow (bypass, 64);

        for (int slot = 0; slot < IrLoader::kNumSlots; ++slot)
        {
            auto row = std::make_unique<IrPickRow> (
                "IR " + juce::String (slot == 0 ? "A" : "B"),
                "IrScreen" + juce::String (slot),
                [this, slot] (const juce::File& file)
                {
                    return processor_.loadIr (slot, file);
                },
                [this] { return processor_.getIrDirectory(); },
                [this, slot]
                {
                    auto* loader = processor_.getIrLoader();
                    return loader != nullptr ? loader->getLoadedFile (slot)
                                             : juce::File();
                });
            auto* rawRow = row.get();

            const auto picker = makePicker (
                "Choose an IR",
                juce::StringArray { ".wav", ".aif", ".aiff" },
                [this, slot]
                {
                    auto* irLoader = processor_.getIrLoader();
                    const auto current = irLoader != nullptr
                        ? irLoader->getLoadedFile (slot)
                        : juce::File();

                    return folderOf (current, processor_.getIrDirectory());
                },
                [this] (const juce::File& directory)
                {
                    processor_.setIrDirectory (directory);
                },
                [rawRow] (const juce::File& file) { return rawRow->loadFile (file); });

            rawRow->setOnOpenPopup (picker.openPopup);
            rawRow->setOnLoad (picker.openLoad);

            ownedControls_.push_back (std::move (row));
            ir->addRow (rawRow, 84);
        }

        // The only knob on this rack: a crossfade between IR A and IR B.
        auto* blendControl = makeControl ("irBlend", "Blend  A - B");
        blendControl->setDarkText (true);
        auto blend = std::make_unique<CenteredRow> (blendControl);
        auto* rawBlend = blend.get();
        ownedControls_.push_back (std::move (blend));
        ir->addRow (rawBlend, 110);

        ir->setPreferredHeight (400);
    }

    // 5. Post FX pedals ------------------------------------------------------
    if (auto* chorus = makePanel (4, "CHORUS / VIBRATO", UnitStyle::Pedal, 2, 0,
                                  chorusBody, chorusAccent))
    {
        chorus->addItem (makeControl ("chorusRate", "Rate"));
        chorus->addItem (makeControl ("chorusDepth", "Depth"));
        chorus->addItem (makeControl ("chorusFeedback", "Feedback"));
        chorus->addItem (makeControl ("chorusDelay", "Delay"));
        chorus->addItem (makeControl ("chorusMix", "Mix"));
        chorus->addItem (makeControl ("chorusStereo", "Stereo", ParamControl::Kind::Toggle));
        chorus->setFooter (makeFootswitch ("chorusBypass", chorusAccent, chorusBody), 92);
    }

    if (auto* flanger = makePanel (4, "FLANGER", UnitStyle::Pedal, 2, 0,
                                   flangerBody, flangerAccent))
    {
        flanger->addItem (makeControl ("flangerRate", "Rate"));
        flanger->addItem (makeControl ("flangerDepth", "Depth"));
        flanger->addItem (makeControl ("flangerManual", "Manual"));
        flanger->addItem (makeControl ("flangerFeedback", "Feedback"));
        flanger->addItem (makeControl ("flangerMix", "Mix"));
        flanger->setFooter (makeFootswitch ("flangerBypass", flangerAccent, flangerBody), 92);
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
        phaser->setFooter (makeFootswitch ("phaserBypass", phaserAccent, phaserBody), 92);
    }

    if (auto* delay = makePanel (4, "DELAY", UnitStyle::Pedal, 2, 0,
                                 delayBody, delayAccent))
    {
        delay->addItem (makeControl ("delayMix", "Mix"));
        delay->addItem (makeControl ("delayTimeMs", "Time"));
        delay->addItem (makeControl ("delayFeedback", "Feedback"));
        delay->addItem (makeControl ("delayPingPong", "Ping-Pong", ParamControl::Kind::Toggle));
        delay->addItem (makeControl ("delayTone", "Tone"));
        delay->addItem (makeControl ("delayFuzz", "Fuzz"));
        delay->setFooter (makeFootswitch ("delayBypass", delayAccent, delayBody), 92);
    }

    if (auto* reverb = makePanel (4, "REVERB", UnitStyle::Pedal, 2, 0,
                                  reverbBody, reverbAccent))
    {
        reverb->addItem (makeControl ("reverbMix", "Mix"));
        reverb->addItem (makeControl ("reverbDecay", "Decay"));
        reverb->addItem (makeControl ("reverbWidth", "Width"));
        reverb->setFooter (makeFootswitch ("reverbBypass", reverbAccent, reverbBody), 92);
    }

    // Convolution reverb: two full stereo IRs in parallel, each with its own
    // Mix and tail Cutoff. The rows use the same file picker as the cabinet
    // loader, with a longer filename screen. Master mirrors the global level.
    if (auto* reverbIr = makePanel (4, "REVERB IR", UnitStyle::Pedal, 3, 330,
                                    reverbIrBody, reverbIrAccent))
    {
        reverbIr->setRowsFirst (true);
        reverbIr->setItemCellHeight (72);

        for (int slot = 0; slot < ReverbIrLoader::kNumSlots; ++slot)
        {
            const juce::String side = slot == 0 ? "L" : "R";

            auto row = std::make_unique<IrPickRow> (
                "REVERB IR " + side,
                "ReverbIrScreen" + juce::String (slot),
                [this, slot] (const juce::File& file)
                {
                    return processor_.loadReverbIr (slot, file);
                },
                [this, slot] { return processor_.getReverbIrDirectory (slot); },
                [this, slot]
                {
                    auto* loader = processor_.getReverbIrLoader();
                    return loader != nullptr ? loader->getLoadedFile (slot)
                                             : juce::File();
                });
            auto* rawRow = row.get();

            const auto picker = makePicker (
                "Choose a reverb IR (" + side + ")",
                juce::StringArray { ".wav", ".aif", ".aiff" },
                [this, slot]
                {
                    auto* loader = processor_.getReverbIrLoader();
                    const auto current = loader != nullptr
                        ? loader->getLoadedFile (slot)
                        : juce::File();

                    return folderOf (current,
                                     processor_.getReverbIrDirectory (slot));
                },
                [this, slot] (const juce::File& directory)
                {
                    processor_.setReverbIrDirectory (slot, directory);
                },
                [rawRow] (const juce::File& file) { return rawRow->loadFile (file); });

            rawRow->setOnOpenPopup (picker.openPopup);
            rawRow->setOnLoad (picker.openLoad);

            ownedControls_.push_back (std::move (row));
            reverbIr->addRow (rawRow, 66);
        }

        auto addReverbIrControl = [this, reverbIr, &makeControl] (const char* id,
                                                    const char* label,
                                                    bool accent = false)
        {
            auto* control = makeControl (id, label);

            if (accent)
            {
                control->setAccentKnob();
                control->setLabelFontSize (12.0f);
            }

            reverbIr->addItem (control);
        };

        addReverbIrControl ("reverbIrLeftMix", "Mix  L", true);
        addReverbIrControl ("reverbIrLeftCutoff", "Cutoff L");
        addReverbIrControl ("reverbIrLevel", "Master");
        addReverbIrControl ("reverbIrRightMix", "Mix  R", true);
        addReverbIrControl ("reverbIrRightCutoff", "Cutoff R");
        reverbIr->setFooter (makeFootswitch ("reverbIrBypass", reverbIrAccent, reverbIrBody), 92);
    }

    layoutContent();
}

//==============================================================================
void NAMBerryEditor::layoutContent()
{
    constexpr int gap = 8;
    constexpr int margin = 6;

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

            // The amp head and the graphical EQ stretch to the full window width.
            if (section->isAmpHead() || section->stretchesToWidth())
                width = juce::jmax (width, viewportWidth - 2 * margin);

            wantedHeight = section->preferredHeight();
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

void NAMBerryEditor::openFilePopup (juce::Component& anchor,
                                    const juce::File& directory,
                                    juce::StringArray extensions,
                                    std::function<void (const juce::File&)> onFileChosen,
                                    std::function<void (const juce::File&)> onDirectoryChanged)
{
    if (filePopup_ == nullptr)
    {
        filePopup_ = std::make_unique<FilePopup>();
        addAndMakeVisible (*filePopup_);
    }

    filePopup_->setBounds (getLocalBounds());

    if (filePopup_->isOpen())
        filePopup_->close();

    filePopup_->open (getLocalArea (&anchor, anchor.getLocalBounds()),
                      directory,
                      std::move (extensions),
                      std::move (onFileChosen),
                      std::move (onDirectoryChanged));

    filePopup_->toFront (true);
}

void NAMBerryEditor::openFileChooser (const juce::String& title,
                                      const juce::File& startDirectory,
                                      const juce::String& wildcard,
                                      std::function<void (const juce::File&)> onFileChosen)
{
    fileChooser_ = std::make_unique<juce::FileChooser> (title, startDirectory, wildcard);

    fileChooser_->launchAsync (juce::FileBrowserComponent::openMode
                                   | juce::FileBrowserComponent::canSelectFiles,
                               [onFileChosen] (const juce::FileChooser& chooser)
                               {
                                   const auto file = chooser.getResult();

                                   if (file.existsAsFile() && onFileChosen != nullptr)
                                       onFileChosen (file);
                               });
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

    auto tunerArea = header.withTrimmedLeft (200).reduced (0, 3);
    tunerArea = tunerArea.withSizeKeepingCentre (
        juce::jmin (640, tunerArea.getWidth()), 54);

    if (headerTuner_ != nullptr)
        headerTuner_->setBounds (tunerArea);

    // Global controls share the top row with the section tabs: Input Gain at
    // the input (left edge), Room / Master at the output (right edge), and the
    // tab bar centred between them.
    constexpr int topRowHeight = 108;
    constexpr int tabHeight = 64;
    constexpr int knobHeight = ParamControl::cellHeight;
    constexpr int knobWidth = ParamControl::cellWidth;
    constexpr int sidePodWidth = knobWidth * 2 + 16;

    auto topRow = r.removeFromTop (topRowHeight);
    const int knobY = topRow.getY() + (topRow.getHeight() - knobHeight) / 2;

    if (inputGainKnob_ != nullptr)
        inputGainKnob_->setBounds (topRow.getX() + 10, knobY, knobWidth, knobHeight);

    if (bottomKnobs_ != nullptr)
        bottomKnobs_->setBounds (topRow.getRight() - 10 - sidePodWidth, knobY,
                                 sidePodWidth, knobHeight);

    // Measure the tab bar's natural width at a generous width first, then
    // shrink and centre the bar so the buttons never drift off-centre.
    tabBar_.setBounds (topRow.withSizeKeepingCentre (
        juce::jmin (900, topRow.getWidth() - 32), tabHeight));

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
    const int leftEdge = topRow.getX() + 10 + knobWidth + 12;
    const int rightEdge = topRow.getRight() - 10 - sidePodWidth - 12;
    const int available = juce::jmax (240, rightEdge - leftEdge);
    const int barWidth = juce::jlimit (240, available,
                                       naturalWidth > 0 ? naturalWidth : 660);
    const int barX = juce::jlimit (leftEdge, juce::jmax (leftEdge, rightEdge - barWidth),
                                   topRow.getX() + (topRow.getWidth() - barWidth) / 2);

    tabBar_.setBounds (barX, topRow.getY() + (topRowHeight - tabHeight) / 2,
                       barWidth, tabHeight);

    viewport_.setBounds (r.reduced (6, 4));
    layoutContent();

    if (filePopup_ != nullptr)
        filePopup_->setBounds (getLocalBounds());
}

void NAMBerryEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (kBg));

    auto header = getLocalBounds().removeFromTop (60);

    // Light chrome: the coloured wordmark stays legible on the pale surface.
    g.setGradientFill (juce::ColourGradient (
        juce::Colour (kPanel), 0.0f, 0.0f,
        juce::Colour (kBg), 0.0f, (float) header.getBottom(), false));
    g.fillRect (header);

    if (const auto* logo = headerLogoNameColored(); logo != nullptr)
    {
        logo->drawWithin (g, juce::Rectangle<float> (10.0f, 8.0f, 140.0f, 44.0f),
                          juce::RectanglePlacement::centred
                              | juce::RectanglePlacement::onlyReduceInSize,
                          1.0f);
    }

    g.setColour (juce::Colour (kAccent).withAlpha (0.55f));
    g.fillRect (header.removeFromBottom (1));

    // Bench deck: a pale studio surface the hardware sits on.
    const auto deck = viewport_.getBounds().toFloat().reduced (2.0f);
    g.setGradientFill (juce::ColourGradient (
        juce::Colour (0xffdde1ee), deck.getX(), deck.getY(),
        juce::Colour (0xffccd1e3), deck.getX(), deck.getBottom(), false));
    g.fillRoundedRectangle (deck, 12.0f);
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.drawRoundedRectangle (deck, 12.0f, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.drawRoundedRectangle (deck.reduced (1.5f), 10.0f, 1.0f);
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

void NAMBerryEditor::applyPerformanceMode (bool enabled)
{
    if (performanceMode_ == enabled)
        return;

    performanceMode_ = enabled;

    for (auto* panel : allPanels_)
        if (auto* section = dynamic_cast<SectionPanel*> (panel))
            section->setPerformanceMode (enabled);

    repaint();
}

void NAMBerryEditor::parentHierarchyChanged()
{
    applyWindowIcon();
}

void NAMBerryEditor::applyWindowIcon()
{
    if (windowIconSet_ || windowIconAttempts_ > 60)
        return;

    ++windowIconAttempts_;

    auto* window = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent());

    if (window == nullptr || window->getPeer() == nullptr)
        return;

    window->setIcon (appIconImage());
    windowIconSet_ = true;
}

void NAMBerryEditor::timerCallback()
{
    applyWindowIcon();

    if (performanceParam_ != nullptr)
        applyPerformanceMode (performanceParam_->load() > 0.5f);

    if (tunerUpdater_)
        tunerUpdater_();

    for (auto& updater : nameUpdaters_)
        updater();

    updateTabVisuals();
}

} // namespace namberry
