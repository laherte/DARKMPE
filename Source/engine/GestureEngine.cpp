#include "GestureEngine.h"
#include "CurveShapes.h"
#include "PhraseForm.h"
#include "Rng.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace dmpe
{
namespace
{

constexpr double twoPi = 6.283185307179586;
constexpr size_t numGestures = (size_t) Gesture::count;
constexpr size_t numArticulations = (size_t) Articulation::count;

struct ProfileDef
{
    float density;                                  // pitch-gesture probability scale
    float riff;                                     // Bend Riff probability scale
    std::array<float, numGestures> weights;         // indexed by Gesture
    float articulationDensity;
    std::array<float, numArticulations> articulations; // indexed by Articulation
    float vibrato;                                  // vibrato scale on long notes
};

//                              none dip  lift scoop fall appr over step trill wob  dive riff rip
const ProfileDef& profileDef (GestureProfile p)
{
    static const ProfileDef liquid     { 0.8f,  1.0f, { 0, 2.0f, 2.0f, 2.0f, 1.0f, 3.0f, 1.0f, 3.0f, 0.0f, 0.5f, 0.0f, 0, 1.0f },
                                         0.6f, { 0, 0.5f, 2.0f, 0.0f, 0.0f, 1.0f }, 1.0f };
    static const ProfileDef vocal      { 0.7f,  0.4f, { 0, 1.0f, 1.0f, 3.0f, 3.0f, 1.5f, 1.5f, 0.5f, 0.3f, 0.0f, 0.0f, 0, 0.5f },
                                         0.5f, { 0, 0.0f, 0.5f, 0.0f, 0.0f, 3.0f }, 1.4f };
    static const ProfileDef acid       { 0.8f,  0.7f, { 0, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f, 3.0f, 1.0f, 0.5f, 1.0f, 0.3f, 0, 2.0f },
                                         0.9f, { 0, 2.0f, 1.0f, 3.0f, 0.0f, 0.0f }, 0.8f };
    static const ProfileDef aggressive { 0.75f, 0.5f, { 0, 1.5f, 0.5f, 1.0f, 3.0f, 1.0f, 2.0f, 0.3f, 0.3f, 1.0f, 2.0f, 0, 1.5f },
                                         0.7f, { 0, 3.0f, 0.0f, 1.0f, 0.0f, 0.0f }, 1.0f };
    static const ProfileDef glitch     { 0.9f,  0.8f, { 0, 1.0f, 1.0f, 0.3f, 0.5f, 0.3f, 0.5f, 0.5f, 3.0f, 2.0f, 0.5f, 0, 2.0f },
                                         0.9f, { 0, 1.0f, 0.0f, 2.0f, 3.0f, 0.0f }, 0.5f };
    switch (p)
    {
        case GestureProfile::liquid:     return liquid;
        case GestureProfile::vocal:      return vocal;
        case GestureProfile::acid:       return acid;
        case GestureProfile::glitch:     return glitch;
        case GestureProfile::aggressive:
        case GestureProfile::classic:
        case GestureProfile::autoStyle:
        case GestureProfile::count:      break;
    }
    return aggressive;
}

// Which gestures suit a role (multiplies the profile's weights) and how often it riffs.
struct RoleDef
{
    std::array<float, numGestures> weights;
    float riff, density;
};

const RoleDef& roleDef (GestureRole r)
{
    //                         none dip   lift scoop fall appr over  step trill wob  dive riff rip
    static const RoleDef lead { { 0, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.5f, 0, 0 }, 1.0f, 1.0f };
    static const RoleDef bass { { 0, 0.4f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.6f, 3.0f, 0, 0 }, 0.6f, 0.6f };
    static const RoleDef arp  { { 0, 1.0f, 1.0f, 1.0f, 0.5f, 1.0f, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f, 0, 0 }, 1.2f, 0.7f };
    switch (r)
    {
        case GestureRole::bass:  return bass;
        case GestureRole::arp:   return arp;
        case GestureRole::lead:
        case GestureRole::chord: break;
    }
    return lead;
}

uint64_t seedOf (const Note& n, const GestureParams& p, uint64_t salt)
{
    if (n.exprSeed != 0)
        return mixSeed (n.exprSeed, salt);
    return mixSeed (mixSeed ((uint64_t) p.seed, salt), (uint64_t) std::llround (n.start * 960.0) * 131ull + (uint64_t) n.pitch);
}

// Semitones to the note `steps` scale steps away in direction `dir` (a chromatic note counts from its neighbours).
int scaleNeighbour (int pitch, int dir, int steps, const GestureParams& p)
{
    return scales::stepOffset (pitch, p.key, p.scale, dir * steps);
}

bool isAnswer (const Note& n)
{
    return n.sectionKind == (int) SectionKind::answer || n.sectionKind == (int) SectionKind::closedAnswer;
}

void startAt (Curve& c, float v)
{
    c.clear();
    c.push_back ({ 0.0, v });
}

// Appends a point, never before the last one (curves stay time-ordered).
void push (Curve& c, double t, float v)
{
    c.push_back ({ c.empty() ? std::max (0.0, t) : std::max (t, c.back().t + 1.0e-4), v });
}

// ------------------------------------------------------------------ Bend Riffs
// Short runs (2..4 notes inside one beat of one bar, nearly legato, within the Depth) become one note that bends
// through their pitches: the attacks survive as timbre flicks and pressure re-articulations.
void mergeRiffs (Phrase& phrase, const GestureParams& p, float chance, float maxSemis, bool liquid)
{
    auto& notes = phrase.notes;
    std::vector<Note> out;
    out.reserve (notes.size());

    size_t i = 0;
    while (i < notes.size())
    {
        const Note& first = notes[i];
        const int bar = (int) std::floor (first.start / 4.0 + 1.0e-9);
        size_t j = i + 1;
        while (j < notes.size() && j - i < 4)
        {
            const Note& prev = notes[j - 1];
            const Note& cand = notes[j];
            if ((int) std::floor (cand.start / 4.0 + 1.0e-9) != bar || cand.start - first.start >= 1.0 - 1.0e-9
                || cand.start - prev.end() > 0.13 || cand.start < prev.start + 1.0e-6 || cand.hasSourceExpr
                || std::abs (cand.pitch - first.pitch) > maxSemis)
                break;
            ++j;
        }

        Rng rng (seedOf (first, p, 0x51f0ull));
        const size_t available = j - i;
        if (available < 2 || first.hasSourceExpr || ! rng.chance (chance))
        {
            out.push_back (first);
            ++i;
            continue;
        }

        const size_t count = 2 + (size_t) rng.range (0, (int) available - 2);
        const Note& last = notes[i + count - 1];
        Note m = first;
        m.length = last.end() - first.start;
        m.gestureKind = (int) Gesture::riff;
        m.vibrato = 0.0f;
        startAt (m.gesture, 0.0f);
        startAt (m.gestureTimbre, 0.0f);
        startAt (m.gesturePress, 1.0f);

        float cur = 0.0f;
        double prevOnset = 0.0;
        for (size_t k = 1; k < count; ++k)
        {
            const Note& nk = notes[i + k];
            const double tk = nk.start - first.start;
            const float target = (float) (nk.pitch - first.pitch);
            const double slew = std::clamp ((liquid ? 0.55 : 0.3) * (tk - prevOnset), 0.012, liquid ? 0.12 : 0.06);
            const double t0 = std::max (m.gesture.back().t, tk - slew * 0.4);
            const double t1 = std::min (m.length, tk + slew * 0.6);
            if (std::abs (target - cur) > 1.0e-3f)
                addTransition (m.gesture, t0, t1, cur, target, GlideShape::ease);
            cur = target;

            // Re-articulate: a pressure dip into the new attack, a flick of timbre.
            push (m.gesturePress, tk - 0.02, 1.0f);
            push (m.gesturePress, tk + 0.004, 0.6f);
            push (m.gesturePress, std::min (m.length, tk + 0.05), 1.05f);
            push (m.gestureTimbre, tk, 0.0f);
            push (m.gestureTimbre, tk + 0.01, 0.18f);
            push (m.gestureTimbre, std::min (m.length, tk + 0.1), 0.0f);

            m.accent = m.accent || nk.accent;
            m.chromatic = m.chromatic || nk.chromatic;
            prevOnset = tk;
        }
        out.push_back (m);
        i += count;
    }
    notes = std::move (out);
}

// ------------------------------------------------------------------ pitch gestures
Gesture pickGesture (Rng& rng, const Note& n, const Note* next, const ProfileDef& def, const RoleDef& role)
{
    const double len = n.length;
    const bool glide = n.glideFrom >= 0;
    // In a phrase form a pedal note follows the chord, so its pitch changes between repeats of a section: the
    // gesture never depends on it (the repeat stays identical).
    const bool nextKnown = next != nullptr && ! (n.sectionKind >= 0 && next->pedal);
    const bool legatoNext = nextKnown && next->start - n.end() <= 0.15 && next->pitch != n.pitch
                         && std::abs (next->pitch - n.pitch) <= 12 && next->glideFrom < 0;

    std::array<float, numGestures> w {};
    auto allow = [&] (Gesture g, bool ok) { w[(size_t) g] = ok ? def.weights[(size_t) g] * role.weights[(size_t) g] : 0.0f; };
    allow (Gesture::dip, len >= 0.2);
    allow (Gesture::lift, len >= 0.2);
    allow (Gesture::scoop, len >= 0.08 && ! glide);
    allow (Gesture::fall, len >= 0.12 && ! legatoNext);
    allow (Gesture::approach, len >= 0.12 && legatoNext);
    allow (Gesture::overshoot, len >= 0.15 && glide);
    allow (Gesture::stepGlide, len >= 0.2 && glide);
    allow (Gesture::trill, len >= 0.25);
    allow (Gesture::wobble, len >= 0.3);
    allow (Gesture::dive, len >= 0.2 && ! legatoNext);

    float total = 0.0f;
    for (float x : w)
        total += x;
    if (total <= 0.0f)
        return Gesture::none;
    float r = rng.uniform() * total;
    for (size_t g = 0; g < numGestures; ++g)
    {
        r -= w[g];
        if (r <= 0.0f && w[g] > 0.0f)
            return (Gesture) g;
    }
    return Gesture::none;
}

void writeGesture (Note& n, Gesture g, Rng& rng, const Note* next, const GestureParams& p, GestureRole role, float maxSemis)
{
    const double len = n.length;
    const float mirror = isAnswer (n) ? -1.0f : 1.0f; // answers turn the call's gestures round
    const StepContext steps { p.key, p.scale };
    n.gestureKind = (int) g;

    switch (g)
    {
        case Gesture::dip:
        case Gesture::lift:
        {
            const int dir = (g == Gesture::dip ? -1 : 1) * (int) mirror;
            const int stepCount = p.depth > 0.5f && rng.chance (0.4f) ? 2 : 1;
            const float semis = std::clamp ((float) scaleNeighbour (n.pitch, dir, stepCount, p), -maxSemis, maxSemis);
            const double ts = len * (0.2 + 0.2 * rng.uniform());
            const double go = std::min (0.08, len * 0.18);
            const double hold = len * (0.12 + 0.18 * rng.uniform());
            startAt (n.gesture, 0.0f);
            addTransition (n.gesture, ts, ts + go, 0.0f, semis, GlideShape::ease);
            addTransition (n.gesture, ts + go + hold, std::min (len * 0.95, ts + 2.0 * go + hold), semis, 0.0f, GlideShape::ease);
            n.vibrato = 0.0f;
            break;
        }

        case Gesture::scoop:
        {
            const float size = std::min (maxSemis, p.depth > 0.6f ? 3.0f : (rng.chance (0.5f) ? 1.0f : 2.0f));
            const double d = std::min (0.12, len * 0.4);
            startAt (n.gesture, -mirror * size);
            addTransition (n.gesture, 0.0, d, -mirror * size, 0.0f, GlideShape::swoopOut);
            break;
        }

        case Gesture::fall:
        {
            int stepCount = std::clamp (1 + (int) std::lround (p.depth * 4.0f) + rng.range (-1, 1), 1, 6);
            float semis = (float) scaleNeighbour (n.pitch, -1, stepCount, p);
            if (p.depth >= 0.9f && rng.chance (0.3f))
                semis = -12.0f;
            semis = std::max (semis, -maxSemis);
            const double tf = len * (0.5 + 0.25 * rng.uniform());
            startAt (n.gesture, 0.0f);
            addTransition (n.gesture, tf, len, 0.0f, semis, GlideShape::swoopIn);
            startAt (n.gesturePress, 1.0f);
            n.gesturePress.push_back ({ tf, 1.0f });
            n.gesturePress.push_back ({ len, 0.45f });
            break;
        }

        case Gesture::approach:
        {
            // Lean part of the way to the next note (a partial glide): the next note then arrives on its own attack.
            const float interval = (float) (next->pitch - n.pitch);
            const float target = std::clamp (interval * (0.35f + 0.3f * rng.uniform()), -maxSemis, maxSemis);
            const double ta = len * (0.45 + 0.15 * rng.uniform());
            startAt (n.gesture, 0.0f);
            addTransition (n.gesture, ta, len, 0.0f, target, GlideShape::ease);
            break;
        }

        case Gesture::overshoot:
        {
            const float from = (float) std::clamp (n.glideFrom - n.pitch, -48, 48);
            const float over = (from < 0.0f ? 1.0f : -1.0f) * std::min (maxSemis, 0.5f + 1.5f * p.depth);
            const double g0 = std::min ((double) p.glideTime, len * 0.45);
            startAt (n.gesture, from);
            addTransition (n.gesture, 0.0, g0 * 0.75, from, over, GlideShape::swoopOut);
            addTransition (n.gesture, g0 * 0.75, std::min (len * 0.9, g0 * 1.7), over, 0.0f, GlideShape::ease);
            n.glideAuthored = true;
            break;
        }

        case Gesture::stepGlide:
        {
            const float from = (float) std::clamp (n.glideFrom - n.pitch, -48, 48);
            startAt (n.gesture, from);
            addTransition (n.gesture, 0.0, std::min (len * 0.7, 2.0 * p.glideTime + 0.05), from, 0.0f, GlideShape::stepped, n.pitch, &steps);
            n.glideAuthored = true;
            break;
        }

        case Gesture::trill:
        {
            const int dir = p.profile == GestureProfile::glitch && rng.chance (0.5f) ? -1 : (int) mirror;
            const float semis = std::clamp ((float) scaleNeighbour (n.pitch, dir, 1, p), -maxSemis, maxSemis);
            const double rate = p.profile == GestureProfile::glitch || len < 0.5 ? 0.125 : 0.25; // 1/32 or 1/16
            const double slew = 0.012;
            startAt (n.gesture, 0.0f);
            bool up = false;
            for (double t = std::max (0.06, len * 0.12); t + rate <= len * 0.94; t += rate)
            {
                const float from = up ? semis : 0.0f;
                up = ! up;
                addTransition (n.gesture, t, t + slew, from, up ? semis : 0.0f, GlideShape::linear);
            }
            if (up)
                addTransition (n.gesture, n.gesture.back().t + rate * 0.5, std::min (len, n.gesture.back().t + rate * 0.5 + slew), semis, 0.0f, GlideShape::linear);
            n.vibrato = 0.0f;
            break;
        }

        case Gesture::wobble:
        {
            static const double rates[] = { 2.0, 3.0, 4.0 }; // cycles per beat: 8ths, 8th triplets, 16ths
            const double rate = rates[rng.range (0, 2)];
            const float depth = std::min (maxSemis, 0.3f + 1.2f * p.depth);
            const double t0 = len * 0.1;
            startAt (n.gesture, 0.0f);
            startAt (n.gestureTimbre, 0.0f);
            for (double t = t0; t <= len + 1.0e-9; t += 1.0 / (16.0 * rate))
            {
                const double ph = rate * (t - t0);
                const float fade = (float) std::min (1.0, (t - t0) / 0.15);
                n.gesture.push_back ({ t, -depth * fade * (float) std::sin (twoPi * ph) });
                n.gestureTimbre.push_back ({ t, 0.2f * fade * (float) std::sin (twoPi * ph - 1.2) });
            }
            n.vibrato = 0.0f;
            break;
        }

        case Gesture::dive:
        {
            // The bass dives by an octave or a fifth when Depth allows it, anything else by the full Depth.
            const float semis = role == GestureRole::bass && maxSemis >= 7.0f ? (maxSemis >= 12.0f ? -12.0f : -7.0f)
                                                                                : -std::min (12.0f, maxSemis);
            const double td = len * (0.25 + 0.2 * rng.uniform());
            startAt (n.gesture, 0.0f);
            addTransition (n.gesture, td, len, 0.0f, std::max (semis, -12.0f), GlideShape::swoopIn);
            startAt (n.gesturePress, 1.0f);
            n.gesturePress.push_back ({ td, 1.1f });
            n.gesturePress.push_back ({ len, 0.5f });
            break;
        }

        case Gesture::none:
        case Gesture::riff:
        case Gesture::rip:
        case Gesture::count:
            n.gestureKind = 0;
            break;
    }
}

// ------------------------------------------------------------------ timbre / pressure articulations
void writeArticulation (Note& n, Articulation a, const GestureParams& p)
{
    const double len = n.length;
    const bool timbreFree = n.gestureTimbre.empty();
    const bool pressFree = n.gesturePress.empty();

    switch (a)
    {
        case Articulation::pluck:
            if (! timbreFree) return;
            n.gestureTimbre = { { 0.0, 0.35f }, { std::min (len, 0.03), 0.2f }, { std::min (len, 0.07), 0.08f }, { std::min (len, 0.12), 0.0f } };
            break;

        case Articulation::wah:
            if (! timbreFree) return;
            // One sweep per beat, locked to the bar (absolute time), so every note joins the same wah.
            for (double t = 0.0; t <= len + 1.0e-9; t += 1.0 / 32.0)
                n.gestureTimbre.push_back ({ t, 0.22f * (float) std::sin (twoPi * (n.start + t)) });
            break;

        case Articulation::steps:
        {
            if (! timbreFree) return;
            // A 16-step timbre sequence for the whole phrase (same seed, same pattern), stepped at every 16th.
            Rng seq (mixSeed ((uint64_t) p.seed, 0x57e9ull));
            std::array<float, 16> values {};
            for (auto& v : values)
                v = -0.25f + 0.55f * seq.uniform();
            for (double t = 0.0; t < len - 1.0e-9;)
            {
                const auto step = (size_t) scales::mod ((int) std::floor ((n.start + t) * 4.0 + 1.0e-6), 16);
                const double next = std::min (len, (std::floor ((n.start + t) * 4.0 + 1.0e-6) + 1.0) / 4.0 - n.start);
                push (n.gestureTimbre, t, values[step]);
                push (n.gestureTimbre, next - 0.004, values[step]);
                t = std::max (next, t + 1.0e-3);
            }
            break;
        }

        case Articulation::tremolo:
        {
            if (! pressFree) return;
            // Pressure chopped in 32nds (a ratchet on a held note).
            bool open = true;
            for (double t = 0.0; t < len - 1.0e-9; t += 0.125)
            {
                push (n.gesturePress, t, open ? 1.0f : 0.3f);
                push (n.gesturePress, std::min (len, t + 0.12), open ? 1.0f : 0.3f);
                open = ! open;
            }
            break;
        }

        case Articulation::swell:
            if (! pressFree) return;
            n.gesturePress = { { 0.0, 0.7f }, { len * 0.7, 1.25f }, { len, 1.0f } };
            break;

        case Articulation::none:
        case Articulation::count:
            return;
    }
    n.articulation = (int) a;
}

Articulation pickArticulation (Rng& rng, const ProfileDef& def, double len)
{
    std::array<float, numArticulations> w = def.articulations;
    if (len < 0.25)
    {
        w[(size_t) Articulation::tremolo] = 0.0f;
        w[(size_t) Articulation::swell] = 0.0f;
    }
    float total = 0.0f;
    for (float x : w)
        total += x;
    if (total <= 0.0f)
        return Articulation::none;
    float r = rng.uniform() * total;
    for (size_t a = 0; a < numArticulations; ++a)
    {
        r -= w[a];
        if (r <= 0.0f && w[a] > 0.0f)
            return (Articulation) a;
    }
    return Articulation::none;
}

// ------------------------------------------------------------------ chords
// Every voice of a chord does the same gesture, one after another: scoop in, fall off, dive, or rip up into it.
void chordGestures (Phrase& phrase, const GestureParams& p, const ProfileDef& def, float maxSemis)
{
    auto& notes = phrase.notes;
    for (size_t i = 0; i < notes.size();)
    {
        size_t j = i + 1;
        while (j < notes.size() && std::abs (notes[j].start - notes[i].start) < 1.0e-6)
            ++j;

        Rng rng (mixSeed (mixSeed ((uint64_t) p.seed, 0xc40dull), (uint64_t) std::llround (notes[i].start * 960.0)));
        if (! notes[i].hasSourceExpr && rng.chance (p.amount * def.density))
        {
            const float w[] = { def.weights[(size_t) Gesture::scoop], def.weights[(size_t) Gesture::fall],
                                def.weights[(size_t) Gesture::dive], def.weights[(size_t) Gesture::rip] };
            const Gesture kinds[] = { Gesture::scoop, Gesture::fall, Gesture::dive, Gesture::rip };
            float total = w[0] + w[1] + w[2] + w[3], r = rng.uniform() * total;
            Gesture g = Gesture::scoop;
            for (int k = 0; k < 4; ++k)
                if ((r -= w[k]) <= 0.0f) { g = kinds[k]; break; }

            const float size = std::min (maxSemis, 1.0f + 4.0f * p.depth);
            for (size_t k = i; k < j; ++k)
            {
                auto& n = notes[k];
                const double len = n.length;
                const double lag = 0.012 * n.voiceIndex;
                startAt (n.gesture, 0.0f);
                switch (g)
                {
                    case Gesture::scoop:
                        n.gesture.front().v = -std::min (size, 2.0f);
                        n.gesture.push_back ({ std::min (len * 0.5, lag), -std::min (size, 2.0f) });
                        addTransition (n.gesture, std::min (len * 0.5, lag), std::min (len * 0.8, lag + 0.08), -std::min (size, 2.0f), 0.0f, GlideShape::swoopOut);
                        break;
                    case Gesture::rip:
                        n.gesture.front().v = -size;
                        n.gesture.push_back ({ std::min (len * 0.5, lag * 2.0), -size });
                        addTransition (n.gesture, std::min (len * 0.5, lag * 2.0), std::min (len * 0.85, lag * 2.0 + 0.1), -size, 0.0f, GlideShape::swoopIn);
                        break;
                    case Gesture::fall:
                        addTransition (n.gesture, len * 0.45 + lag, len, 0.0f, -size, GlideShape::swoopIn);
                        break;
                    case Gesture::dive:
                    case Gesture::none:
                    case Gesture::dip:
                    case Gesture::lift:
                    case Gesture::approach:
                    case Gesture::overshoot:
                    case Gesture::stepGlide:
                    case Gesture::trill:
                    case Gesture::wobble:
                    case Gesture::riff:
                    case Gesture::count:
                        addTransition (n.gesture, len * 0.3 + lag, len, 0.0f, -std::min (12.0f, size * 2.0f), GlideShape::swoopIn);
                        break;
                }
                n.gestureKind = (int) g;
            }
        }
        i = j;
    }
}

} // namespace

GestureProfile resolveProfile (GestureProfile p, Style s)
{
    if (p != GestureProfile::autoStyle)
        return p;
    switch (s)
    {
        case Style::opr:
        case Style::raveStab:   return GestureProfile::glitch;
        case Style::darkArp:    return GestureProfile::liquid;
        case Style::acidSlide:  return GestureProfile::acid;
        case Style::pursuit:
        case Style::hateOrGlory:
        case Style::gallop:
        case Style::count:      break;
    }
    return GestureProfile::aggressive;
}

float gestureDepthSemitones (float depth)
{
    return 1.0f + 11.0f * std::clamp (depth, 0.0f, 1.0f);
}

void applyGestures (Phrase& phrase, const GestureParams& input, GestureRole role)
{
    GestureParams p = input;
    p.profile = resolveProfile (p.profile, p.style);
    if (p.profile == GestureProfile::classic || phrase.notes.empty())
        return;

    const auto& def = profileDef (p.profile);
    const float maxSemis = gestureDepthSemitones (p.depth);
    phrase.sortByStart();

    if (role == GestureRole::chord)
    {
        chordGestures (phrase, p, def, maxSemis);
        return;
    }

    const auto& rd = roleDef (role);
    const float riffChance = std::clamp (p.riff * def.riff * rd.riff, 0.0f, 1.0f);
    if (riffChance > 0.0f)
        mergeRiffs (phrase, p, riffChance, role == GestureRole::bass ? std::max (maxSemis, 12.0f) : maxSemis,
                    p.profile == GestureProfile::liquid);

    auto& notes = phrase.notes;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        auto& n = notes[i];
        if (n.hasSourceExpr || n.lockedExpr)
            continue;
        const Note* next = i + 1 < notes.size() ? &notes[i + 1] : nullptr;
        Rng rng (seedOf (n, p, 0x6e57ull));

        // Long notes sing with the profile's vibrato; the held close of a form sings more.
        const bool heldClose = n.sectionKind == (int) SectionKind::close && n.length >= 0.45
                            && (next == nullptr || (int) std::floor (next->start / 4.0 + 1.0e-9) != (int) std::floor (n.start / 4.0 + 1.0e-9));
        if (n.length >= 0.45 && n.gestureKind == 0)
            n.vibrato = def.vibrato * (heldClose ? 1.4f : 1.0f);

        if (n.gestureKind == 0 && rng.chance (std::clamp (p.amount * def.density * rd.density, 0.0f, 1.0f)))
        {
            const auto g = pickGesture (rng, n, next, def, rd);
            if (g != Gesture::none)
                writeGesture (n, g, rng, next, p, role, maxSemis);
        }

        Rng art (seedOf (n, p, 0xa271ull));
        if (art.chance (std::clamp (p.amount * def.articulationDensity, 0.0f, 1.0f)))
            writeArticulation (n, pickArticulation (art, def, n.length), p);
    }
}

} // namespace dmpe
