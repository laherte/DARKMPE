#include "VoicingEngine.h"
#include "Rng.h"
#include "Scales.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>

namespace dmpe
{
namespace
{

using scales::mod;

// Chord tones relative to the bass/root, unique pitch classes, 0 first.
std::vector<int> relativePcs (const std::vector<int>& pitches)
{
    std::set<int> pcs;
    const int root = pitches.front();
    for (int p : pitches)
        pcs.insert (mod (p - root, 12));
    return { pcs.begin(), pcs.end() };
}

int nearestTo (const std::vector<int>& pcs, int target, int exclude)
{
    int best = -1;
    for (int pc : pcs)
        if (pc != exclude && (best < 0 || std::abs (pc - target) < std::abs (best - target)))
            best = pc;
    return best < 0 ? target : best;
}

int firstOf (const std::vector<int>& pcs, std::initializer_list<int> wanted)
{
    for (int w : wanted)
        if (std::find (pcs.begin(), pcs.end(), w) != pcs.end())
            return w;
    return -1;
}

// Removes repeated entries, keeping the first occurrence (templates are in priority order).
std::vector<int> unique (const std::vector<int>& v)
{
    std::vector<int> out;
    for (int x : v)
        if (std::find (out.begin(), out.end(), x) == out.end())
            out.push_back (x);
    return out;
}

int thirdOf (const std::vector<int>& pcs)
{
    for (int c : { 3, 4, 2, 5 })
        if (std::find (pcs.begin(), pcs.end(), c) != pcs.end())
            return c;
    return 3; // imply minor: this is dark techno
}

// Semitone offsets above the root, ordered low -> high.
std::vector<int> templateFor (VoicingMode mode, const std::vector<int>& pcs)
{
    std::vector<int> others;
    for (int pc : pcs)
        if (pc != 0)
            others.push_back (pc);

    const int fifth = nearestTo (pcs, 7, 0);
    const int third = thirdOf (pcs);

    switch (mode)
    {
        case VoicingMode::drop2:
        {
            // Close position (4 notes), drop the second-from-top an octave, root doubled below as bass.
            std::vector<int> close { 0 };
            for (int o : others)
                close.push_back (o);
            for (int pad : { 12, fifth + 12, third + 12 })
                if (close.size() < 4)
                    close.push_back (pad);
            if (close.size() > 4)
                close.erase (close.begin() + 1, close.begin() + (long) (close.size() - 3));
            std::sort (close.begin(), close.end());
            close[2] -= 12;
            std::vector<int> out { 0 };
            for (int c : close)
                out.push_back (c + 12);
            std::sort (out.begin() + 1, out.end());
            return out;
        }

        case VoicingMode::openSpread:
        {
            std::vector<int> out { 0, fifth, third + 12 };
            for (int o : others)
                if (o != fifth && o != third)
                    out.push_back (o + 12);
            out.push_back (fifth + 12);
            out.push_back (third + 24);
            return out;
        }

        case VoicingMode::darkCluster:
        {
            std::vector<int> out { 0, fifth, third + 12, 13 };
            for (int o : others)
                if (o != fifth && o != third && o != 1)
                    out.push_back (o + 12);
            out.push_back (fifth + 12);
            return out;
        }

        case VoicingMode::epicSpread:
        {
            // Film-score spread over three octaves: root, fifth, octave and tenth, then the 7th in the middle and
            // colour tones (9, 11, b9, b13...) up high where they ring instead of muddying. Priority order:
            // with fewer voices the last entries are dropped.
            std::vector<int> out { 0, fifth, 12, third + 12 };
            const int seventh = firstOf (pcs, { 11, 10, 9 });
            if (seventh >= 0 && seventh != fifth)
                out.push_back (seventh + 12);
            bool colour = false;
            for (int pc : pcs)
                if (pc != 0 && pc != fifth && pc != third && pc != seventh)
                {
                    out.push_back (pc + 24);
                    colour = true;
                }
            out.push_back (fifth + 12);
            if (! colour)
                out.push_back (third + 24);
            out.push_back (24);
            return unique (out);
        }

        case VoicingMode::gothic:
        {
            const bool minor = third == 3;
            std::vector<int> out { 0, fifth, 12, third + 12, 25, fifth + 12, minor ? 23 : 22 };
            for (int pc : pcs)
                if (pc != 0 && pc != fifth && pc != third)
                    out.push_back (pc + 24);
            out.push_back (third + 24);
            return unique (out);
        }

        case VoicingMode::hyperSpread:
        {
            int colour = -1;
            for (int pc : pcs)
                if (pc != 0 && pc != fifth && pc != third)
                    colour = colour < 0 ? pc : colour;
            return unique ({ 0, 12, fifth + 12, third + 24, fifth + 24, colour >= 0 ? colour + 36 : 38, 36, third + 36 });
        }

        case VoicingMode::quartal:
            return { 0, 5, 10, 15, 20, 26 };

        case VoicingMode::powerOctave:
            return { 0, 7, 12, 19, 24, 31 };

        case VoicingMode::add9_11:
        {
            std::vector<int> out { 0 };
            for (int o : others)
                out.push_back (o);
            out.push_back (14);
            out.push_back (17);
            std::sort (out.begin(), out.end());
            return out;
        }

        case VoicingMode::asPlayed:
        case VoicingMode::unisonStack:
        case VoicingMode::count:
            break;
    }

    std::vector<int> out { 0 };
    for (int o : others)
        out.push_back (o);
    return out;
}

std::vector<int> fitCount (std::vector<int> tpl, int voices)
{
    const size_t base = tpl.size();
    for (size_t i = 0; tpl.size() < (size_t) voices; ++i)
        tpl.push_back (tpl[i % base] + 12 * (int) (1 + i / base));
    tpl.resize ((size_t) voices);
    return tpl;
}

int placeNear (int pc, int target, int lo, int hi)
{
    int p = target + mod (pc - target, 12);
    if (p - target > 6)
        p -= 12;
    while (p < lo) p += 12;
    while (p > hi) p -= 12;
    return p;
}

double centroid (const std::vector<int>& v)
{
    return v.empty() ? 0.0 : std::accumulate (v.begin(), v.end(), 0.0) / (double) v.size();
}

// Returns pitches indexed by voice slot.
std::vector<int> voiceChord (const Chord& chord, const VoicingParams& p, const std::vector<int>* prev)
{
    const int n = std::clamp (p.voices, 1, 8);
    const int lo = std::min (p.lowPitch, p.highPitch - 12);
    const int hi = p.highPitch;

    if (p.mode == VoicingMode::asPlayed)
    {
        std::vector<int> out = chord.pitches;
        if (prev == nullptr || ! p.voiceLeading || prev->size() != out.size())
            return out;

        // Keep pitches but re-assign voice slots so each slot moves as little as possible.
        std::vector<int> perm (out.size());
        std::iota (perm.begin(), perm.end(), 0);
        std::vector<int> best = out;
        long bestCost = -1;
        if (out.size() <= 6)
        {
            do
            {
                long cost = 0;
                for (size_t i = 0; i < perm.size(); ++i)
                    cost += std::abs (out[(size_t) perm[i]] - (*prev)[i]);
                if (bestCost < 0 || cost < bestCost)
                {
                    bestCost = cost;
                    for (size_t i = 0; i < perm.size(); ++i)
                        best[i] = out[(size_t) perm[i]];
                }
            } while (std::next_permutation (perm.begin(), perm.end()));
        }
        return best;
    }

    const int rootPc = mod (chord.pitches.front(), 12);

    if (p.mode == VoicingMode::unisonStack)
    {
        const int top = std::clamp (chord.pitches.back(), lo, hi);
        return std::vector<int> ((size_t) n, top);
    }

    const auto tpl = fitCount (templateFor (p.mode, relativePcs (chord.pitches)), n);

    // The chord is built on the root; only the bass voice takes the inversion / pedal note.
    const int rootBase = lo + mod (rootPc - lo, 12);
    const int bass = chord.bassPc >= 0 ? lo + mod (chord.bassPc - lo, 12) : rootBase;
    std::vector<int> shaped;
    for (size_t i = 0; i < tpl.size(); ++i)
    {
        int v = i == 0 ? bass : rootBase + tpl[i];
        if (i > 0)
            while (v <= bass) v += 12;
        while (v > hi) v -= 12;
        shaped.push_back (v);
    }

    if (prev == nullptr || prev->size() != (size_t) n)
    {
        std::sort (shaped.begin(), shaped.end());
        return shaped;
    }

    if (! p.voiceLeading)
    {
        // Keep the shape; move the upper structure by octaves toward the previous centroid.
        const size_t first = p.bassAnchor ? 1 : 0;
        std::vector<int> upper (shaped.begin() + (long) first, shaped.end());
        std::vector<int> prevUpper (prev->begin() + (long) first, prev->end());
        const double target = centroid (prevUpper);
        int bestShift = 0;
        double bestDist = 1.0e9;
        for (int k = -2; k <= 2; ++k)
        {
            bool ok = true;
            for (int u : upper)
                ok = ok && u + 12 * k >= lo && u + 12 * k <= hi;
            const double d = std::abs (centroid (upper) + 12 * k - target);
            if (ok && d < bestDist)
            {
                bestDist = d;
                bestShift = k;
            }
        }
        std::sort (shaped.begin() + (long) first, shaped.end());
        for (size_t i = first; i < shaped.size(); ++i)
            shaped[i] += 12 * bestShift;
        return shaped;
    }

    // Minimal-motion voice leading: permute the pitch classes over the previous voices.
    const size_t first = p.bassAnchor ? 1 : 0;
    std::vector<int> out ((size_t) n);
    if (p.bassAnchor)
        out[0] = bass;

    std::vector<int> pcs;
    for (size_t i = first; i < tpl.size(); ++i)
        pcs.push_back (mod (rootPc + tpl[i], 12));
    std::sort (pcs.begin(), pcs.end());

    const int upperLo = p.bassAnchor ? bass + 1 : lo;
    long bestCost = -1;
    std::vector<int> bestUpper;
    do
    {
        std::vector<int> cand;
        long cost = 0;
        for (size_t i = 0; i < pcs.size(); ++i)
        {
            const int target = (*prev)[first + i];
            const int v = placeNear (pcs[i], target, upperLo, hi);
            cost += std::abs (v - target);
            cand.push_back (v);
        }
        std::vector<int> sorted = cand;
        std::sort (sorted.begin(), sorted.end());
        if (std::adjacent_find (sorted.begin(), sorted.end()) != sorted.end())
            cost += 24; // discourage unisons unless the chord needs them

        if (bestCost < 0 || cost < bestCost)
        {
            bestCost = cost;
            bestUpper = cand;
        }
    } while (std::next_permutation (pcs.begin(), pcs.end()));

    for (size_t i = 0; i < bestUpper.size(); ++i)
        out[first + i] = bestUpper[i];
    return out;
}

Phrase voiceMelody (const Phrase& input, const VoicingParams& p)
{
    Phrase out;
    out.lengthBeats = input.lengthBeats;

    std::vector<int> offsets { 0 };
    if (p.mode == VoicingMode::unisonStack)
        offsets.assign ((size_t) std::clamp (p.voices, 1, 6), 0);
    else if (p.mode == VoicingMode::powerOctave)
        offsets = fitCount ({ 0, 12, 19, 24 }, std::clamp (p.voices, 1, 4));
    else if (p.mode == VoicingMode::darkCluster)
        offsets = { 0, -11 }; // b9 shadow below
    else if (p.mode == VoicingMode::openSpread)
        offsets = { 0, -12 };

    Rng rng (99);
    auto src = input.notes;
    std::vector<bool> glides (src.size(), false);
    for (size_t i = 1; i < src.size(); ++i)
    {
        const bool legato = src[i].start - src[i - 1].end() < 0.06 && src[i].pitch != src[i - 1].pitch;
        const bool playerBent = src[i].hasSourceExpr && ! src[i].bend.empty(); // the performer already slid
        if (p.glide && legato && ! playerBent && rng.chance (p.glideProb))
        {
            glides[i] = true;
            src[i - 1].length = src[i].start - src[i - 1].start;
        }
    }

    const int count = (int) offsets.size();
    for (size_t i = 0; i < src.size(); ++i)
    {
        for (int v = 0; v < count; ++v)
        {
            Note n = src[i];
            n.pitch = std::clamp (n.pitch + offsets[(size_t) v], 0, 127);
            n.voiceIndex = v;
            n.voiceCount = count;
            n.velocity = std::clamp (n.velocity - 0.06f * (float) v, 0.05f, 1.0f);
            n.glideFrom = glides[i] ? std::clamp (src[i - 1].pitch + offsets[(size_t) v], 0, 127) : -1;
            out.notes.push_back (n);
        }
    }
    out.sortByStart();
    return out;
}

// A voice takes the imported expression of the chord note closest in pitch (exact match first).
const Note* inheritExpression (Note& voice, const Chord& chord)
{
    const Note* best = nullptr;
    for (const auto& s : chord.sources)
    {
        if (! s.hasSourceExpr)
            continue;
        const int d = std::abs (mod (s.pitch - voice.pitch + 6, 12) - 6) * 4 + std::abs (s.pitch - voice.pitch) / 12;
        const int bd = best == nullptr ? 1 << 30
                                       : std::abs (mod (best->pitch - voice.pitch + 6, 12) - 6) * 4 + std::abs (best->pitch - voice.pitch) / 12;
        if (d < bd)
            best = &s;
    }
    if (best == nullptr)
        return nullptr;

    voice.bend = best->bend;
    voice.slide = best->slide;
    voice.pressure = best->pressure;
    voice.hasSourceExpr = true;
    return best;
}

// "As Played" on polyphonic input: every input note survives untouched (timing, unisons, expression);
// only voice slots and optional glides between consecutive chords are added.
Phrase playedAsIs (const Phrase& input, const VoicingParams& p)
{
    Phrase out;
    out.lengthBeats = input.lengthBeats;

    std::vector<Note> prevNotes;
    double prevEnd = -1.0e9;

    for (const auto& chord : detectChords (input))
    {
        auto notes = chord.sources;
        std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.pitch < b.pitch; });
        const bool connected = ! prevNotes.empty() && chord.start - prevEnd < 0.25;
        std::vector<bool> used (prevNotes.size(), false);

        for (size_t v = 0; v < notes.size(); ++v)
        {
            auto& n = notes[v];
            n.voiceIndex = (int) v;
            n.voiceCount = (int) notes.size();

            // Greedy: slide in from the closest unused note of the previous chord.
            if (p.glide && connected && ! (n.hasSourceExpr && ! n.bend.empty()))
            {
                int best = -1;
                for (size_t k = 0; k < prevNotes.size(); ++k)
                    if (! used[k] && (best < 0 || std::abs (prevNotes[k].pitch - n.pitch) < std::abs (prevNotes[(size_t) best].pitch - n.pitch)))
                        best = (int) k;
                if (best >= 0)
                {
                    used[(size_t) best] = true;
                    if (prevNotes[(size_t) best].pitch != n.pitch && std::abs (prevNotes[(size_t) best].pitch - n.pitch) <= 12)
                        n.glideFrom = prevNotes[(size_t) best].pitch;
                }
            }
            out.notes.push_back (n);
        }

        prevNotes = notes;
        prevEnd = chord.start + chord.length;
    }

    out.sortByStart();
    return out;
}

} // namespace

std::vector<int> voiceChordSlots (const Chord& chord, const VoicingParams& p, const std::vector<int>* prev)
{
    return voiceChord (chord, p, prev);
}

bool isMonophonic (const Phrase& input)
{
    if (input.notes.size() < 2)
        return true;

    const auto chords = detectChords (input);
    int multi = 0;
    for (const auto& c : chords)
        multi += c.pitches.size() > 1 ? 1 : 0;

    auto notes = input.notes;
    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    int longOverlaps = 0;
    double latestEnd = notes.front().end();
    for (size_t i = 1; i < notes.size(); ++i)
    {
        // Overlap longer than a legato hand-off (1/4 beat, or half the note) means two voices.
        const double overlap = latestEnd - notes[i].start;
        if (overlap > std::min (0.25, notes[i].length * 0.5))
            ++longOverlaps;
        latestEnd = std::max (latestEnd, notes[i].end());
    }

    const double n = (double) chords.size();
    return multi <= 0.1 * n && longOverlaps <= 0.1 * (double) notes.size();
}

std::vector<Chord> detectChords (const Phrase& input, double window)
{
    auto notes = input.notes;
    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });

    std::vector<Chord> chords;
    for (size_t i = 0; i < notes.size();)
    {
        Chord c;
        c.start = notes[i].start;
        double end = notes[i].end();
        float vel = 0.0f;
        size_t j = i;
        std::set<int> pitches;
        for (; j < notes.size() && notes[j].start - c.start < window; ++j)
        {
            pitches.insert (notes[j].pitch);
            c.sources.push_back (notes[j]);
            end = std::max (end, notes[j].end());
            vel += notes[j].velocity;
        }
        c.pitches.assign (pitches.begin(), pitches.end());
        c.length = end - c.start;
        c.velocity = vel / (float) (j - i);
        chords.push_back (c);
        i = j;
    }

    for (size_t k = 0; k + 1 < chords.size(); ++k)
        chords[k].length = std::min (chords[k].length, chords[k + 1].start - chords[k].start);

    return chords;
}

Phrase applyVoicing (const Phrase& input, const VoicingParams& p)
{
    if (input.empty())
        return input;

    if (isMonophonic (input))
        return voiceMelody (input, p);

    if (p.mode == VoicingMode::asPlayed)
        return playedAsIs (input, p);

    const auto chords = detectChords (input);
    Phrase out;
    out.lengthBeats = input.lengthBeats;

    std::vector<int> prev;
    double prevEnd = -1.0e9;

    for (const auto& chord : chords)
    {
        const auto voiced = voiceChord (chord, p, prev.empty() ? nullptr : &prev);
        const bool connected = ! prev.empty() && chord.start - prevEnd < 0.25 && prev.size() == voiced.size();
        const int n = (int) voiced.size();

        // Strum order by pitch rank.
        std::vector<int> order ((size_t) n);
        std::iota (order.begin(), order.end(), 0);
        std::stable_sort (order.begin(), order.end(), [&] (int a, int b) { return voiced[(size_t) a] < voiced[(size_t) b]; });
        std::vector<int> rank ((size_t) n);
        for (int r = 0; r < n; ++r)
            rank[(size_t) order[(size_t) r]] = p.strumDown ? n - 1 - r : r;

        for (int v = 0; v < n; ++v)
        {
            Note note;
            const double offset = p.strum * rank[(size_t) v];
            note.start = chord.start + offset;
            note.length = std::max (0.05, chord.length - offset);
            note.pitch = std::clamp (voiced[(size_t) v], 0, 127);
            note.velocity = std::clamp (chord.velocity - 0.04f * (float) rank[(size_t) v] * (p.strumDown ? -1.0f : 1.0f), 0.05f, 1.0f);
            note.voiceIndex = v;
            note.voiceCount = n;
            if (const auto* src = inheritExpression (note, chord))
            {
                // Keep a little of the player's timing feel (rolled / late notes), never more than 1/8 beat.
                const double feel = std::clamp (src->start - chord.start, 0.0, 0.125);
                note.start += feel;
                note.length = std::max (0.05, note.length - feel);
            }
            if (p.glide && connected && prev[(size_t) v] != note.pitch)
                note.glideFrom = prev[(size_t) v];
            out.notes.push_back (note);
        }

        prev = voiced;
        prevEnd = chord.start + chord.length;
    }

    out.sortByStart();
    return out;
}

} // namespace dmpe
