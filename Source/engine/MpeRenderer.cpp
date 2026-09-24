#include "MpeRenderer.h"
#include "ExpressionShaper.h"

#include <algorithm>
#include <cmath>

namespace dmpe
{

int bendToMidi (float semitones, int range)
{
    const double v = 8192.0 + (double) semitones / (double) range * 8192.0;
    return std::clamp ((int) std::lround (v), 0, 16383);
}

namespace
{
int to7 (float v) { return std::clamp ((int) std::lround (v * 127.0f), 0, 127); }
int velTo7 (float v) { return std::clamp ((int) std::lround (v * 127.0f), 1, 127); }
} // namespace

juce::MidiMessageSequence renderMpe (const Phrase& input, const RenderOptions& opts)
{
    juce::MidiMessageSequence seq;

    if (opts.includeZoneConfig)
    {
        const int members = opts.lastMemberChannel - opts.firstMemberChannel + 1;
        for (const auto meta : juce::MPEMessages::setLowerZone (members, opts.pitchBendRange, 2))
            seq.addEvent (meta.getMessage(), 0.0);
    }

    Phrase phrase = input;
    phrase.sortByStart();
    auto& notes = phrase.notes;

    // ---- channel allocation: least-recently-freed channel, steal (and cut) the oldest if all are busy.
    const int numCh = opts.lastMemberChannel - opts.firstMemberChannel + 1;
    std::vector<double> freeAt ((size_t) numCh, -1.0e9);
    std::vector<int> owner ((size_t) numCh, -1);
    std::vector<int> channelOf (notes.size(), 0);
    constexpr double eps = 1.0e-9;

    for (size_t i = 0; i < notes.size(); ++i)
    {
        const double s = notes[i].start;
        int best = -1;
        for (int c = 0; c < numCh; ++c)
            if (freeAt[(size_t) c] <= s + eps && (best < 0 || freeAt[(size_t) c] < freeAt[(size_t) best]))
                best = c;

        if (best < 0)
        {
            // All busy: steal the channel whose note ends first, and cut that note here.
            best = 0;
            for (int c = 1; c < numCh; ++c)
                if (freeAt[(size_t) c] < freeAt[(size_t) best])
                    best = c;
            auto& victim = notes[(size_t) owner[(size_t) best]];
            victim.length = std::max (0.0, s - victim.start);
        }

        channelOf[i] = opts.firstMemberChannel + best;
        freeAt[(size_t) best] = notes[i].end();
        owner[(size_t) best] = (int) i;
    }

    // ---- events
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i];
        const int ch = channelOf[i];
        const double s = n.start;
        const double len = n.length;

        if (len <= 0.0)
            continue;

        int lastBend = bendToMidi (evalCurve (n.bend, 0.0, 0.0f), opts.pitchBendRange);
        int lastSlide = to7 (evalCurve (n.slide, 0.0, 0.5f));
        int lastPress = to7 (evalCurve (n.pressure, 0.0, 0.0f));

        seq.addEvent (juce::MidiMessage::pitchWheel (ch, lastBend), s);
        seq.addEvent (juce::MidiMessage::controllerEvent (ch, 74, lastSlide), s);
        seq.addEvent (juce::MidiMessage::channelPressureChange (ch, lastPress), s);
        seq.addEvent (juce::MidiMessage::noteOn (ch, n.pitch, (juce::uint8) velTo7 (n.velocity)), s);

        for (double t = curveResolution; t < len - eps; t += curveResolution)
        {
            const int b = bendToMidi (evalCurve (n.bend, t, 0.0f), opts.pitchBendRange);
            const int sl = to7 (evalCurve (n.slide, t, 0.5f));
            const int pr = to7 (evalCurve (n.pressure, t, 0.0f));

            if (b != lastBend)   { seq.addEvent (juce::MidiMessage::pitchWheel (ch, b), s + t); lastBend = b; }
            if (sl != lastSlide) { seq.addEvent (juce::MidiMessage::controllerEvent (ch, 74, sl), s + t); lastSlide = sl; }
            if (pr != lastPress) { seq.addEvent (juce::MidiMessage::channelPressureChange (ch, pr), s + t); lastPress = pr; }
        }

        seq.addEvent (juce::MidiMessage::noteOff (ch, n.pitch, (juce::uint8) velTo7 (n.releaseVelocity)), s + len);
    }

    seq.updateMatchedPairs();
    return seq;
}

} // namespace dmpe
