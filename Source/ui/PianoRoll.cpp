#include "PianoRoll.h"
#include "Theme.h"

#include <cmath>

PianoRoll::PianoRoll (DarkMPEProcessor& p) : proc (p)
{
    setOpaque (true);
    refresh();
    startTimerHz (30);
}

PianoRoll::~PianoRoll() { stopTimer(); }

void PianoRoll::refresh()
{
    shown = proc.getRendered();
    repaint();
}

void PianoRoll::timerCallback()
{
    const double ph = proc.isPlayingBack() ? proc.getPlayheadBeats() : -1.0;
    if (std::abs (ph - lastPlayhead) > 1.0e-4)
    {
        lastPlayhead = ph;
        repaint();
    }
}

void PianoRoll::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (bg());

    auto area = getLocalBounds().toFloat().reduced (1.0f);
    const float laneH = 44.0f;
    auto pressureLane = area.removeFromBottom (laneH);
    area.removeFromBottom (4.0f);
    auto slideLane = area.removeFromBottom (laneH);
    area.removeFromBottom (6.0f);
    auto roll = area;

    g.setColour (panel());
    g.fillRect (roll);
    g.fillRect (slideLane);
    g.fillRect (pressureLane);

    if (shown == nullptr)
        return;

    const auto& phrase = shown->focused().phrase;
    const double L = std::max (1.0, shown->lengthBeats);

    int lo = 127, hi = 0;
    for (const auto& n : phrase.notes)
    {
        lo = std::min (lo, n.pitch);
        hi = std::max (hi, n.pitch);
        if (n.glideFrom >= 0)
        {
            lo = std::min (lo, n.glideFrom);
            hi = std::max (hi, n.glideFrom);
        }
        for (const auto& pt : n.bend)
        {
            lo = std::min (lo, n.pitch + (int) std::floor (pt.v));
            hi = std::max (hi, n.pitch + (int) std::ceil (pt.v));
        }
    }
    if (phrase.notes.empty()) { lo = 48; hi = 72; }
    lo -= 3;
    hi += 3;
    if (hi - lo < 24) { const int c = (hi + lo) / 2; lo = c - 12; hi = c + 12; }

    const float rowH = roll.getHeight() / (float) (hi - lo + 1);
    auto xOf = [&] (double beat, const juce::Rectangle<float>& r) { return r.getX() + (float) (beat / L) * r.getWidth(); };
    auto yOf = [&] (float pitch) { return roll.getBottom() - (pitch - (float) lo + 0.5f) * rowH; };

    // pitch rows: black keys darker, C highlighted
    for (int p = lo; p <= hi; ++p)
    {
        const int pc = ((p % 12) + 12) % 12;
        const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        const float y = yOf ((float) p) - rowH * 0.5f;
        if (black)
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRect (roll.getX(), y, roll.getWidth(), rowH);
        }
        if (pc == 0)
        {
            g.setColour (grid().withAlpha (0.9f));
            g.drawHorizontalLine ((int) (y + rowH), roll.getX(), roll.getRight());
            g.setColour (textDim());
            g.setFont (juce::FontOptions (9.0f));
            g.drawText ("C" + juce::String (p / 12 - 2), juce::Rectangle<float> (roll.getX() + 2, y, 30, rowH), juce::Justification::centredLeft);
        }
    }

    // beat grid
    for (int b = 0; b <= (int) L; ++b)
    {
        g.setColour (b % 4 == 0 ? grid().brighter (0.4f) : grid());
        for (auto* r : { &roll, &slideLane, &pressureLane })
            g.drawVerticalLine ((int) xOf (b, *r), r->getY(), r->getBottom());
    }

    // notes
    for (const auto& n : phrase.notes)
    {
        const float x0 = xOf (n.start, roll), x1 = xOf (n.end(), roll);
        const auto rect = juce::Rectangle<float> (x0, yOf ((float) n.pitch) - rowH * 0.5f, std::max (2.0f, x1 - x0), rowH).reduced (0.0f, 0.5f);
        auto c = accent().withMultipliedBrightness (0.55f + 0.45f * n.velocity);
        if (n.chromatic)
            c = chrome();
        g.setColour (c.withAlpha (0.85f));
        g.fillRoundedRectangle (rect, 1.5f);
        g.setColour (c.brighter (0.4f));
        g.drawRoundedRectangle (rect, 1.5f, 0.8f);

        // bend path
        if (n.bend.size() > 1)
        {
            juce::Path path;
            bool started = false;
            for (const auto& pt : n.bend)
            {
                const float x = xOf (n.start + pt.t, roll);
                const float y = yOf ((float) n.pitch + pt.v);
                if (! started) { path.startNewSubPath (x, y); started = true; }
                else path.lineTo (x, y);
            }
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.strokePath (path, juce::PathStrokeType (1.3f));
        }

        auto lane = [&] (const dmpe::Curve& curve, const juce::Rectangle<float>& r, juce::Colour col)
        {
            if (curve.size() < 2)
                return;
            juce::Path path;
            path.startNewSubPath (xOf (n.start, r), r.getBottom());
            for (const auto& pt : curve)
                path.lineTo (xOf (n.start + pt.t, r), r.getBottom() - pt.v * (r.getHeight() - 2.0f));
            path.lineTo (xOf (n.start + curve.back().t, r), r.getBottom());
            path.closeSubPath();
            g.setColour (col.withAlpha (0.18f));
            g.fillPath (path);
            g.setColour (col.withAlpha (0.8f));
            g.strokePath (path, juce::PathStrokeType (0.8f));
        };
        lane (n.slide, slideLane, slideCol());
        lane (n.pressure, pressureLane, pressureCol());
    }

    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.setColour (slideCol());
    g.drawText ("SLIDE / CC74", slideLane.reduced (4, 2), juce::Justification::topLeft);
    g.setColour (pressureCol());
    g.drawText ("PRESSURE", pressureLane.reduced (4, 2), juce::Justification::topLeft);

    // playhead
    if (lastPlayhead >= 0.0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        const float x = xOf (std::fmod (lastPlayhead, L), roll);
        g.drawLine (x, roll.getY(), x, pressureLane.getBottom(), 1.0f);
    }

    g.setColour (grid().brighter (0.3f));
    g.drawRect (roll);
}
