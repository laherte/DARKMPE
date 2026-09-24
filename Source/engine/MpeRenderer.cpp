#include "MpeRenderer.h"

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

// Curves are sampled on this grid; a message is sent only when the value has moved enough.
constexpr double renderGrid = 1.0 / 512.0;

// Pitch bend: a move of 12 cents is sent at once (fast glides get a message every grid step, ~1 ms), smaller
// drifts of 3+ cents at most every 1/32 beat, and a pitch that settles is always sent exactly (glides land in tune).
int centsToBend (double cents, int range) { return std::max (1, (int) std::lround (cents / 100.0 * 8192.0 / std::max (1, range))); }
constexpr double bendInterval = 1.0 / 32.0;

// 7-bit lanes (CC74, pressure): a jump of 8 steps is sent at once, smaller drifts at most every 1/16 beat.
constexpr int laneJump = 8;
constexpr double laneInterval = 1.0 / 16.0;

// Emits the bend / CC74 / pressure changes of one note after its start `s`.
template <typename Add>
void emitExpression (const Note& n, double s, double len, int range, int ch, Add&& add)
{
    CurveCursor bend (n.bend, 0.0f), slide (n.slide, 0.5f), press (n.pressure, 0.0f);
    int lastBend = bendToMidi (bend.at (0.0), range), lastSlide = to7 (slide.at (0.0)), lastPress = to7 (press.at (0.0));
    int prevBend = lastBend;
    double bendT = 0.0, slideT = 0.0, pressT = 0.0;
    const int bigStep = centsToBend (12.0, range), smallStep = centsToBend (3.0, range);
    constexpr double eps = 1.0e-9;

    for (int k = 1;; ++k)
    {
        const double t = k * renderGrid;
        if (t >= len - eps)
            break;

        const int b = bendToMidi (bend.at (t), range);
        const int moved = std::abs (b - lastBend);
        if (moved >= bigStep || (moved >= smallStep && t - bendT >= bendInterval - eps) || (moved > 0 && b == prevBend))
        {
            add (juce::MidiMessage::pitchWheel (ch, b), s + t);
            lastBend = b;
            bendT = t;
        }
        prevBend = b;

        const int sl = to7 (slide.at (t));
        if (std::abs (sl - lastSlide) >= laneJump || (sl != lastSlide && t - slideT >= laneInterval - eps))
        {
            add (juce::MidiMessage::controllerEvent (ch, 74, sl), s + t);
            lastSlide = sl;
            slideT = t;
        }
        const int pr = to7 (press.at (t));
        if (std::abs (pr - lastPress) >= laneJump || (pr != lastPress && t - pressT >= laneInterval - eps))
        {
            add (juce::MidiMessage::channelPressureChange (ch, pr), s + t);
            lastPress = pr;
            pressT = t;
        }
    }
}
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

    // ---- events (collected, then sorted once: inserting into a MidiMessageSequence note by note is quadratic)
    std::vector<std::pair<double, juce::MidiMessage>> events;
    auto add = [&] (const juce::MidiMessage& m, double t) { events.emplace_back (t, m); };

    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i];
        const int ch = channelOf[i];
        const double s = n.start;
        const double len = n.length;

        if (len <= 0.0)
            continue;

        add (juce::MidiMessage::pitchWheel (ch, bendToMidi (evalCurve (n.bend, 0.0, 0.0f), opts.pitchBendRange)), s);
        add (juce::MidiMessage::controllerEvent (ch, 74, to7 (evalCurve (n.slide, 0.0, 0.5f))), s);
        add (juce::MidiMessage::channelPressureChange (ch, to7 (evalCurve (n.pressure, 0.0, 0.0f))), s);
        add (juce::MidiMessage::noteOn (ch, n.pitch, (juce::uint8) velTo7 (n.velocity)), s);
        emitExpression (n, s, len, opts.pitchBendRange, ch, add);
        add (juce::MidiMessage::noteOff (ch, n.pitch, (juce::uint8) velTo7 (n.releaseVelocity)), s + len);
    }

    std::stable_sort (events.begin(), events.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [t, m] : events)
        seq.addEvent (m, t);
    seq.updateMatchedPairs();
    return seq;
}

std::vector<PlayEvent> toPlayEvents (const juce::MidiMessageSequence& seq)
{
    std::vector<PlayEvent> out;
    out.reserve ((size_t) seq.getNumEvents());
    for (auto* ev : seq)
    {
        const auto& m = ev->message;
        const int size = m.getRawDataSize();
        if (m.isMetaEvent() || m.isSysEx() || size < 1 || size > 3)
            continue;
        PlayEvent e;
        e.beat = m.getTimeStamp();
        e.size = (juce::uint8) size;
        std::copy (m.getRawData(), m.getRawData() + size, e.data);
        out.push_back (e);
    }
    return out;
}

std::vector<PlayEvent> zoneConfigEvents (int pitchBendRange, int memberChannels)
{
    juce::MidiMessageSequence seq;
    for (const auto meta : juce::MPEMessages::setLowerZone (memberChannels, pitchBendRange, 2))
        seq.addEvent (meta.getMessage(), 0.0);
    return toPlayEvents (seq);
}

} // namespace dmpe
