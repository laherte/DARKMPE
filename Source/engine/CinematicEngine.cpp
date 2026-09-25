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

std::vector<int> transposed (std::vector<int> v, int semis)
{
    for (auto& x : v)
        x += semis;
    return v;
}

// The regular 1/32-beat grid plus every point of an authored curve, so fast transitions keep their shape.
std::vector<double> sampleTimes (const Curve& c, double len)
{
    std::vector<double> ts;
    for (int i = 0; i * curveResolution < len; ++i)
        ts.push_back (i * curveResolution);
    ts.push_back (len);
    for (const auto& pt : c)
        if (pt.t > 0.0 && pt.t < len)
            ts.push_back (pt.t);
    std::sort (ts.begin(), ts.end());
    ts.erase (std::unique (ts.begin(), ts.end(), [] (double a, double b) { return b - a < 1.0e-6; }), ts.end());
    return ts;
}

size_t regionAt (const std::vector<Region>& regions, double t)
{
    size_t r = 0;
    while (r + 1 < regions.size() && regions[r + 1].start <= t + 1.0e-9)
        ++r;
    return r;
}

std::vector<bool> euclid16 (int pulses)
{
    std::vector<bool> pat (16, false);
    for (int i = 0; i < 16; ++i)
        pat[(size_t) i] = ((i * pulses) % 16) < pulses;
    pat[0] = true;
    return pat;
}

int medianPitch (std::vector<int> v)
{
    std::sort (v.begin(), v.end());
    return v[v.size() / 2];
}

// Suspension of a voice entering a chord: the step above the 3rd (4-3), above the root (9-8 / b9-8) or above the
// fifth (6-5 / b6-5). Darkness prefers the semitone versions.
int suspensionFor (int pitch, int rootPitch, float darkness)
{
    const int rel = mod (pitch - rootPitch, 12);
    const bool dark = darkness > 0.5f;
    if (rel == 3 || rel == 4) return 5 - rel;
    if (rel == 0)             return dark ? 1 : 2;
    if (rel == 7)             return dark ? 1 : 2;
    return 0;
}

} // namespace

// ------------------------------------------------------------------ harmony
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

std::vector<Region> regionsFromPhrase (const Phrase& input, double lengthBeats)
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
    return regions;
}

std::vector<Region> reharmonise (std::vector<Region> regions, Reharm reharm, int tonicPc)
{
    if (reharm == Reharm::off || reharm == Reharm::suspensions || regions.empty())
        return regions;

    if (reharm == Reharm::tonicPedal)
    {
        for (auto& r : regions)
            r.bassPc = mod (tonicPc, 12);
        return regions;
    }

    if (reharm == Reharm::planing)
    {
        // Parallel harmony: every chord takes the interval structure of the first one.
        const auto& first = regions.front();
        std::vector<int> shape;
        for (int p : first.pitches)
            shape.push_back (p - first.pitches.front());
        for (auto& r : regions)
        {
            r.pitches = transposed (shape, r.pitches.front());
            r.bassPc = -1;
        }
        return regions;
    }

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
            case Reharm::tritoneApproach:
            {
                if (i + 1 < regions.size())
                {
                    const double approach = std::min (1.0, r.length * 0.25);
                    if (r.length - approach >= 0.5)
                    {
                        Region a;
                        a.start = r.start + r.length - approach;
                        a.length = approach;
                        a.pitches = transposed (regions[i + 1].pitches, reharm == Reharm::tritoneApproach ? 6 : 1);
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
                    m.bassPc = r.bassPc >= 0 ? mod (r.bassPc + ((i % 2 == 0) ? 4 : -4), 12) : -1;
                    r.length -= m.length;
                    out.push_back (r);
                    out.push_back (m);
                }
                else
                    out.push_back (r);
                break;
            }

            case Reharm::off:
            case Reharm::suspensions:
            case Reharm::tonicPedal:
            case Reharm::planing:
            case Reharm::count:
                out.push_back (r);
                break;
        }
    }
    return out;
}

std::vector<Region> buildRegions (const Phrase& input, Reharm reharm, double lengthBeats)
{
    auto regions = regionsFromPhrase (input, lengthBeats);
    const int tonic = regions.empty() ? 0 : mod (regions.front().pitches.front(), 12);
    return reharmonise (std::move (regions), reharm, tonic);
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

// ------------------------------------------------------------------ motion
Phrase cinematic (const Phrase& input, const CineParams& p, std::vector<Region>* usedRegions)
{
    auto regions = regionsFromPhrase (input, input.lengthBeats);
    const int tonic = regions.empty() ? 0 : mod (regions.front().pitches.front(), 12);
    return cinematicRegions (std::move (regions), input.lengthBeats, p, tonic, usedRegions);
}

Phrase cinematicRegions (std::vector<Region> regions, double lengthBeats, const CineParams& p, int tonicPc,
                         std::vector<Region>* usedRegions)
{
    Phrase out;
    out.lengthBeats = lengthBeats;
    if (regions.empty())
        return out;

    colourRegions (regions, p.tension, p.darkness, p.seed);
    regions = reharmonise (std::move (regions), p.reharm, tonicPc);
    if (usedRegions != nullptr)
        *usedRegions = regions;

    // ---- voice every region into fixed slots with minimal motion
    VoicingParams vp;
    vp.mode = (p.voicing == VoicingMode::asPlayed || p.voicing == VoicingMode::unisonStack) ? VoicingMode::epicSpread : p.voicing;
    vp.voices = std::clamp (p.voices, 2, 8);
    vp.lowPitch = p.lowPitch;
    vp.highPitch = std::max (p.highPitch, p.lowPitch + 24);
    vp.bassAnchor = true;
    vp.voiceLeading = true;

    std::vector<std::vector<int>> slots;
    auto voiceAll = [&] (const std::vector<int>* before)
    {
        slots.clear();
        for (const auto& r : regions)
        {
            Chord c;
            c.start = r.start;
            c.length = r.length;
            c.pitches = r.pitches;
            c.bassPc = r.bassPc;
            slots.push_back (voiceChordSlots (c, vp, slots.empty() ? before : &slots.back()));
        }
    };
    voiceAll (nullptr);
    // The loop comes round: voice it again starting from where it ends, so the first chord follows the last one
    // (no leap at the repeat). Deep Note grows its first chord out of a cluster instead.
    if (regions.size() > 1 && p.motion != Motion::deepNote)
    {
        const auto last = slots.back();
        voiceAll (&last);
    }

    // Sub: one more voice an octave under the bass, following it.
    const int bassSlot = p.sub ? 1 : 0;
    if (p.sub)
        for (auto& s : slots)
            s.insert (s.begin(), s.front() - 12 >= 21 ? s.front() - 12 : s.front());

    const int n = (int) slots.front().size();
    Rng rng ((uint64_t) p.seed * 977ull + 5ull);
    std::vector<float> lag ((size_t) n);
    for (int k = 0; k < n; ++k)
        lag[(size_t) k] = p.stagger * ((n > 1 ? (float) k / (float) (n - 1) : 0.0f) + 0.15f * rng.bipolar());

    // Suspensions: per region, up to two upper voices enter a step above their note.
    std::vector<std::vector<int>> sus (regions.size(), std::vector<int> ((size_t) n, 0));
    if (p.reharm == Reharm::suspensions)
    {
        Rng susRng ((uint64_t) p.seed * 6151ull + 29ull);
        for (size_t r = 0; r < regions.size(); ++r)
        {
            int placed = 0;
            for (int k = n - 1; k > bassSlot && placed < 2; --k)
                if (susRng.chance (0.55f))
                    if (const int s = suspensionFor (slots[r][(size_t) k], regions[r].pitches.front(), p.darkness); s != 0)
                    {
                        sus[r][(size_t) k] = s;
                        ++placed;
                    }
        }
    }

    const StepContext steps { p.key, p.scale };
    std::vector<Note> notes;
    const bool morphLike = p.motion == Motion::morph || p.motion == Motion::deepNote || p.motion == Motion::counterline
                        || p.motion == Motion::ripple || p.motion == Motion::shimmer;
    const bool loops = regions.size() > 1 && regions.back().start + regions.back().length >= lengthBeats - 1.0e-6;

    if (morphLike)
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
                addTransition (cur.bend, t0 - cur.start, t1 - cur.start, offset, target, p.shape, cur.pitch, &steps);
                offset = target;
            }

            // The loop comes round: glide into the first chord's note, arriving as the loop restarts.
            const auto& last = regions.back();
            const double end = last.start + last.length;
            if (loops && p.motion != Motion::deepNote)
            {
                const int to = slots.front()[(size_t) k];
                if (to != slots.back()[(size_t) k] && std::abs (to - cur.pitch) <= 24)
                {
                    const double dur = std::max (0.05, (double) p.glide * std::min (last.length, regions.front().length));
                    const double t0 = std::max (end - dur + lag[(size_t) k] * dur * 0.4, last.start + 0.02);
                    addTransition (cur.bend, t0 - cur.start, end - 0.01 - cur.start, offset, (float) (to - cur.pitch), p.shape, cur.pitch, &steps);
                }
            }
            cur.length = end - cur.start;
            notes.push_back (cur);
        }

        if (p.motion == Motion::deepNote)
        {
            // THX-style intro: the first chord grows out of a drifting cluster of detuned voices.
            const auto& first = regions.front();
            // Converged before the first glide to the next chord starts (it may anticipate the change).
            const double conv0 = 0.3 * first.length, conv1 = 0.62 * first.length;
            const int centre = medianPitch (slots.front());
            Rng deep ((uint64_t) p.seed * 3571ull + 101ull);
            for (auto& note : notes)
            {
                if (note.start > first.start + 1.0e-9)
                    continue;
                const float target = evalCurve (note.bend, conv1, 0.0f);
                float v = std::clamp ((float) (centre - note.pitch) + deep.bipolar() * 5.0f, -47.0f, 47.0f);
                Curve c;
                c.push_back ({ 0.0, v });
                for (double w : { 0.08, 0.16, 0.24 })
                {
                    v = std::clamp (v + deep.bipolar() * 1.2f, -47.0f, 47.0f);
                    c.push_back ({ w * first.length, v });
                }
                addTransition (c, conv0, conv1, v, target, GlideShape::ease);
                for (const auto& pt : note.bend)
                    if (pt.t > conv1 + 1.0e-9)
                        c.push_back (pt);
                note.bend = std::move (c);
            }
        }
    }
    else if (p.motion == Motion::pulse)
    {
        const auto pattern = euclid16 (std::clamp ((int) std::lround (4.0f + 12.0f * p.pulse), 1, 16));
        constexpr double stepLen = 0.25;
        const double glideDur = 0.04 + 0.25 * p.glide;

        for (size_t r = 0; r < regions.size(); ++r)
        {
            const auto& reg = regions[r];
            const double end = reg.start + reg.length;
            bool firstHit = true;
            for (double t = std::ceil (reg.start / stepLen - 1.0e-9) * stepLen; t < end - 1.0e-9; t += stepLen)
            {
                const int step = (int) std::lround (t / stepLen) % 16;
                if (! pattern[(size_t) step])
                    continue;
                const double len = std::min (stepLen * 0.55, end - t - 0.01);
                if (len < 0.03)
                    continue;

                for (int k = 0; k < n; ++k)
                {
                    Note hit;
                    hit.voiceIndex = k;
                    hit.start = t;
                    hit.length = len;
                    hit.pitch = slots[r][(size_t) k];
                    hit.accent = step % 4 == 0;
                    hit.bend.push_back ({ 0.0, 0.0f });
                    if (firstHit && (r > 0 || loops))
                    {
                        const auto& before = r > 0 ? slots[r - 1] : slots.back(); // the loop comes round
                        const float from = (float) std::clamp (before[(size_t) k] - hit.pitch, -24, 24);
                        if (std::abs (from) > 0.0f)
                        {
                            hit.bend.front().v = from;
                            addTransition (hit.bend, 0.0, std::min (glideDur, len * 0.9), from, 0.0f, p.shape, hit.pitch, &steps);
                        }
                    }
                    notes.push_back (hit);
                }
                firstHit = false;
            }
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

                if (p.motion == Motion::tensionRise)
                {
                    // The chord stretches: bass down, upper voices up, faster and faster into the change.
                    const float pos = n > 1 ? (float) k / (float) (n - 1) : 1.0f;
                    const float rise = k <= bassSlot ? -(2.0f + 5.0f * p.glide) : (pos - 0.3f) * (3.0f + 9.0f * p.glide);
                    note.bend.push_back ({ 0.0, 0.0f });
                    addTransition (note.bend, reg.length * (0.2 + 0.1 * std::abs (lag[(size_t) k])), reg.length - 0.02,
                                   0.0f, std::clamp (rise, -24.0f, 24.0f), GlideShape::swoopIn);
                    notes.push_back (note);
                    continue;
                }

                const float away = (float) std::clamp (centre - note.pitch, -48, 48);
                const double delay = std::abs (lag[(size_t) k]) * dur * 0.5;

                note.bend.push_back ({ 0.0, in ? away : 0.0f });
                if (in)
                    addTransition (note.bend, delay, std::min (reg.length * 0.95, delay + dur), away, 0.0f, p.shape, note.pitch, &steps);
                if (outward)
                    addTransition (note.bend, std::max (note.bend.back().t, reg.length - dur - delay),
                                   reg.length - 0.01, 0.0f, away, p.shape, note.pitch, &steps);
                notes.push_back (note);
            }
        }
    }

    // ---- per-note expression: suspensions, top-voice vibrato, falls, pressure + timbre swell, phrase arc
    int topPitch = 0;
    for (const auto& s : slots)
        topPitch = std::max (topPitch, *std::max_element (s.begin(), s.end()));

    const bool spanning = morphLike;
    const float fallDepth = p.fall > 0.0f ? 1.0f + 4.0f * p.fall : 0.0f;

    // ---- per-voice motion of Counterline / Ripple / Shimmer, in absolute beats (added to the voices' bends)
    std::vector<Curve> extra ((size_t) n);
    if (p.motion == Motion::counterline && n > 0)
    {
        // The top voice sings: two to four moves by scale step inside every chord, back on its chord tone before
        // the change.
        const int top = n - 1;
        Rng cr ((uint64_t) p.seed * 7507ull + 71ull);
        static const int figureSteps[] = { 1, 2, -1, 1, 3, -2, 2, -1 };
        auto& c = extra[(size_t) top];
        c.push_back ({ 0.0, 0.0f });
        for (size_t r = 0; r < regions.size(); ++r)
        {
            const auto& reg = regions[r];
            if (reg.length < 0.99)
                continue;
            const int base = slots[r][(size_t) top];
            const int moves = std::min (2 + cr.range (0, reg.length >= 3.9 ? 2 : 1), (int) std::floor (reg.length * 2.0));
            const double t0 = reg.start + 0.2 * reg.length, t1 = reg.start + 0.85 * reg.length;
            const double seg = (t1 - t0) / (double) moves;
            float cur = 0.0f;
            int pick = cr.range (0, 7);
            for (int m = 0; m < moves; ++m)
            {
                float target = 0.0f;
                if (m < moves - 1)
                {
                    target = (float) scales::stepOffset (base, p.key, p.scale, figureSteps[(size_t) pick % 8]);
                    pick += 1 + cr.range (0, 2);
                }
                const double a = t0 + seg * m;
                if (std::abs (target - cur) > 1.0e-3f)
                    addTransition (c, a, a + std::min (0.12, seg * 0.35), cur, target, GlideShape::ease);
                cur = target;
            }
        }
    }
    else if (p.motion == Motion::ripple)
    {
        // Every voice dips a scale step and comes back, one after another from the bass up.
        for (int k = 0; k < n; ++k)
        {
            auto& c = extra[(size_t) k];
            c.push_back ({ 0.0, 0.0f });
            const float pos = n > 1 ? (float) k / (float) (n - 1) : 0.0f;
            for (size_t r = 0; r < regions.size(); ++r)
            {
                const auto& reg = regions[r];
                const float dip = (float) scales::stepOffset (slots[r][(size_t) k], p.key, p.scale, -1);
                const double start = reg.start + reg.length * (0.3 + 0.35 * pos);
                const double go = std::min (0.1, reg.length * 0.05), hold = reg.length * 0.08;
                addTransition (c, start, start + go, 0.0f, dip, GlideShape::ease);
                addTransition (c, start + go + hold, start + 2.0 * go + hold, dip, 0.0f, GlideShape::ease);
            }
        }
    }
    else if (p.motion == Motion::shimmer)
    {
        // The voices drift apart (up to a quarter tone at the edges) with their own slow vibrato, and close up
        // again as the chord changes.
        Rng sr ((uint64_t) p.seed * 3001ull + 17ull);
        for (int k = 0; k < n; ++k)
        {
            auto& c = extra[(size_t) k];
            const float pos = n > 1 ? (float) k / (float) (n - 1) * 2.0f - 1.0f : 0.0f;
            const double rate = 1.1 + 0.8 * sr.uniform(), phase = sr.uniform();
            const float spread = (0.1f + 0.25f * p.swell) * pos;
            const float vib = 0.05f + 0.08f * p.swell;
            for (double t = 0.0; t <= lengthBeats + 1.0e-9; t += 1.0 / 16.0)
            {
                const auto& reg = regions[regionAt (regions, t)];
                const float x = (float) ((t - reg.start) / std::max (0.25, reg.length));
                const float env = smoothstep (0.0f, 0.6f, x) * (1.0f - smoothstep (0.85f, 1.0f, x));
                c.push_back ({ t, env * (spread + vib * (float) std::sin (6.283185307 * (rate * t + phase))) });
            }
        }
    }
    const double phraseLen = std::max (1.0, lengthBeats);

    // Suspension offset of voice k at absolute time `abs` (resolves between 35% and 60% of the chord).
    auto suspension = [&] (int k, double abs, bool noteSpansChords)
    {
        const size_t ri = regionAt (regions, abs);
        const auto& reg = regions[ri];
        float v = 0.0f;
        if (const int s = sus[ri][(size_t) k]; s != 0)
        {
            const float x = (float) ((abs - (reg.start + 0.35 * reg.length)) / (0.25 * reg.length));
            v += (float) s * (1.0f - shapeCurve (GlideShape::ease, x));
        }
        // A long note bends into the next chord's suspension together with its glide.
        if (noteSpansChords && ri + 1 < regions.size())
            if (const int s = sus[ri + 1][(size_t) k]; s != 0)
            {
                const double ramp = std::min (0.5, 0.2 * regions[ri + 1].length);
                v += (float) s * smoothstep ((float) (regions[ri + 1].start - ramp), (float) regions[ri + 1].start, (float) abs);
            }
        return v;
    };

    for (auto& note : notes)
    {
        const int k = note.voiceIndex;
        const float voicePos = n > 1 ? (float) k / (float) (n - 1) : 0.5f;
        const bool isTop = note.pitch >= topPitch - 7 && voicePos > 0.7f;
        const float reg = std::clamp ((float) (note.pitch - 36) / 60.0f, 0.0f, 1.0f);
        const double len = std::max (0.05, note.length);
        const size_t firstRegion = regionAt (regions, note.start);
        const double regionEnd = regions[firstRegion].start + regions[firstRegion].length;

        // Falls: at the end of a chord (for Pulse only its last hit), never on the bass / sub.
        const bool falls = fallDepth > 0.0f && k > bassSlot
                        && (p.motion != Motion::pulse || note.end() > regionEnd - 0.26);
        const double fallLen = std::min (0.5, 0.18 * len);

        auto times = sampleTimes (note.bend, len);
        const auto& voiceMotion = extra[(size_t) std::clamp (k, 0, n - 1)];
        if (! voiceMotion.empty())
        {
            for (const auto& pt : voiceMotion)
                if (pt.t > note.start && pt.t < note.start + len)
                    times.push_back (pt.t - note.start);
            std::sort (times.begin(), times.end());
            times.erase (std::unique (times.begin(), times.end(), [] (double a, double b) { return b - a < 1.0e-6; }), times.end());
        }

        Curve bend, slide, pressure;
        for (const double t : times)
        {
            const double abs = note.start + t;
            const auto& region = regions[regionAt (regions, abs)];
            const float x = (float) ((abs - region.start) / std::max (0.25, region.length));
            const float arcGain = 1.0f - p.arc * 0.55f * (1.0f - smoothstep (0.0f, 1.0f, (float) (abs / phraseLen)));
            const float fx = falls ? (float) std::clamp ((t - (len - fallLen)) / fallLen, 0.0, 1.0) : 0.0f;

            float b = evalCurve (note.bend, t, 0.0f) + suspension (k, abs, spanning && note.length > region.length + 1.0e-6)
                    + evalCurve (voiceMotion, abs, 0.0f);
            if (isTop && x > 0.35f && region.length >= 2.0)
            {
                const float depth = (0.05f + 0.12f * p.swell) * (0.7f + 0.6f * p.arc * (float) (abs / phraseLen));
                b += depth * smoothstep (0.35f, 0.7f, x) * (float) std::sin (6.283185307 * 1.2 * (abs - region.start));
            }
            b -= fallDepth * fx * fx;
            bend.push_back ({ t, std::clamp (b, -48.0f, 48.0f) });

            float press;
            if (p.motion == Motion::pulse)
                press = (note.accent ? 0.75f : 0.5f) * (1.0f - 0.6f * (float) (t / len)) * (0.55f + 0.45f * p.swell * smoothstep (0.0f, 1.0f, x));
            else if (p.motion == Motion::tensionRise)
                press = 0.2f + p.swell * 0.75f * smoothstep (0.0f, 1.0f, x);
            else
                press = 0.18f + p.swell * 0.7f * smoothstep (0.0f, 0.75f, x) - p.swell * 0.2f * smoothstep (0.92f, 1.0f, x);
            press = press * arcGain * (1.0f - 0.5f * fx);
            pressure.push_back ({ t, std::clamp (press, 0.04f, 1.0f) });

            const float riseOpen = p.motion == Motion::tensionRise ? 0.25f * x * x : 0.0f;
            const float open = p.swell * 0.55f * smoothstep (0.05f + 0.3f * voicePos * p.stagger, 1.0f, x) + riseOpen;
            slide.push_back ({ t, std::clamp ((0.15f + 0.3f * reg + open) * (0.6f + 0.4f * arcGain), 0.0f, 1.0f) });
        }

        note.bend = std::move (bend);
        note.slide = std::move (slide);
        note.pressure = std::move (pressure);
        note.lockedExpr = true;
        note.voiceCount = n;
        note.velocity = std::clamp (0.62f + 0.2f * (1.0f - voicePos) + (note.accent ? 0.12f : 0.0f) + 0.05f * rng.bipolar(), 0.1f, 1.0f);
        note.releaseVelocity = 0.3f;
    }

    out.notes = std::move (notes);
    out.sortByStart();
    return out;
}

} // namespace dmpe
