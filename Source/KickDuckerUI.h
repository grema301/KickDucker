#pragma once
#include <JuceHeader.h>

//==============================================================================
// Theme: one palette, used by the LookAndFeel, the EQ display and the editor.
namespace KDTheme
{
    // Surfaces
    const juce::Colour bgTop         { 0xff10162a };
    const juce::Colour bgBottom      { 0xff080b14 };
    const juce::Colour headerBg      { 0xff0a0e19 };
    const juce::Colour cardTop       { 0xff171e31 };
    const juce::Colour cardBottom    { 0xff121828 };
    const juce::Colour displayTop    { 0xff0d1220 };
    const juce::Colour displayBottom { 0xff090d16 };
    const juce::Colour border        { 0xff222b42 };

    // Signal colours
    const juce::Colour accent        { 0xff35d8ff };   // live / active
    const juce::Colour accentDim     { 0xff1b8fb0 };
    const juce::Colour amber         { 0xffffb83d };   // locked
    const juce::Colour danger        { 0xffff5a72 };   // bypass / heavy reduction

    // Text
    const juce::Colour textPrimary   { 0xffe9edf7 };
    const juce::Colour textSecondary { 0xff98a2bd };
    const juce::Colour textDim       { 0xff5a6482 };

    // Controls
    const juce::Colour knobTrack     { 0xff242c42 };
    const juce::Colour knobTop       { 0xff2a3350 };
    const juce::Colour knobBottom    { 0xff161c2e };
    const juce::Colour chipBg        { 0xff141b2c };
    const juce::Colour chipBorder    { 0xff2c3652 };

    //--------------------------------------------------------------------------
    // Fonts. juce::Font's old constructors are deprecated in JUCE 8 (FontOptions),
    // so these two helpers compile cleanly on both JUCE 7 and JUCE 8.
    inline juce::Font makeFont (float height, bool bold = false, float kerning = 0.0f)
    {
       #if JUCE_MAJOR_VERSION >= 8
        juce::Font f (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
       #else
        juce::Font f (height, bold ? juce::Font::bold : juce::Font::plain);
       #endif
        if (kerning != 0.0f) f.setExtraKerningFactor (kerning);
        return f;
    }

    inline juce::Font makeMonoFont (float height, bool bold = false)
    {
       #if JUCE_MAJOR_VERSION >= 8
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height,
                                              bold ? juce::Font::bold : juce::Font::plain));
       #else
        return juce::Font (juce::Font::getDefaultMonospacedFontName(), height,
                           bold ? juce::Font::bold : juce::Font::plain);
       #endif
    }

    //--------------------------------------------------------------------------
    // Text helpers. charToString() avoids putting non-ASCII characters in the
    // source file (which MSVC can mangle without /utf-8).
    inline juce::String minusSign() { return juce::String::charToString ((juce::juce_wchar) 0x2212); }
    inline juce::String timesSign() { return juce::String::charToString ((juce::juce_wchar) 0x00D7); }

    inline juce::String formatNumber (double v, int decimals, bool alwaysSign = false)
    {
        const double scale = std::pow (10.0, decimals);
        const double r     = std::round (v * scale) / scale;
        const juce::String s (std::abs (r), decimals);
        if (r < 0.0)                  return minusSign() + s;
        if (r > 0.0 && alwaysSign)    return "+" + s;
        return s;
    }

    inline juce::String formatDB (double v, int decimals = 1, bool alwaysSign = false)
    {
        return formatNumber (v, decimals, alwaysSign) + " dB";
    }

    /** Parses what formatNumber() produced (typographic minus included) or plain typing. */
    inline double parseNumber (const juce::String& text)
    {
        return text.replace (minusSign(), "-").getDoubleValue();
    }
}

//==============================================================================
/**
 * Look and feel for every control in the plugin.
 *
 * Rotary sliders read optional properties:
 *    "bipolar" (bool) - value arc grows from the 12 o'clock position (e.g. Out Gain)
 *    "invert"  (bool) - value arc grows from the max end back to the value (e.g. Depth)
 */
class KickDuckerLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KickDuckerLookAndFeel()
    {
        using namespace KDTheme;

        setColour (juce::Slider::rotarySliderFillColourId,    accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, knobTrack);
        setColour (juce::Slider::textBoxTextColourId,         textPrimary);
        setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId,    accent.withAlpha (0.35f));

        setColour (juce::Label::textColourId,                 textSecondary);

        // Inline value editing (double-click a knob's value)
        setColour (juce::TextEditor::backgroundColourId,      chipBg);
        setColour (juce::TextEditor::textColourId,            textPrimary);
        setColour (juce::TextEditor::highlightColourId,       accent.withAlpha (0.35f));
        setColour (juce::TextEditor::highlightedTextColourId, textPrimary);
        setColour (juce::TextEditor::outlineColourId,         chipBorder);
        setColour (juce::TextEditor::focusedOutlineColourId,  accent);
        setColour (juce::CaretComponent::caretColourId,       accent);

        setColour (juce::TooltipWindow::backgroundColourId,   headerBg);
        setColour (juce::TooltipWindow::textColourId,         textPrimary);
        setColour (juce::TooltipWindow::outlineColourId,      chipBorder);

        setColour (juce::TextButton::buttonColourId,          chipBg);
        setColour (juce::TextButton::buttonOnColourId,        accent);
        setColour (juce::TextButton::textColourOffId,         textSecondary);
        setColour (juce::TextButton::textColourOnId,          textPrimary);
    }

    //--------------------------------------------------------------------------
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider& slider) override
    {
        using namespace KDTheme;

        const auto area     = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (4.0f);
        const float diameter = juce::jmin (area.getWidth(), area.getHeight());
        const auto  centre   = area.getCentre();
        const float radius   = diameter * 0.5f;

        const float arcThickness = juce::jmax (3.0f, diameter * 0.075f);
        const float arcRadius    = radius - arcThickness * 0.5f;
        const float angle        = startAngle + sliderPos * (endAngle - startAngle);
        const bool  hover        = slider.isMouseOverOrDragging();
        const bool  enabled      = slider.isEnabled();

        const bool bipolar = (bool) slider.getProperties().getWithDefault ("bipolar", false);
        const bool invert  = (bool) slider.getProperties().getWithDefault ("invert",  false);

        float from = startAngle, to = angle;
        if (bipolar) from = startAngle + 0.5f * (endAngle - startAngle);
        if (invert)  { from = angle; to = endAngle; }
        if (to < from) std::swap (from, to);

        const auto fillColour = slider.findColour (juce::Slider::rotarySliderFillColourId)
                                      .withMultipliedAlpha (enabled ? 1.0f : 0.4f);

        // Track
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
        g.strokePath (track, juce::PathStrokeType (arcThickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        // Value arc (soft glow underneath, crisp arc on top)
        if (to - from > 0.002f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, from, to, true);

            g.setColour (fillColour.withAlpha (hover ? 0.30f : 0.18f));
            g.strokePath (value, juce::PathStrokeType (arcThickness + 5.0f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
            g.setColour (fillColour);
            g.strokePath (value, juce::PathStrokeType (arcThickness, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        // Knob body
        const float bodyR = radius - arcThickness - 5.0f;
        juce::ColourGradient body (hover ? knobTop.brighter (0.15f) : knobTop, centre.x, centre.y - bodyR,
                                   knobBottom, centre.x, centre.y + bodyR, false);
        g.setGradientFill (body);
        g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

        // Pointer
        juce::Path pointer;
        pointer.addRoundedRectangle (-1.5f, -bodyR + 4.0f, 3.0f, bodyR * 0.46f, 1.5f);
        g.setColour (hover ? juce::Colours::white : textPrimary.withAlpha (0.92f));
        g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
    }

    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (slider);
        l->setFont (KDTheme::makeMonoFont (11.0f));
        l->setJustificationType (juce::Justification::centred);

        // LookAndFeel_V4 gives the box an outline; we want bare text
        l->setColour (juce::Label::outlineColourId,    juce::Colours::transparentBlack);
        l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        l->setColour (juce::Label::textColourId,       KDTheme::textPrimary);
        return l;
    }

    //--------------------------------------------------------------------------
    // Pill-shaped toggle chips (SNAP / LOCK / BYPASS). The per-button "on" colour
    // is read from TextButton::buttonOnColourId so each chip can have its own.
    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                               bool isMouseOver, bool isButtonDown) override
    {
        using namespace KDTheme;

        const auto r      = button.getLocalBounds().toFloat().reduced (0.5f);
        const bool on     = button.getToggleState();
        const auto accentC = button.findColour (juce::TextButton::buttonOnColourId);
        const float rad   = r.getHeight() * 0.5f;

        juce::Colour fill = on ? accentC.withAlpha (0.16f) : button.findColour (juce::TextButton::buttonColourId);
        if (isMouseOver) fill = fill.brighter (0.12f);
        if (isButtonDown) fill = fill.darker (0.15f);

        g.setColour (fill);
        g.fillRoundedRectangle (r, rad);

        g.setColour (on ? accentC.withAlpha (0.85f) : chipBorder.brighter (isMouseOver ? 0.25f : 0.0f));
        g.drawRoundedRectangle (r, rad, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool) override
    {
        const bool on      = button.getToggleState();
        const auto accentC = button.findColour (juce::TextButton::buttonOnColourId);
        auto r = button.getLocalBounds().toFloat();

        if (button.getClickingTogglesState())
        {
            // Small LED on the left
            const float d = 6.0f;
            const float cy = r.getCentreY();
            if (on)
            {
                g.setColour (accentC.withAlpha (0.25f));
                g.fillEllipse (10.0f - 3.0f, cy - d * 0.5f - 3.0f, d + 6.0f, d + 6.0f);
            }
            g.setColour (on ? accentC : KDTheme::textDim);
            g.fillEllipse (10.0f, cy - d * 0.5f, d, d);
            r.removeFromLeft (d + 12.0f);
        }

        g.setFont (getTextButtonFont (button, button.getHeight()));
        g.setColour (button.findColour (on ? juce::TextButton::textColourOnId
                                           : juce::TextButton::textColourOffId));
        g.drawText (button.getButtonText(), r, juce::Justification::centred, false);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return KDTheme::makeFont (juce::jmin (11.0f, (float) buttonHeight * 0.5f), true, 0.1f);
    }
};

//==============================================================================
/**
 * Compact sidechain status: activity dot, caption, level bar and dB readout.
 * Tells the user at a glance whether the sidechain is routed and receiving signal.
 */
class SidechainMeter : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    SidechainMeter()
    {
        setTooltip ("Sidechain input level. \"no signal\" means nothing is routed to the "
                    "sidechain input (or it is silent), so the notch has nothing to follow.");
    }

    /** Called from the editor's timer (30 Hz). */
    void setState (float levelDB, bool active)
    {
        const float target = active ? levelDB : -80.0f;
        // Fast attack, slow fall (3 dB per tick)
        const float next = target > shownDB ? target : juce::jmax (target, shownDB - 3.0f);

        if (std::abs (next - shownDB) > 0.05f || active != isActive)
        {
            shownDB  = next;
            isActive = active;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        using namespace KDTheme;

        const float cy = (float) getHeight() * 0.5f;

        // Activity dot
        const auto dotCol = isActive ? accent : textDim;
        if (isActive)
        {
            g.setColour (dotCol.withAlpha (0.22f));
            g.fillEllipse (0.0f, cy - 7.0f, 14.0f, 14.0f);
        }
        g.setColour (dotCol);
        g.fillEllipse (3.0f, cy - 4.0f, 8.0f, 8.0f);

        // Caption
        g.setFont (makeFont (9.5f, true, 0.12f));
        g.setColour (textDim);
        g.drawText ("SIDECHAIN", 20, (int) cy - 7, 66, 14, juce::Justification::centredLeft);

        // Level bar
        const juce::Rectangle<float> track (92.0f, cy - 3.0f, 130.0f, 6.0f);
        g.setColour (juce::Colour (0xff070a12));
        g.fillRoundedRectangle (track, 3.0f);
        g.setColour (border);
        g.drawRoundedRectangle (track.reduced (0.5f), 3.0f, 1.0f);

        const float norm = juce::jlimit (0.0f, 1.0f, juce::jmap (shownDB, -60.0f, 0.0f, 0.0f, 1.0f));
        if (norm > 0.01f)
        {
            juce::ColourGradient grad (accentDim, track.getX(), 0.0f, amber, track.getRight(), 0.0f, false);
            grad.addColour (0.72, accent);
            g.setGradientFill (grad);

            auto inner = track.reduced (1.0f);
            g.fillRoundedRectangle (inner.withWidth (inner.getWidth() * norm), 2.0f);
        }

        // Readout
        g.setFont (makeMonoFont (10.0f));
        g.setColour (isActive ? textSecondary : textDim);
        g.drawText (isActive ? formatNumber (shownDB, 0) + " dB" : juce::String ("no signal"),
                    230, (int) cy - 7, 70, 14, juce::Justification::centredLeft);
    }

private:
    float shownDB  = -80.0f;
    bool  isActive = false;
};