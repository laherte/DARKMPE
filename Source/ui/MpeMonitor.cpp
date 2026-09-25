#include "MpeMonitor.h"
#include "Theme.h"

#include <cmath>

MpeMonitor::MpeMonitor (DarkMPEProcessor& p) : proc (p)
{
    setOpaque (true);
    startTimerHz (30);
}

MpeMonitor::~MpeMonitor() { stopTimer(); }

void MpeMonitor::timerCallback()
{
    std::array<int, 64> now {};
    size_t i = 0;
    now[i++] = proc.isPortOpen() ? 1 : 0;
    now[i++] = proc.isHostMono() ? 1 : 0;
    now[i++] = proc.getTranspose();
    for (int ch = 1; ch <= 16; ++ch)
    {
        const auto& m = proc.monitor (ch);
        const int note = m.note.load (std::memory_order_relaxed);
        if (ch == 1 && ! proc.isHostMono())
            continue;
        now[i++] = note;
        now[i++] = note < 0 ? 0 : (int) std::lround (m.bend.load (std::memory_order_relaxed) * 200.0f); // 0.5 cent
        now[i++] = note < 0 ? 0 : (int) std::lround (m.slide.load (std::memory_order_relaxed) * 48.0f)
                                  + 100 * (int) std::lround (m.pressure.load (std::memory_order_relaxed) * 48.0f);
    }
    if (now != snapshot)
    {
        snapshot = now;
        repaint();
    }
}

void MpeMonitor::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (bg());

    auto area = getLocalBounds().toFloat();
    auto label = area.removeFromLeft (92.0f);

    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.setColour (accent());
    const bool mono = proc.isHostMono();
    g.drawText (mono ? "MONO OUT" : "MPE OUT", label.removeFromTop (label.getHeight() * 0.5f).reduced (4, 0), juce::Justification::bottomLeft);
    g.setColour (proc.isPortOpen() ? slideCol() : textDim());
    g.setFont (juce::FontOptions (9.0f));
    g.drawText (proc.isPortOpen() ? proc.getPortName() : "port off", label.reduced (4, 0), juce::Justification::topLeft);
    if (const int t = proc.getTranspose(); t != 0)
    {
        g.setColour (chrome());
        g.drawText ("transpose " + juce::String (t > 0 ? "+" : "") + juce::String (t), label.reduced (4, 0).withTrimmedTop (12),
                    juce::Justification::topLeft);
    }

    const float w = area.getWidth() / 15.0f;
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    for (int ch = mono ? 1 : 2; ch <= (mono ? 1 : 16); ++ch)
    {
        // Mono: one wide cell for channel 1.
        auto cell = (mono ? juce::Rectangle<float> (area.getX(), area.getY(), w * 3.0f, area.getHeight())
                          : juce::Rectangle<float> (area.getX() + (ch - 2) * w, area.getY(), w, area.getHeight())).reduced (1.5f, 1.0f);
        const auto& m = proc.monitor (ch);
        const int note = m.note.load (std::memory_order_relaxed);
        const bool on = note >= 0;

        g.setColour (on ? panel().brighter (0.12f) : panel());
        g.fillRoundedRectangle (cell, 2.0f);

        auto text = cell.removeFromTop (13.0f);
        g.setColour (on ? chrome() : textDim().withAlpha (0.5f));
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText (on ? juce::String (names[note % 12]) + juce::String (note / 12 - 2) : "ch" + juce::String (ch),
                    text, juce::Justification::centred);

        if (! on)
            continue;

        // bend: two bars from the centre line, coarse (+-12 semitones, white) and fine (+-1 semitone, violet:
        // detune and vibrato move it), and the value (cents below a semitone)
        const float bend = m.bend.load (std::memory_order_relaxed);
        auto bendArea = cell.removeFromTop (cell.getHeight() * 0.55f).reduced (3.0f, 1.0f);
        const float mid = bendArea.getCentreY();
        const float half = bendArea.getHeight() * 0.5f;
        const float coarse = juce::jlimit (-1.0f, 1.0f, bend / 12.0f) * half;
        const float fine = juce::jlimit (-1.0f, 1.0f, bend) * half;
        g.setColour (grid());
        g.drawHorizontalLine ((int) mid, bendArea.getX(), bendArea.getRight());
        g.setColour (juce::Colours::white);
        g.fillRect (juce::Rectangle<float> (bendArea.getX() + bendArea.getWidth() * 0.12f, std::min (mid, mid - coarse),
                                            bendArea.getWidth() * 0.3f, std::max (1.0f, std::abs (coarse))));
        g.setColour (bendCol());
        g.fillRect (juce::Rectangle<float> (bendArea.getX() + bendArea.getWidth() * 0.5f, std::min (mid, mid - fine),
                                            bendArea.getWidth() * 0.18f, std::max (1.0f, std::abs (fine))));
        g.setFont (juce::FontOptions (8.0f));
        g.setColour (textDim());
        const auto value = std::abs (bend) < 1.0f ? juce::String (juce::roundToInt (bend * 100.0f)) + "c" : juce::String (bend, 1);
        g.drawText (value, bendArea, juce::Justification::bottomRight);

        // slide + pressure meters
        auto meters = cell.reduced (3.0f, 2.0f);
        auto slideBar = meters.removeFromLeft (meters.getWidth() * 0.5f).reduced (1.0f, 0.0f);
        auto pressBar = meters.reduced (1.0f, 0.0f);
        const float sl = m.slide.load (std::memory_order_relaxed);
        const float pr = m.pressure.load (std::memory_order_relaxed);
        g.setColour (slideCol());
        g.fillRect (slideBar.removeFromBottom (slideBar.getHeight() * sl));
        g.setColour (pressureCol());
        g.fillRect (pressBar.removeFromBottom (pressBar.getHeight() * pr));
    }
}
