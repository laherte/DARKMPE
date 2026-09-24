#include "CinematicEngine.h"
#include "ExpressionShaper.h"
#include "Rng.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace dmpe
{
namespace
{

using scales::mod;

bool hasPc (const std::set<int>& pcs, int pc) { return pcs.count (mod (pc, 12)) != 0; }

float shapeCurve (GlideShape s, float x)
{
    x = std::clamp (x, 0.0f, 1.0f);
    switch (s)
    {
        case GlideShape::ease:     return x * x * (3.0f - 2.0f * x);
        case GlideShape::swoopIn:  return x * x * x;
        case GlideShape::swoopOut: return 1.0f - (1.0f - x) * (1.0f - x) * (1.0f - x);
        case GlideShape::linear:
        case GlideShape::count:    break;
    }
    return x;
}

float smoothstep (float a, float b, float x)
{
    const float t = std::clamp ((x - a) / std::max (1.0e-6f, b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

std::vector<int> transposed (std::vector<int> v, int semis)
{
    for (auto& x : v)
        x += semis;
    return v;
}

// Appends a shaped transition (from -> to between t0 and t1) to a curve, keeping it time-ordered.
void addTransition (Curve& c, double t0, double t1, float from, float to, GlideShape shape)
{
    t0 = std::max (t0, c.empty() ? 0.0 : c.back().t);
    t1 = std::max (t1, t0 + 1.0e-3);
    c.push_back ({ t0, from });
    constexpr int steps = 16;
    for (int i = 1; i <= steps; ++i)
    {
        const float x = (float) i / steps;
        c.push_back ({ t0 + (t1 - t0) * x, from + (to - from) * shapeCurve (shape, x) });
    }
}

size_t regionAt (const std::vector<Region>& regions, double t)
{
    size_t r = 0;
    while (r + 1 < regions.size() && regions[r + 1].start <= t)
        ++r;
    return r;
}

} // namespace

int estimateRoot (const std::vector<int>& pitches)
{
    if (pitches.empty())
        return 0;

    std::set<int> pcs;
    for (int p : pitches)
        pcs.insert (mod (p, 12));
    const int bassPc = mod (*std::min_element (pitches.begin(), pitches.end()), 12);

    int best = bassPc;
    float bestScore = -1.0f;
    for (int r : pcs)
    {
        float score = 0.0f;
        score += hasPc (pcs, r + 7) ? 2.0f : (hasPc (pcs, r + 6) ? 0.8f : 0.0f);
        score += (hasPc (pcs, r + 3) || hasPc (pcs, r + 4)) ? 2.0f : 0.0f;
        score += (hasPc (pcs, r + 10) || hasPc (pcs, r + 11)) ? 0.5f : 0.0f;
        score += (r == bassPc) ? 1.5f : 0.0f;
        if (score > bestScore)
        {
            bestScore = score;
            best = r;
        }
    }
    return best;
}

std::vector<Region> buildRegions (const Phrase& input, Reharm reharm, double lengthBeats)
{
    std::vector<Region> regions;
    const auto chords = detectChords (input);

    for (size_t i = 0; i < chords.size(); ++i)
    {
        const auto& c = chords[i];
        Region r;
        r.start = c.start;
        const double next = i + 1 < chords.size() ? chords[i + 1].start : std::max (lengthBeats, c.start + c.length);
        r.length = std::max (0.25, next - c.start);

        const int root = estimateRoot (c.pitches);
        int rootPitch = c.pitches.front();
        for (int p : c.pitches)
            if (mod (p, 12) == root) { rootPitch = p; break; }
        r.pitches.push_back (rootPitch);
        for (int p : c.pitches)
            if (p != rootPitch)
                r.pitches.push_back (p);
        regions.push_back (r);
    }

    if (reharm == Reharm::off || regions.empty())
        return regions;

    std::vector<Region> out;
    for (size_t i = 0; i < regions.size(); ++i)
    {
        auto r = regions[i];
        const int root = r.pitches.front();

        switch (reharm)
        {
            case Reharm::susResolve:
            {
                std::set<int> rel;
                for (int p : r.pitches) rel.insert (mod (p - root, 12));
                const int third = rel.count (3) ? 3 : (rel.count (4) ? 4 : -1);
                if (third > 0 && r.length >= 1.0)
                {
                    Region sus = r;
                    sus.length = std::min (2.0, r.length * 0.5);
                    for (auto& p : sus.pitches)
                        if (mod (p - root, 12) == third)
                            p += 5 - third; // 3rd -> 4th
                    r.start += sus.length;
                    r.length -= sus.length;
                    out.push_back (sus);
                }
                out.push_back (r);
                break;
            }

            case Reharm::chromaticApproach:
            {
                if (i + 1 < regions.size())
                {
                    const double approach = std::min (1.0, r.length * 0.25);
                    if (r.length - approach >= 0.5)
                    {
                        Region a;
                        a.start = r.start + r.length - approach;
                        a.length = approach;
                        a.pitches = transposed (regions[i + 1].pitches, 1);
                        r.length -= approach;
                        out.push_back (r);
                        out.push_back (a);
                        break;
                    }
                }
                out.push_back (r);
                break;
            }

            case Reharm::mediantShift:
            {
                if (r.length >= 2.0)
                {
                    Region m = r;
                    m.length = r.length * 0.5;
                    m.start = r.start + m.length;
                    m.pitches = transposed (r.pitches, (i % 2 == 0) ? 4 : -4);
                    r.length -= m.length;
                    out.push_back (r);
                    out.push_back (m);
                }
                else
                    out.push_back (r);
                break;
            }

            case Reharm::off:
            case Reharm::count:
                out.push_back (r);
                break;
        }
    }
    return out;
}

Phrase demoProgression (int key, scales::Scale scale, int bars)
{
    static const int degrees[] = { 0, 5, 2, 6 }; // i - VI - III - VII
    Phrase p;
    bars = std::clamp (bars, 1, 16);
    p.lengthBeats = std::max (4, bars) * 4.0;
    const int tonic = key + 48;
    const int count = std::max (4, bars);
    for (int b = 0; b < count; ++b)
        for (int step : { 0, 2, 4 })
        {
            Note n;
            n.start = b * 4.0;
            n.length = 4.0;
            n.pitch = scales::degreeToPitch (tonic, scale, degrees[b % 4] + step);
            p.notes.push_back (n);
        }
    return p;
}

Phrase cinematic (const Phrase& input, const CineParams& p)
{
    Phrase out;
    out.lengthBeats = input.lengthBeats;
    if (input.empty())
        return out;

    const auto regions = buildRegions (input, p.reharm, input.lengthBeats);
    if (regions.empty())
        return out;

    // ---- voice every region into fixed slots with minimal motion
    VoicingParams vp;
    vp.mode = (p.voicing == VoicingMode::asPlayed || p.voicing == VoicingMode::unisonStack) ? VoicingMode::epicSpread : p.voicing;
    vp.voices = std::clamp (p.voices, 2, 6);
    vp.lowPitch = p.lowPitch;
    vp.highPitch = std::max (p.highPitch, p.lowPitch + 24);
    vp.bassAnchor = true;
    vp.voiceLeading = true;

    std::vector<std::vector<int>> slots;
    for (const auto& r : regions)
    {
        Chord c;
        c.start = r.start;
        c.length = r.length;
        c.pitches = r.pitches;
        slots.push_back (voiceChordSlots (c, vp, slots.empty() ? nullptr : &slots.back()));
    }

    const int n = (int) slots.front().size();
    Rng rng ((uint64_t) p.seed * 977ull + 5ull);
    std::vector<float> lag ((size_t) n);
    for (int k = 0; k < n; ++k)
        lag[(size_t) k] = p.stagger * ((n > 1 ? (float) k / (float) (n - 1) : 0.0f) + 0.15f * rng.bipolar());

    auto medianPitch = [] (std::vector<int> v)
    {
        std::sort (v.begin(), v.end());
        return v[v.size() / 2];
    };

    std::vector<Note> notes;

    if (p.motion == Motion::morph)
    {
        for (int k = 0; k < n; ++k)
        {
            Note cur;
            cur.voiceIndex = k;
            cur.start = regions[0].start;
            cur.pitch = slots[0][(size_t) k];
            cur.bend.push_back ({ 0.0, 0.0f });
            float offset = 0.0f;

            for (size_t r = 1; r < regions.size(); ++r)
            {
                const int from = slots[r - 1][(size_t) k];
                const int to = slots[r][(size_t) k];
                const double boundary = regions[r].start;
                if (to == from)
                    continue;

                if (std::abs (to - cur.pitch) > 24)
                {
                    // Too far to bend musically: re-strike.
                    cur.length = boundary - cur.start;
                    notes.push_back (cur);
                    cur = Note {};
                    cur.voiceIndex = k;
                    cur.start = boundary;
                    cur.pitch = to;
                    cur.bend.push_back ({ 0.0, 0.0f });
                    offset = 0.0f;
                    continue;
                }

                const double dur = std::max (0.05, (double) p.glide * std::min (regions[r - 1].length, regions[r].length));
                double t0 = boundary - p.anticipation * dur + lag[(size_t) k] * dur * 0.75;
                t0 = std::max (t0, regions[r - 1].start + 0.02);
                double t1 = std::min (t0 + dur, regions[r].start + regions[r].length - 0.02);
                const float target = (float) (to - cur.pitch);
                addTransition (cur.bend, t0 - cur.start, t1 - cur.start, offset, target, p.shape);
                offset = target;
            }

            const auto& last = regions.back();
            cur.length = last.start + last.length - cur.start;
            notes.push_back (cur);
        }
    }
    else
    {
        for (size_t r = 0; r < regions.size(); ++r)
        {
            const auto& reg = regions[r];
            const int centre = medianPitch (slots[r]);
            const bool in = p.motion == Motion::bloom || p.motion == Motion::breathe;
            const bool outward = p.motion == Motion::collapse || p.motion == Motion::breathe;
            const double span = p.motion == Motion::breathe ? reg.length * 0.5 : reg.length;
            const double dur = std::max (0.05, (double) p.glide * span);

            for (int k = 0; k < n; ++k)
            {
                Note note;
                note.voiceIndex = k;
                note.start = reg.start;
                note.length = reg.length;
                note.pitch = slots[r][(size_t) k];
                const float away = (float) std::clamp (centre - note.pitch, -48, 48);
                const double delay = std::abs (lag[(size_t) k]) * dur * 0.5;

                note.bend.push_back ({ 0.0, in ? away : 0.0f });
                if (in)
                    addTransition (note.bend, delay, std::min (reg.length * 0.95, delay + dur), away, 0.0f, p.shape);
                if (outward)
                    addTransition (note.bend, std::max (note.bend.back().t, reg.length - dur - delay),
                                   reg.length - 0.01, 0.0f, away, p.shape);
                notes.push_back (note);
            }
        }
    }

    // ---- per-note expression: top-voice vibrato, pressure + timbre swell per chord
    int topPitch = 0;
    for (const auto& s : slots)
        topPitch = std::max (topPitch, *std::max_element (s.begin(), s.end()));

    for (auto& note : notes)
    {
        const int k = note.voiceIndex;
        const float voicePos = n > 1 ? (float) k / (float) (n - 1) : 0.5f;
        const bool isTop = note.pitch >= topPitch - 7 && voicePos > 0.7f;
        const float reg = std::clamp ((float) (note.pitch - 36) / 60.0f, 0.0f, 1.0f);

        Curve bend, slide, pressure;
        const double len = std::max (0.05, note.length);
        const int steps = std::max (1, (int) std::ceil (len / curveResolution));
        for (int i = 0; i <= steps; ++i)
        {
            const double t = std::min (len, i * curveResolution);
            const double abs = note.start + t;
            const auto& region = regions[regionAt (regions, abs)];
            const float x = (float) ((abs - region.start) / std::max (0.25, region.length));

            float b = evalCurve (note.bend, t, 0.0f);
            if (isTop && x > 0.35f && region.length >= 2.0)
                b += 0.12f * smoothstep (0.35f, 0.7f, x) * (float) std::sin (6.283185307 * 1.2 * (abs - region.start));
            bend.push_back ({ t, b });

            const float press = 0.18f + p.swell * 0.7f * smoothstep (0.0f, 0.75f, x)
                              - p.swell * 0.2f * smoothstep (0.92f, 1.0f, x);
            pressure.push_back ({ t, std::clamp (press, 0.0f, 1.0f) });

            const float open = p.swell * 0.55f * smoothstep (0.05f + 0.3f * voicePos * p.stagger, 1.0f, x);
            slide.push_back ({ t, std::clamp (0.15f + 0.3f * reg + open, 0.0f, 1.0f) });
        }

        note.bend = std::move (bend);
        note.slide = std::move (slide);
        note.pressure = std::move (pressure);
        note.lockedExpr = true;
        note.voiceCount = n;
        note.velocity = std::clamp (0.62f + 0.2f * (1.0f - voicePos) + 0.05f * rng.bipolar(), 0.1f, 1.0f);
        note.releaseVelocity = 0.3f;
    }

    out.notes = std::move (notes);
    out.sortByStart();
    return out;
}

} // namespace dmpe
