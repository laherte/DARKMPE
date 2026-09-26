#include "LeadHarmony.h"

#include <algorithm>
#include <set>

namespace dmpe
{
namespace
{

using scales::mod;
using scales::Scale;

std::set<int> scaleSet (int tonic, Scale s)
{
    std::set<int> out;
    for (int iv : scales::intervals (s))
        out.insert (mod (tonic + iv, 12));
    return out;
}

// Diatonic chord on a degree of the key scale (triad, or a seventh chord when `seventh`), from C3..B3.
Region diatonicChord (int key, Scale s, int degree, bool seventh)
{
    Region r;
    const int tonic = key + 48;
    for (int step : { 0, 2, 4, 6 })
        if (step < 6 || seventh)
            r.pitches.push_back (scales::degreeToPitch (tonic, s, scales::mapDegree (s, degree + step)));
    return r;
}

// Scale Lock on a progression chord: the root goes to the nearest scale note, and the chord becomes the
// scale's own chord on it (Fm in A minor -> F, E7 -> Em7, Bb -> Bdim).
Region lockChord (const Region& in, int key, Scale s)
{
    const int root = scales::snap (in.pitches.front(), key, s);
    const int degree = std::max (0, scales::degreeOf (s, root - key));
    Region r = diatonicChord (key, s, degree, in.pitches.size() >= 4);
    r.start = in.start;
    r.length = in.length;
    r.bassPc = in.bassPc >= 0 ? mod (scales::snap (in.bassPc + 48, key, s), 12) : -1;
    return r;
}

// The scale the lines use over a chord: the key scale when it holds every chord tone, otherwise the scale
// (same type preferred, then the other dark 7-note scales) that holds them all and shares most with the key.
ChordSpan spanFor (const Region& chord, int key, Scale keyScale)
{
    ChordSpan span;
    span.start = chord.start;
    span.length = chord.length;
    span.chord = chord;

    std::set<int> tones;
    for (int p : chord.pitches)
        tones.insert (mod (p, 12));
    if (chord.bassPc >= 0)
        tones.insert (chord.bassPc);
    const int rootPc = mod (chord.pitches.front(), 12);

    const auto keySet = scaleSet (key, keyScale);
    auto holds = [&tones] (const std::set<int>& set)
    {
        return std::all_of (tones.begin(), tones.end(), [&set] (int pc) { return set.count (pc) != 0; });
    };

    span.scaleTonic = key;
    span.scale = keyScale;
    if (holds (keySet))
    {
        span.rootDegree = std::max (0, scales::degreeOf (keyScale, rootPc - key));
        return span;
    }

    int bestScore = -1;
    for (int type = 0; type < (int) Scale::count; ++type)
    {
        const auto s = (Scale) type;
        if (scales::intervals (s).size() != 7)
            continue;
        for (int k = 0; k < 12; ++k)
        {
            const int tonic = mod (key + k, 12);
            const auto set = scaleSet (tonic, s);
            if (! holds (set))
                continue;
            int shared = 0;
            for (int pc : set)
                shared += keySet.count (pc) != 0 ? 1 : 0;
            const int score = 10 * shared + (s == keyScale ? 4 : 0) + (s == Scale::naturalMinor ? 1 : 0) + (tonic == key ? 3 : 0);
            if (score > bestScore)
            {
                bestScore = score;
                span.scaleTonic = tonic;
                span.scale = s;
            }
        }
    }

    if (bestScore < 0) // no scale holds it (odd colours): stay in the key, on the nearest root
    {
        span.scaleTonic = key;
        span.scale = keyScale;
        span.rootDegree = std::max (0, scales::degreeOf (keyScale, scales::snap (rootPc + 48, key, keyScale) - key));
        return span;
    }
    span.rootDegree = std::max (0, scales::degreeOf (span.scale, rootPc - span.scaleTonic));
    return span;
}

} // namespace

bool HarmonyTrack::inKey() const
{
    return std::all_of (spans.begin(), spans.end(), [this] (const ChordSpan& s)
                        { return s.scale == scale && mod (s.scaleTonic, 12) == key; });
}

const ChordSpan& HarmonyTrack::at (double beat) const
{
    for (size_t i = spans.size(); i-- > 1;)
        if (beat >= spans[i].start - 1.0e-9)
            return spans[i];
    return spans.front();
}

int HarmonyTrack::pitch (double beat, int tonicPitch, int degree) const
{
    const auto& s = at (beat);
    int offset = mod (s.scaleTonic - key, 12);
    if (offset > 6)
        offset -= 12;
    return scales::degreeToPitch (tonicPitch + offset, s.scale, scales::mapDegree (s.scale, s.rootDegree + degree));
}

int HarmonyTrack::bass (double beat, int tonicPitch) const
{
    const auto& s = at (beat);
    const int root = pitch (beat, tonicPitch, 0);
    if (s.chord.bassPc < 0)
        return root;
    const int up = mod (s.chord.bassPc - root, 12);
    return root + (up > 4 ? up - 12 : up);
}

int HarmonyTrack::step (double beat, int pitch, int steps) const
{
    const auto& s = at (beat);
    return scales::stepFrom (pitch, s.scaleTonic, s.scale, steps);
}

bool HarmonyTrack::contains (double beat, int pitch) const
{
    const auto& s = at (beat);
    return scales::inScale (pitch, s.scaleTonic, s.scale);
}

std::vector<Region> HarmonyTrack::chords() const
{
    std::vector<Region> out;
    for (const auto& s : spans)
    {
        auto r = s.chord;
        r.start = s.start;
        r.length = s.length;
        out.push_back (r);
    }
    return out;
}

HarmonyTrack buildHarmony (const HarmonySpec& spec, SectionMarks* marks)
{
    HarmonyTrack track;
    track.key = mod (spec.key, 12);
    track.scale = spec.scale;
    const int bars = std::clamp (spec.bars, 1, 16);

    std::vector<Region> chords;
    switch (spec.source)
    {
        case HarmonySource::progression:
        {
            auto hp = spec.progression;
            hp.key = spec.key;
            hp.scale = spec.scale;
            hp.bars = bars;
            chords = generateProgression (hp, marks);
            if (spec.scaleLock)
                for (auto& c : chords)
                    c = lockChord (c, spec.key, spec.scale);
            break;
        }

        case HarmonySource::learned:
            // Your chords, as played, every 4 bars.
            for (int rep = 0; rep * 16 < bars * 4 && ! spec.learnedChords.empty(); ++rep)
                for (auto c : spec.learnedChords)
                {
                    c.start += rep * 16.0;
                    if (c.start >= bars * 4.0 - 1.0e-9 || c.pitches.empty())
                        continue;
                    c.length = std::min (c.length, bars * 4.0 - c.start);
                    chords.push_back (c);
                }
            if (! chords.empty())
                break;
            [[fallthrough]]; // nothing learned yet: the tonic

        case HarmonySource::tonic:
        {
            auto r = diatonicChord (spec.key, spec.scale, 0, false);
            r.start = 0.0;
            r.length = bars * 4.0;
            chords.push_back (r);
            break;
        }

        case HarmonySource::style:
        case HarmonySource::count:
            for (int bar = 0; bar < bars; ++bar)
            {
                const auto& deg = spec.styleDegrees;
                auto r = diatonicChord (spec.key, spec.scale, deg.empty() ? 0 : deg[(size_t) bar % deg.size()], false);
                r.start = bar * 4.0;
                r.length = 4.0;
                chords.push_back (r);
            }
            break;
    }

    for (const auto& c : chords)
        track.spans.push_back (spanFor (c, spec.key, spec.scale));

    // The style's degrees are kept as written (on a 5-note scale two degrees can share a note, and the lines
    // count their steps from the degree, as they always did).
    if (spec.source == HarmonySource::style && ! spec.styleDegrees.empty())
        for (size_t i = 0; i < track.spans.size(); ++i)
            track.spans[i].rootDegree = spec.styleDegrees[i % spec.styleDegrees.size()];
    return track;
}

void lockRegions (std::vector<Region>& regions, int key, scales::Scale scale)
{
    for (auto& r : regions)
    {
        std::vector<int> out;
        for (int p : r.pitches)
        {
            const int q = scales::snap (p, key, scale);
            if (std::find (out.begin(), out.end(), q) == out.end())
                out.push_back (q);
        }
        r.pitches = out;
        if (r.bassPc >= 0)
            r.bassPc = mod (scales::snap (r.bassPc + 48, key, scale), 12);
    }
}

} // namespace dmpe
