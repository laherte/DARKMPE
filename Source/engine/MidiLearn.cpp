#include "MidiLearn.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dmpe
{
namespace
{

using scales::mod;
using scales::Scale;

struct Template
{
    std::vector<int> intervals; // above the root
    float prior;                // a small cost: the simpler chord wins a tie
};

const std::vector<Template>& templates()
{
    static const std::vector<Template> list {
        { { 0, 3, 7 }, 0.0f },      // minor
        { { 0, 4, 7 }, 0.0f },      // major
        { { 0, 7 }, 0.03f },        // power chord
        { { 0, 3, 6 }, 0.05f },     // diminished
        { { 0, 4, 8 }, 0.07f },     // augmented
        { { 0, 5, 7 }, 0.05f },     // sus4
        { { 0, 2, 7 }, 0.06f },     // sus2
        { { 0, 3, 7, 10 }, 0.05f }, // m7
        { { 0, 4, 7, 10 }, 0.05f }, // 7
        { { 0, 4, 7, 11 }, 0.05f }, // maj7
        { { 0, 3, 7, 11 }, 0.06f }, // m(maj7)
        { { 0, 3, 6, 10 }, 0.06f }, // m7b5
    };
    return list;
}

// Cost of a chord tone that does not sound: the root, the third and the seventh define a chord, the fifth
// is often left out.
float missingCost (int interval)
{
    switch (interval)
    {
        case 0:  return 0.3f;
        case 7:  return 0.08f;
        case 10:
        case 11: return 0.3f;
        default: return 0.25f; // thirds, sus notes, altered fifths
    }
}

struct ChordFit
{
    float score = 0.0f; // per unit of weight: +1 when every note is a chord tone and every chord tone sounds
    int root = -1;
    int shape = -1;     // index in templates()
};

float sum (const std::array<float, 12>& a)
{
    float s = 0.0f;
    for (float x : a)
        s += x;
    return s;
}

// The best chord for a stretch whose pitch classes weigh `w` over a lowest note `bassPc` (-1: none). `parts`
// are its pieces of about two beats: a chord tone should sound in each of them (at half the cost), so
// neighbouring triads (Am then F, or Bb Gm Bb) do not pass for one seventh chord (Fmaj7, Gm7), while a broken
// chord still covers every piece.
ChordFit bestChord (const std::array<float, 12>& w, const std::array<float, 12>& bassTime, const std::vector<std::array<float, 12>>& parts)
{
    ChordFit best;
    const float total = sum (w);
    if (total <= 1.0e-6f)
        return best;
    std::vector<float> partTotals;
    for (const auto& part : parts)
        partTotals.push_back (sum (part));
    const float bassTotal = sum (bassTime);
    best.score = -std::numeric_limits<float>::infinity();
    const auto& shapes = templates();
    for (int root = 0; root < 12; ++root)
        for (int k = 0; k < (int) shapes.size(); ++k)
        {
            const auto& t = shapes[(size_t) k];
            float in = 0.0f, missing = 0.0f, missingInParts = 0.0f, bassOnTones = 0.0f;
            int heard = 0;
            for (int iv : t.intervals)
            {
                const auto pc = (size_t) mod (root + iv, 12);
                in += w[pc];
                if (w[pc] < 0.04f * total)
                    missing += missingCost (iv);
                for (size_t q = 0; q < parts.size(); ++q)
                    if (partTotals[q] > 1.0e-6f && parts[q][pc] < 0.04f * partTotals[q])
                        missingInParts += missingCost (iv);
                bassOnTones += bassTime[pc];
            }
            for (float pt : partTotals)
                heard += pt > 1.0e-6f ? 1 : 0;
            const float share = in / total;
            // The lowest note, beat by beat: on the root is best, on another chord tone fine (an inversion).
            float bass = 0.0f;
            if (bassTotal > 1.0e-6f)
            {
                const float onRoot = bassTime[(size_t) root] / bassTotal, onTones = bassOnTones / bassTotal;
                bass = 0.3f * onRoot + 0.05f * (onTones - onRoot) - 0.15f * (1.0f - onTones);
            }
            const float score = share - 1.2f * (1.0f - share) - missing - (heard > 0 ? 0.5f * missingInParts / (float) heard : 0.0f)
                              + bass - t.prior;
            if (score > best.score)
                best = { score, root, k };
        }
    return best;
}

// Notes that start inside the loop, cut at its end. Starts and ends are put on a 1/48-beat grid (10 ms at
// 120 BPM): far finer than any groove, and it removes the sample jitter of a recording (a chord at 11.99996
// belongs to the bar at 12).
Phrase inLoop (const Phrase& clip)
{
    Phrase out;
    out.lengthBeats = learnBeats;
    auto snap = [] (double t) { return std::round (t * 48.0) / 48.0; };
    for (auto n : clip.notes)
    {
        const double end = snap (n.end());
        n.start = snap (n.start);
        n.length = std::max (1.0 / 48.0, end - n.start);
        if (n.start < 0.0 || n.start >= learnBeats - 1.0e-9)
            continue;
        n.length = std::max (1.0 / 64.0, std::min (n.length, learnBeats - n.start));
        out.notes.push_back (n);
    }
    out.sortByStart();
    return out;
}

// Onsets closer than this are one event (a chord, a flam).
constexpr double together = 1.0 / 48.0;

std::string chordText (const std::vector<Region>& chords)
{
    std::string s;
    for (size_t i = 0; i < chords.size() && i < 12; ++i)
        s += (i > 0 ? " " : "") + chordSymbol (chords[i]);
    return s;
}

std::string keyText (const KeyGuess& k)
{
    return std::string (scales::keyNames[mod (k.key, 12)]) + " " + scales::scaleNames[(size_t) k.scale];
}

} // namespace

KeyGuess detectKey (const KeyEvidence& ev, int currentKey, Scale currentScale)
{
    KeyGuess guess { mod (currentKey, 12), currentScale, false };
    const float total = sum (ev.notes);
    if (total <= 1.0e-6f)
        return guess;
    const float rootTotal = sum (ev.roots);

    static const Scale order[] = { Scale::naturalMinor, Scale::phrygian, Scale::dorian, Scale::harmonicMinor,
                                   Scale::minorPentatonic, Scale::phrygianDominant, Scale::hungarianMinor, Scale::locrian,
                                   Scale::aeolianFlat5, Scale::neapolitanMinor, Scale::doubleHarmonic };
    struct Candidate { int tonic; Scale scale; float fit, tonicScore; int rank; };
    std::vector<Candidate> all;
    for (int rank = 0; rank < (int) std::size (order); ++rank)
        for (int t = 0; t < 12; ++t)
        {
            float in = 0.0f;
            for (int iv : scales::intervals (order[rank]))
                in += ev.notes[(size_t) mod (t + iv, 12)];
            const float share = in / total;
            const float tonicScore = (rootTotal > 0.0f ? ev.roots[(size_t) t] / rootTotal : 0.0f)
                                   + (ev.first == t ? 0.3f : 0.0f) + (ev.last == t ? 0.1f : 0.0f)
                                   + (ev.lowest == t ? 0.15f : 0.0f) + 0.2f * ev.notes[(size_t) t] / total;
            all.push_back ({ t, order[rank], share - 3.0f * (1.0f - share), tonicScore, rank });
        }

    // The scales that hold the notes best; among them the tonic the roots point at; then what was already set.
    float bestFit = -1.0e9f;
    for (const auto& c : all)
        bestFit = std::max (bestFit, c.fit);
    std::vector<Candidate> fitting;
    for (const auto& c : all)
        if (c.fit >= bestFit - 0.02f)
            fitting.push_back (c);
    float bestTonic = -1.0e9f;
    for (const auto& c : fitting)
        bestTonic = std::max (bestTonic, c.tonicScore);
    const Candidate* pick = nullptr;
    auto preference = [&] (const Candidate& c) // the key already set first, then its scale
    {
        return (c.tonic == guess.key && c.scale == currentScale ? 4 : 0) + (c.tonic == guess.key ? 2 : 0) + (c.scale == currentScale ? 1 : 0);
    };
    for (const auto& c : fitting)
    {
        if (c.tonicScore < bestTonic - 0.1f) // the tonic must be clearly pointed at to move away from the key set
            continue;
        if (pick == nullptr || preference (c) > preference (*pick) || (preference (c) == preference (*pick) && c.rank < pick->rank))
            pick = &c;
    }
    if (pick != nullptr)
        guess = { pick->tonic, pick->scale, true };
    return guess;
}

std::vector<Region> chordsFromClip (const Phrase& input, double length)
{
    const auto clip = inLoop (input);
    constexpr double slot = 0.5;
    const int slots = std::max (1, (int) std::ceil (length / slot - 1.0e-9));

    // Per half beat: how long each pitch class sounds (an attack counts a little extra), and the lowest note.
    std::vector<std::array<float, 12>> weight ((size_t) slots);
    std::vector<int> lowest ((size_t) slots, 128);
    float everything = 0.0f;
    for (const auto& n : clip.notes)
        for (int s = std::max (0, (int) (n.start / slot)); s < slots && s * slot < n.end(); ++s)
        {
            const double overlap = std::min (n.end(), (s + 1) * slot) - std::max (n.start, s * slot);
            if (overlap <= 0.0)
                continue;
            const bool attack = n.start >= s * slot && n.start < (s + 1) * slot;
            const float x = (float) overlap + (attack ? 0.1f : 0.0f);
            weight[(size_t) s][(size_t) mod (n.pitch, 12)] += x;
            lowest[(size_t) s] = std::min (lowest[(size_t) s], n.pitch);
            everything += x;
        }
    if (everything <= 0.0f)
        return {};

    std::vector<std::array<float, 12>> prefix ((size_t) slots + 1);
    for (int s = 0; s < slots; ++s)
        for (int pc = 0; pc < 12; ++pc)
            prefix[(size_t) s + 1][(size_t) pc] = prefix[(size_t) s][(size_t) pc] + weight[(size_t) s][(size_t) pc];

    // A change of chord costs the evidence of a bit more than half a beat of the clip (more off the beat).
    const float change = 0.6f * everything / (float) length;

    // How many notes sound together: block chords check that a chord's tones sound in every two beats, broken
    // chords (one note at a time) only in every four, over stretches of two bars or more.
    float voices = 0.0f;
    int active = 0;
    for (const auto& w : weight)
    {
        int count = 0;
        for (float x : w)
            count += x > 0.1f * (float) slot ? 1 : 0;
        voices += (float) count;
        active += count > 0 ? 1 : 0;
    }
    const bool block = active > 0 && voices / (float) active >= 2.5f;
    const int partSlots = block ? 4 : 8;

    std::vector<float> best ((size_t) slots + 1, -std::numeric_limits<float>::infinity());
    std::vector<int> from ((size_t) slots + 1, 0);
    std::vector<ChordFit> chosen ((size_t) slots + 1);
    best[0] = 0.0f;
    for (int j = 1; j <= slots; ++j)
        for (int i = 0; i < j; ++i)
        {
            auto between = [&prefix] (int a, int b)
            {
                std::array<float, 12> w {};
                for (int pc = 0; pc < 12; ++pc)
                    w[(size_t) pc] = prefix[(size_t) b][(size_t) pc] - prefix[(size_t) a][(size_t) pc];
                return w;
            };
            const auto w = between (i, j);
            std::vector<std::array<float, 12>> parts;
            if (j - i >= (block ? 2 : 2 * partSlots))
            {
                const int count = std::max (2, (j - i + partSlots / 2) / partSlots);
                for (int q = 0; q < count; ++q)
                    parts.push_back (between (i + (j - i) * q / count, i + (j - i) * (q + 1) / count));
            }
            std::array<float, 12> bassTime {}; // how long each pitch class is the lowest note
            for (int s = i; s < j; ++s)
                if (lowest[(size_t) s] < 128)
                    bassTime[(size_t) mod (lowest[(size_t) s], 12)] += 1.0f;
            const float total = sum (w);
            const auto fit = bestChord (w, bassTime, parts);
            // Chords change on the bar line, then the half bar, the beat, rarely off the beat (broken chords, where
            // every note could start a new chord, even more so).
            const float where = i % 8 == 0 ? 0.0f : (i % 4 == 0 ? (block ? 0.15f : 0.5f) : (i % 2 == 0 ? (block ? 0.35f : 1.0f) : (block ? 0.6f : 1.5f)));
            const float value = best[(size_t) i] + fit.score * total - change * (1.0f + where);
            if (value > best[(size_t) j])
            {
                best[(size_t) j] = value;
                from[(size_t) j] = i;
                chosen[(size_t) j] = fit;
            }
        }

    // Walk back, then merge equal neighbours.
    std::vector<std::pair<int, int>> segments; // [i, j)
    for (int j = slots; j > 0; j = from[(size_t) j])
        segments.push_back ({ from[(size_t) j], j });
    std::reverse (segments.begin(), segments.end());

    std::vector<Region> out;
    int previousRoot = -1, previousShape = -1, previousBass = -2;
    for (auto [i, j] : segments)
    {
        const auto fit = chosen[(size_t) j];
        if (fit.root < 0)
        {
            if (! out.empty())
                out.back().length = j * slot - out.back().start; // silence: the chord rings on
            continue;
        }
        int low = 128;
        for (int s = i; s < j; ++s)
            low = std::min (low, lowest[(size_t) s]);
        const int bassPc = low < 128 && mod (low, 12) != fit.root ? mod (low, 12) : -1;

        if (fit.root == previousRoot && fit.shape == previousShape && bassPc == previousBass)
        {
            out.back().length = j * slot - out.back().start;
            continue;
        }
        Region r;
        r.start = out.empty() ? 0.0 : i * slot; // leading silence belongs to the first chord
        r.length = j * slot - r.start;
        for (int iv : templates()[(size_t) fit.shape].intervals)
            r.pitches.push_back (48 + fit.root + iv);
        r.bassPc = bassPc;
        out.push_back (r);
        previousRoot = fit.root;
        previousShape = fit.shape;
        previousBass = bassPc;
    }
    return out;
}

std::vector<BassRoot> bassRoots (const Phrase& input, double length)
{
    const auto clip = inLoop (input);
    const int halves = std::max (1, (int) std::ceil (length / 2.0 - 1.0e-9));
    std::vector<BassRoot> out;
    int previous = -1;
    std::vector<int> pcs ((size_t) halves, -1);
    for (int h = 0; h < halves; ++h)
    {
        std::array<float, 12> w {};
        const double h0 = h * 2.0, h1 = h0 + 2.0;
        for (const auto& n : clip.notes)
        {
            const double overlap = std::min (n.end(), h1) - std::max (n.start, h0);
            if (overlap <= 0.0)
                continue;
            const bool downbeat = std::abs (n.start - h0) < 0.06;
            w[(size_t) mod (n.pitch, 12)] += (float) overlap * (downbeat ? 2.0f : 1.0f);
        }
        int pc = -1;
        float most = 0.0f;
        for (int k = 0; k < 12; ++k)
            if (w[(size_t) k] > most + 1.0e-6f || (w[(size_t) k] > most - 1.0e-6f && k == previous && most > 0.0f))
            {
                most = w[(size_t) k];
                pc = k;
            }
        pcs[(size_t) h] = pc >= 0 ? pc : previous; // a silent half bar keeps the root
        previous = pcs[(size_t) h];
    }
    // A silent start takes the first root heard.
    int first = -1;
    for (int pc : pcs)
        if (pc >= 0) { first = pc; break; }
    if (first < 0)
        return out;
    for (auto& pc : pcs)
    {
        if (pc >= 0)
            break;
        pc = first;
    }
    for (int h = 0; h < halves; ++h)
    {
        const double start = h * 2.0, len = std::min (2.0, length - start);
        if (! out.empty() && out.back().pc == pcs[(size_t) h])
            out.back().length += len;
        else
            out.push_back ({ start, len, pcs[(size_t) h] });
    }
    return out;
}

std::vector<Region> chordsOnRoots (const std::vector<BassRoot>& roots, int key, Scale scale)
{
    std::vector<Region> out;
    for (const auto& b : roots)
    {
        Region r;
        r.start = b.start;
        r.length = b.length;
        if (scales::inScale (b.pc, key, scale))
        {
            const int degree = std::max (0, scales::degreeOf (scale, b.pc - key));
            for (int step : { 0, 2, 4 })
                r.pitches.push_back (scales::degreeToPitch (key + 48, scale, scales::mapDegree (scale, degree + step)));
        }
        else
            for (int iv : { 0, 4, 7 })
                r.pitches.push_back (48 + b.pc + iv);
        out.push_back (r);
    }
    return out;
}

Rate detectRate (const std::vector<double>& onsets)
{
    if (onsets.empty())
        return Rate::sixteenth;
    auto error = [&onsets] (double grid)
    {
        double e = 0.0;
        for (double o : onsets)
        {
            const double d = o / grid;
            e += std::pow ((d - std::round (d)) * grid, 2.0);
        }
        return e / (double) onsets.size();
    };
    constexpr double tolerance = 0.02 * 0.02; // about 10 ms at 120 BPM
    const double e16 = error (0.25), eTriplet = error (1.0 / 6.0), e32 = error (0.125);
    if (e16 <= tolerance)
        return Rate::sixteenth;
    if (eTriplet <= tolerance)
        return Rate::sixteenthTriplet;
    if (e32 <= tolerance)
        return Rate::thirtySecond;
    // Loose playing: the grid it sits closest to, 1/16 first.
    if (eTriplet < 0.6 * e16 && eTriplet <= e32)
        return Rate::sixteenthTriplet;
    if (e32 < 0.6 * e16)
        return Rate::thirtySecond;
    return Rate::sixteenth;
}

LearnResult analyseClip (LearnKind kind, const Phrase& input, int currentKey, Scale currentScale)
{
    LearnResult r;
    r.kind = kind;
    const auto clip = inLoop (input);
    if (clip.empty())
    {
        r.message = "Nothing heard: no notes started in the 4 bars";
        return r;
    }

    KeyEvidence ev;
    int lowestPitch = 128;
    for (const auto& n : clip.notes)
    {
        ev.notes[(size_t) mod (n.pitch, 12)] += (float) n.length;
        lowestPitch = std::min (lowestPitch, n.pitch);
    }
    ev.lowest = mod (lowestPitch, 12);

    // One line: the top note of every onset (a lead), or every onset (a rhythm).
    auto line = [&clip] (bool top)
    {
        std::vector<LearnedEvent> events;
        for (const auto& n : clip.notes)
        {
            if (! events.empty() && n.start - events.back().start < together)
            {
                auto& e = events.back();
                if (top && n.pitch > e.pitch)
                    e.pitch = n.pitch;
                e.velocity = std::max (e.velocity, n.velocity);
                e.length = std::max (e.length, n.length);
                continue;
            }
            events.push_back ({ n.start, n.length, top ? n.pitch : -1, n.velocity });
        }
        for (size_t i = 0; i + 1 < events.size(); ++i)
            events[i].length = std::min (events[i].length, events[i + 1].start - events[i].start); // one line
        return events;
    };
    auto onsets = [] (const std::vector<LearnedEvent>& events)
    {
        std::vector<double> v;
        for (const auto& e : events)
            v.push_back (e.start);
        return v;
    };

    switch (kind)
    {
        case LearnKind::chords:
        case LearnKind::count:
        {
            r.chords = chordsFromClip (clip, learnBeats);
            if (r.chords.empty())
            {
                r.message = "No chords found";
                return r;
            }
            for (const auto& c : r.chords) // what sounds in each chord (a chord ringing through silence adds nothing)
                for (const auto& n : clip.notes)
                {
                    const double overlap = std::min (n.end(), c.start + c.length) - std::max (n.start, c.start);
                    if (overlap > 0.0)
                        ev.roots[(size_t) mod (c.pitches.front(), 12)] += (float) overlap;
                }
            ev.first = mod (r.chords.front().pitches.front(), 12);
            ev.last = mod (r.chords.back().pitches.front(), 12);
            r.key = detectKey (ev, currentKey, currentScale);
            r.message = "Chords: " + chordText (r.chords) + "  -  " + keyText (r.key);
            break;
        }

        case LearnKind::bass:
        {
            const auto roots = bassRoots (clip, learnBeats);
            if (roots.empty())
            {
                r.message = "No bass notes found";
                return r;
            }
            for (const auto& b : roots)
                ev.roots[(size_t) b.pc] += (float) b.length;
            ev.first = roots.front().pc;
            ev.last = roots.back().pc;
            r.key = detectKey (ev, currentKey, currentScale);
            r.chords = chordsOnRoots (roots, r.key.key, r.key.scale);
            r.message = "Bass: " + chordText (r.chords) + "  -  " + keyText (r.key);
            break;
        }

        case LearnKind::lead:
        {
            r.events = line (true);
            ev.first = mod (r.events.front().pitch, 12);
            ev.last = mod (r.events.back().pitch, 12);
            r.key = detectKey (ev, currentKey, currentScale);
            r.rate = detectRate (onsets (r.events));
            r.message = "Lead: " + std::to_string (r.events.size()) + " notes on " + rateNames[(size_t) r.rate] + "  -  " + keyText (r.key);
            break;
        }

        case LearnKind::rhythm:
        {
            r.events = line (false);
            r.rate = detectRate (onsets (r.events));
            r.message = "Rhythm: " + std::to_string (r.events.size()) + " hits on " + rateNames[(size_t) r.rate];
            break;
        }
    }
    r.ok = true;
    return r;
}

} // namespace dmpe
