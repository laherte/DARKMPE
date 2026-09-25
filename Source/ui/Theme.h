#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace theme
{
inline juce::Colour bg()          { return juce::Colour (0xff0a0a0c); }
inline juce::Colour panel()       { return juce::Colour (0xff131316); }
inline juce::Colour grid()        { return juce::Colour (0xff24242a); }
inline juce::Colour chrome()      { return juce::Colour (0xffd4d6dc); }
inline juce::Colour textDim()     { return juce::Colour (0xff7a7c85); }
inline juce::Colour accent()      { return juce::Colour (0xffe0162b); }
inline juce::Colour slideCol()    { return juce::Colour (0xff4fc3d9); }
inline juce::Colour pressureCol() { return juce::Colour (0xffe8a33d); }
inline juce::Colour bendCol()     { return juce::Colour (0xffb28dff); }

// Bend axis of the expression views: semitones -> -1..1. The middle 60% is linear over +-100 cents (detune and
// vibrato are visible), the rest is logarithmic up to +-24 semitones (glides, falls, riffs).
inline float bendAxis (float semitones)
{
    const float cents = std::abs (semitones) * 100.0f;
    const float y = cents <= 100.0f ? 0.6f * cents / 100.0f : 0.6f + 0.4f * std::log (cents / 100.0f) / std::log (24.0f);
    return std::copysign (std::min (1.0f, y), semitones);
}

// Draws the bend-axis grid (0, +-50 cents, +-1, +-12 semitones) with labels into a lane.
inline void drawBendGrid (juce::Graphics& g, juce::Rectangle<float> lane)
{
    const float mid = lane.getCentreY(), half = lane.getHeight() * 0.5f - 1.0f;
    g.setFont (juce::FontOptions (8.0f));
    for (auto [semis, label] : { std::pair { 0.5f, "50c" }, { 1.0f, "1" }, { 12.0f, "12" } })
        for (float sign : { 1.0f, -1.0f })
        {
            const float y = mid - sign * bendAxis (semis) * half;
            g.setColour (grid().withAlpha (semis == 1.0f ? 1.0f : 0.6f));
            g.drawHorizontalLine ((int) y, lane.getX(), lane.getRight());
            g.setColour (textDim().withAlpha (0.8f));
            g.drawText ((sign > 0 ? "+" : "-") + juce::String (label), juce::Rectangle<float> (lane.getRight() - 30.0f, y - 6.0f, 28.0f, 12.0f),
                        juce::Justification::centredRight);
        }
    g.setColour (grid().brighter (0.5f));
    g.drawHorizontalLine ((int) mid, lane.getX(), lane.getRight());
}

// KIT layers: Lead, Bass, Arp, Siren, Stab, Pad
inline juce::Colour layerColour (int layer)
{
    static const juce::uint32 cols[] = { 0xffe0162b, 0xff9b6bff, 0xff4fc3d9, 0xffe8a33d, 0xffd4d6dc, 0xffc2185b };
    return juce::Colour (cols[(size_t) juce::jlimit (0, 5, layer)]);
}

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, bg());
        setColour (juce::Label::textColourId, chrome());
        setColour (juce::Slider::textBoxTextColourId, chrome());
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::ComboBox::backgroundColourId, panel());
        setColour (juce::ComboBox::outlineColourId, grid().brighter (0.2f));
        setColour (juce::ComboBox::textColourId, chrome());
        setColour (juce::ComboBox::arrowColourId, accent());
        setColour (juce::PopupMenu::backgroundColourId, panel());
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent());
        setColour (juce::PopupMenu::textColourId, chrome());
        setColour (juce::TextButton::buttonColourId, panel());
        setColour (juce::TextButton::buttonOnColourId, accent());
        setColour (juce::TextButton::textColourOffId, chrome());
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::ToggleButton::textColourId, chrome());
        setColour (juce::ToggleButton::tickColourId, accent());
        setColour (juce::ToggleButton::tickDisabledColourId, textDim());
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override
    {
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
        const float r = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);

        juce::Path track;
        track.addCentredArc (c.x, c.y, r, r, 0.0f, startAngle, endAngle, true);
        g.setColour (grid().brighter (0.2f));
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path value;
        value.addCentredArc (c.x, c.y, r, r, 0.0f, startAngle, angle, true);
        g.setColour (accent());
        g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (panel().brighter (0.08f));
        g.fillEllipse (juce::Rectangle<float> (r * 1.3f, r * 1.3f).withCentre (c));
        g.setColour (chrome());
        const juce::Point<float> tip (c.x + std::sin (angle) * r * 0.62f, c.y - std::cos (angle) * r * 0.62f);
        g.drawLine ({ c, tip }, 2.0f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto col = b.getToggleState() ? accent() : base;
        if (down) col = col.brighter (0.2f);
        else if (over) col = col.brighter (0.08f);
        g.setColour (col);
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (b.getToggleState() ? accent().brighter (0.3f) : grid().brighter (0.3f));
        g.drawRoundedRectangle (r, 2.0f, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override
    {
        return juce::Font (juce::FontOptions ((float) std::min (13, h - 8), juce::Font::bold)).withExtraKerningFactor (0.08f);
    }
};
} // namespace theme
