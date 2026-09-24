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
    cacheValid = false;
    repaint();
}

float PianoRoll::playheadX (double beats) const
{
    const double L = shown != nullptr ? std::max (1.0, shown->lengthBeats) : 4.0;
    return rollArea.getX() + (float) (std::fmod (beats, L) / L) * rollArea.getWidth();
}

void PianoRoll::timerCallback()
{
    const double ph = proc.isPlayingBack() ? proc.getPlayheadBeats() : -1.0;
    if (std::abs (ph - lastPlayhead) < 1.0e-4)
        return;

    // Only the strips under the old and the new playhead need redrawing.
    const auto strip = [this] (double beats)
    {
        return juce::Rectangle<float> (playheadX (beats) - 2.0f, laneArea.getY(), 4.0f, laneArea.getHeight()).getSmallestIntegerContainer();
    };
    if (lastPlayhead >= 0.0)
        repaint (strip (lastPlayhead));
    lastPlayhead = ph;
    if (ph >= 0.0)
        repaint (strip (ph));
}

void PianoRoll::paint (juce::Graphics& g)
{
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int w = std::max (1, juce::roundToInt ((float) getWidth() * scale));
    const int h = std::max (1, juce::roundToInt ((float) getHeight() * scale));
    if (! cacheValid || cache.getWidth() != w || cache.getHeight() != h)
    {
        cache = juce::Image (juce::Image::RGB, w, h, false);
        juce::Graphics ig (cache);
        ig.addTransform (juce::AffineTransform::scale ((float) w / (float) std::max (1, getWidth()),
                                                       (float) h / (float) std::max (1, getHeight())));
        drawStatic (ig);
        cacheValid = true;
    }
    g.drawImage (cache, getLocalBounds().toFloat());

    if (lastPlayhead >= 0.0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        const float x = playheadX (lastPlayhead);
        g.drawLine (x, laneArea.getY(), x, laneArea.getBottom(), 1.0f);
    }
}

void PianoRoll::drawStatic (juce::Graphics& g)
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
    rollArea = roll;
    laneArea = roll.withBottom (pressureLane.getBottom());

    g.setColour (panel());
    g.fillRect (roll);
    g.fillRect (slideLane);
    g.fillRect (pressureLane);

    if (shown == nullptr)
        return;

    const auto& phrase = shown->focused().phrase;
    const double L = std::max (1.0, shown->lengthBeats);
    const bool layered = shown->streams.size() > 1; // KIT: every layer, the focused one on top

    int lo = 127, hi = 0;
    bool anyNote = false;
    for (const auto& stream : shown->streams)
    for (const auto& n : stream.phrase.notes)
    {
        anyNote = true;
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
    if (! anyNote) { lo = 48; hi = 72; }
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

    // notes with their pitch-bend path
    auto drawNotes = [&] (const dmpe::Phrase& ph, juce::Colour base, float alpha)
    {
        for (const auto& n : ph.notes)
        {
            const float x0 = xOf (n.start, roll), x1 = xOf (n.end(), roll);
            const auto rect = juce::Rectangle<float> (x0, yOf ((float) n.pitch) - rowH * 0.5f, std::max (2.0f, x1 - x0), rowH).reduced (0.0f, 0.5f);
            auto c = base.withMultipliedBrightness (0.55f + 0.45f * n.velocity);
            if (n.chromatic && ! layered)
                c = chrome();
            g.setColour (c.withAlpha (0.85f * alpha));
            g.fillRoundedRectangle (rect, 1.5f);
            g.setColour (c.brighter (0.4f).withAlpha (alpha));
            g.drawRoundedRectangle (rect, 1.5f, 0.8f);

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
                g.setColour ((layered ? base.brighter (0.5f) : juce::Colours::white).withAlpha (0.85f * alpha));
                g.strokePath (path, juce::PathStrokeType (1.3f));
            }
        }
    };

    if (layered)
        for (size_t i = 0; i < shown->streams.size(); ++i)
            if ((int) i != shown->focus)
                drawNotes (shown->streams[i].phrase, layerColour (shown->streams[i].layer), 0.4f);
    drawNotes (phrase, layered ? layerColour (shown->focused().layer) : accent(), 1.0f);

    // expression lanes of the focused stream
    for (const auto& n : phrase.notes)
    {
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

    // phrase form: A / B / C... at the start of each section
    for (const auto& [beat, label] : shown->sections)
    {
        const float x = xOf (beat, roll);
        g.setColour (chrome().withAlpha (0.35f));
        g.drawVerticalLine ((int) x, roll.getY(), roll.getBottom());
        const auto tag = juce::Rectangle<float> (x + 3.0f, roll.getY() + 3.0f, 30.0f, 15.0f);
        g.setColour (bg().withAlpha (0.85f));
        g.fillRoundedRectangle (tag, 3.0f);
        g.setColour (chrome());
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (label, tag, juce::Justification::centred);
    }

    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.setColour (slideCol());
    g.drawText ("SLIDE / CC74", slideLane.reduced (4, 2), juce::Justification::topLeft);
    g.setColour (pressureCol());
    g.drawText ("PRESSURE", pressureLane.reduced (4, 2), juce::Justification::topLeft);

    g.setColour (grid().brighter (0.3f));
    g.drawRect (roll);
}
