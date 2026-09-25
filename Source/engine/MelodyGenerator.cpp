#include "MelodyGenerator.h"
#include "Rng.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace dmpe
{
namespace
{

struct StyleDef
{
    std::vector<int> progression; // scale-degree roots, one per bar
    int minPulses, maxPulses;     // onsets per 16-step bar
    float pedalBias, octaveBias, slideBias, gateBias;
    bool upperVoice;              // moving voice sits an octave above the pedal
    bool arp;
    bool octaveOnOffbeats;
    std::vector<int> pool;        // candidate offsets in scale steps from the chord root
    std::vector<int> weights;
    std::vector<int> fixedRhythm; // steps of a fixed rhythm (instead of a euclidean one)
};

const StyleDef& styleDef (Style s)
{
    static const StyleDef defs[] = {
        // pursuit: i i VI VII, pedal on the root with a voice that walks above it
        { { 0, 0, 5, 6 }, 11, 16, 0.2f, -0.1f, 0.0f, -0.1f, true, false, false,
          { 2, 4, 1, 3, 6, 7, 0 }, { 4, 3, 2, 1, 1, 2, 1 } },
        // hate or glory: i VI iv v, octave-jumping riff
        { { 0, 5, 3, 4 }, 6, 11, -0.1f, 0.35f, 0.0f, 0.0f, false, false, true,
          { 0, 2, 4, 7, 1 }, { 4, 2, 3, 2, 1 } },
        // opr: i bII (phrygian), short stabs
        { { 0, 1, 0, 1 }, 5, 9, 0.1f, 0.05f, -0.2f, -0.35f, false, false, false,
          { 0, 1, 2, 4, -1, -3 }, { 3, 3, 2, 2, 1, 1 } },
        // dark arp: i VI VII v
        { { 0, 5, 6, 4 }, 12, 16, -0.3f, 0.0f, -0.1f, 0.0f, false, true, false,
          { 0, 2, 4, 7 }, { 1, 1, 1, 1 } },
        // acid slide: syncopated, glides everywhere
        { { 0, 0, 5, 1 }, 8, 12, 0.0f, 0.1f, 0.35f, 0.15f, false, false, false,
          { 0, 1, 2, 4, 6, 7, -2 }, { 3, 2, 2, 2, 1, 2, 1 } },
        // gallop: i i bVI bVII, 8th + two 16ths on every beat, mostly the root
        { { 0, 0, 5, 6 }, 12, 12, 0.3f, 0.1f, -0.1f, -0.2f, false, false, false,
          { 0, 4, 7, 2, -2 }, { 6, 2, 2, 1, 1 }, { 0, 2, 3, 4, 6, 7, 8, 10, 11, 12, 14, 15 } },
        // rave stab: i VI iv v, sparse syncopated hits, octaves on the offbeats
        { { 0, 5, 3, 4 }, 5, 8, -0.1f, 0.3f, -0.15f, -0.35f, false, false, true,
          { 0, 4, 7, 2, 1 }, { 4, 3, 2, 1, 1 } },
    };
    return defs[(size_t) s];
}

std::vector<bool> euclid (int pulses, int steps, int rotation)
{
    std::vector<bool> pat ((size_t) steps, false);
    for (int i = 0; i < steps; ++i)
        pat[(size_t) i] = ((i * pulses) % steps) < pulses;

    std::vector<bool> out ((size_t) steps);
    for (int i = 0; i < steps; ++i)
        out[(size_t) ((i + rotation) % steps)] = pat[(size_t) i];
    return out;
}

int pickWeighted (Rng& rng, const std::vector<int>& pool, const std::vector<int>& weights, int previous)
{
    // Favour small moves from the previous offset so the line stays singable.
    float total = 0.0f;
    std::vector<float> w (pool.size());
    for (size_t i = 0; i < pool.size(); ++i)
    {
        const float closeness = 1.0f / (1.0f + 0.35f * (float) std::abs (pool[i] - previous));
        w[i] = (float) weights[i] * (0.4f + closeness);
        total += w[i];
    }

    float r = rng.uniform() * total;
    for (size_t i = 0; i < pool.size(); ++i)
    {
        r -= w[i];
        if (r <= 0.0f)
            return pool[i];
    }
    return pool.back();
}

struct MotifEvent
{
    int step;       // 0..15
    int offset;     // scale steps from chord root
    int octave;     // extra octaves
    bool pedal;
    bool accent;
    float jitter = 0.0f; // velocity variation fixed per event (forms: repeated sections stay identical)
};

uint64_t labelHash (const std::string& label)
{
    uint64_t h = 1469598103934665603ull;
    for (char c : label)
        h = (h ^ (uint64_t) (unsigned char) c) * 1099511628211ull;
    return h;
}

// A section of a phrase form built from the basic idea A. Offsets are scale steps from the chord root; pedal
// events stay on the root in every section (they carry the style). `tail` marks the note held to the next one.
std::vector<MotifEvent> sectionMotif (const std::vector<MotifEvent>& a, const Section& s, const StyleDef& def, Rng& rng,
                                      std::vector<bool>& tail)
{
    auto m = a;
    auto lastMovable = [&m]
    {
        for (int i = (int) m.size() - 1; i >= 0; --i)
            if (! m[(size_t) i].pedal)
                return i;
        return -1;
    };

    switch (s.kind)
    {
        case SectionKind::statement:
            for (auto& e : m)
                if (! e.pedal)
                    e.offset += s.shift;
            break;

        case SectionKind::answer:
        case SectionKind::closedAnswer:
        {
            // Same rhythm and opening; the second half mirrors the call's contour around where it had got to,
            // and lands open (the fifth: a question) or closed (the root).
            int pivot = 0;
            for (const auto& e : m)
                if (e.step < 8 && ! e.pedal)
                    pivot = e.offset;
            for (auto& e : m)
                if (e.step >= 8 && ! e.pedal)
                {
                    e.offset = std::clamp (2 * pivot - e.offset, -3, 9);
                    if (rng.chance (0.3f))
                        e.offset = pickWeighted (rng, def.pool, def.weights, e.offset);
                }
            if (const int i = lastMovable(); i >= 0)
                m[(size_t) i].offset = s.kind == SectionKind::answer ? 4 : 0;
            break;
        }

        case SectionKind::close:
        {
            // A's opening, then a stepwise walk down to the root, held until the phrase comes round again.
            std::vector<size_t> second;
            for (size_t i = 0; i < m.size(); ++i)
                if (m[i].step >= 8 && ! m[i].pedal)
                    second.push_back (i);
            if (second.empty())
            {
                if (const int i = lastMovable(); i >= 0)
                    second.push_back ((size_t) i);
            }
            const int n = (int) second.size();
            for (int k = 0; k < n; ++k)
                m[second[(size_t) k]].offset = n - 1 - k;
            if (n > 0)
            {
                const int endStep = m[second.back()].step;
                m.erase (std::remove_if (m.begin(), m.end(), [endStep] (const MotifEvent& e) { return e.step > endStep; }), m.end());
            }
            tail.assign (m.size(), false);
            if (! m.empty())
                tail.back() = true;
            return m;
        }

        case SectionKind::fragment:
        {
            // Fragmentation: the head of the idea, twice, the repeat a step higher.
            std::vector<MotifEvent> head;
            for (const auto& e : a)
                if (e.step < 8)
                    head.push_back (e);
            m = head;
            for (auto e : head)
            {
                e.step += 8;
                if (! e.pedal)
                    e.offset += 1;
                m.push_back (e);
            }
            break;
        }
    }

    tail.assign (m.size(), false);
    return m;
}

} // namespace

const std::vector<int>& styleProgression (Style s)
{
    return styleDef (s).progression;
}

Phrase generateMelody (const GenParams& p)
{
    const auto& def = styleDef (p.style);
    Rng rng ((uint64_t) p.seed * 131ull + (uint64_t) p.style);
    Rng varRng ((uint64_t) p.seed * 7919ull + (uint64_t) p.variation * 104729ull + 17ull);

    const int bars = std::clamp (p.bars, 1, 16);
    const double stepLen = 0.25;

    // ---- rhythm
    const int pulses = std::clamp ((int) std::lround (def.minPulses + (def.maxPulses - def.minPulses) * p.density), 1, 16);
    auto pattern = euclid (pulses, 16, rng.range (0, 3));
    if (! def.fixedRhythm.empty())
    {
        // Fixed rhythm; low density thins out the off-beat 16ths.
        std::fill (pattern.begin(), pattern.end(), false);
        for (int step : def.fixedRhythm)
            pattern[(size_t) step] = step % 4 == 0 || ! rng.chance ((1.0f - p.density) * 0.6f);
    }
    pattern[0] = true; // the downbeat always speaks

    // ---- motif (one bar)
    const float pedalProb = std::clamp (p.pedal + def.pedalBias, 0.0f, 0.95f);
    const float octaveProb = std::clamp (p.octave + def.octaveBias, 0.0f, 0.95f);

    std::vector<MotifEvent> motif;
    int prevOffset = 0;
    int arpIndex = 0;
    const int arpDir = rng.chance (0.5f) ? 1 : -1;
    static const int arpShape[] = { 0, 2, 4, 7, 9, 7, 4, 2 };

    for (int s = 0; s < 16; ++s)
    {
        if (! pattern[(size_t) s])
            continue;

        MotifEvent e { s, 0, 0, false, (s % 4 == 0) || rng.chance (0.2f) };

        if (def.arp)
        {
            const int n = (int) std::size (arpShape);
            const int idx = scales::mod (arpDir > 0 ? arpIndex : n - 1 - arpIndex, n);
            e.offset = arpShape[idx];
            ++arpIndex;
        }
        else
        {
            // In "pursuit" the pedal lands on the strong 8ths, the voice on the rest.
            const bool strong = (s % 2 == 0);
            const float pp = def.upperVoice ? (strong ? pedalProb + 0.25f : pedalProb * 0.4f) : pedalProb;
            e.pedal = (s == 0) || rng.chance (pp);

            if (e.pedal)
                e.offset = 0;
            else
            {
                e.offset = pickWeighted (rng, def.pool, def.weights, prevOffset);
                prevOffset = e.offset;
                if (def.upperVoice)
                    e.octave = 1;
            }
        }

        const bool offbeat = (motif.size() % 2) == 1;
        if (! e.pedal && (! def.octaveOnOffbeats || offbeat) && rng.chance (octaveProb))
            e.octave += 1;
        else if (def.octaveOnOffbeats && offbeat && rng.chance (octaveProb))
            e.octave += 1;

        motif.push_back (e);
    }

    // ---- unfold over bars with variations
    const int tonic = p.key + 12 * (p.baseOctave + 1);
    const int lowest = tonic - 5;
    const int highest = tonic + 12 * std::max (1, p.rangeOctaves) + 7;

    Phrase out;
    out.lengthBeats = bars * 4.0;

    const auto sections = formSections (p.form, bars);
    std::set<long long> heldStarts; // form: closing notes held to the next note
    auto startKey = [] (double t) { return (long long) std::llround (t * 1.0e6); };

    if (! sections.empty())
    {
        // Phrase form: every bar is a section built from the motif; the same label gives the same notes
        // (MUTATE re-draws the answers, never A).
        Rng jitterRng ((uint64_t) p.seed * 6364136223846793005ull + 5ull);
        for (auto& e : motif)
            e.jitter = jitterRng.uniform();

        for (int bar = 0; bar < bars; ++bar)
        {
            const auto& sec = sections[(size_t) bar];
            const int root = def.progression[(size_t) (bar % (int) def.progression.size())];
            Rng secRng ((uint64_t) p.seed * 7919ull + (uint64_t) p.variation * 104729ull + labelHash (sec.label) * 31ull + 17ull);
            std::vector<bool> tail;
            const auto events = sectionMotif (motif, sec, def, secRng, tail);

            for (size_t i = 0; i < events.size(); ++i)
            {
                const auto& e = events[i];
                int pitch = scales::degreeToPitch (tonic, p.scale, scales::mapDegree (p.scale, root + e.offset)) + 12 * e.octave;
                while (pitch > highest) pitch -= 12;
                while (pitch < lowest)  pitch += 12;

                Note n;
                n.start = bar * 4.0 + e.step * stepLen;
                if (e.step % 2 == 1)
                    n.start += p.swing * (stepLen / 3.0);
                n.pitch = pitch;
                n.accent = e.accent;
                n.velocity = std::clamp ((e.pedal ? 0.66f : 0.74f) + (e.accent ? 0.2f : 0.0f) + 0.08f * e.jitter, 0.05f, 1.0f);
                out.notes.push_back (n);
                if (tail[i])
                    heldStarts.insert (startKey (n.start));
            }
        }
    }
    else
    for (int bar = 0; bar < bars; ++bar)
    {
        const int root = def.progression[(size_t) (bar % (int) def.progression.size())];
        const bool responseBar = (bar % 4 == 3) || (bars > 1 && bar == bars - 1);
        const bool evenVariation = (bar % 2 == 1);

        for (auto e : motif)
        {
            if (responseBar && ! e.pedal && varRng.chance (0.35f))
                e.offset = pickWeighted (varRng, def.pool, def.weights, e.offset);
            else if (evenVariation && ! e.pedal && varRng.chance (0.12f))
                e.offset = pickWeighted (varRng, def.pool, def.weights, e.offset);

            if (responseBar && e.step >= 12 && varRng.chance (0.3f))
                e.octave += 1; // lift at the end of the phrase

            int pitch = scales::degreeToPitch (tonic, p.scale, scales::mapDegree (p.scale, root + e.offset)) + 12 * e.octave;
            while (pitch > highest) pitch -= 12;
            while (pitch < lowest)  pitch += 12;

            Note n;
            n.start = bar * 4.0 + e.step * stepLen;
            if (e.step % 2 == 1)
                n.start += p.swing * (stepLen / 3.0);
            n.pitch = pitch;
            n.accent = e.accent;
            n.velocity = std::clamp ((e.pedal ? 0.66f : 0.74f) + (e.accent ? 0.2f : 0.0f) + 0.08f * rng.uniform(), 0.05f, 1.0f);
            out.notes.push_back (n);
        }

        // Response bar: occasionally drop a note for breathing room.
        if (responseBar && out.notes.size() > 3 && varRng.chance (0.25f))
        {
            const size_t barStartIdx = out.notes.size() - motif.size();
            const size_t victim = barStartIdx + 1 + (size_t) varRng.range (0, (int) motif.size() - 2);
            if (victim < out.notes.size())
                out.notes.erase (out.notes.begin() + (long) victim);
        }
    }

    out.sortByStart();
    auto& notes = out.notes;
    const size_t count = notes.size();

    // With a form, per-note decisions depend on (section, step) only, so repeated sections stay identical.
    auto barOf = [] (const Note& n) { return (int) std::floor (n.start / 4.0 + 1.0e-9); };
    auto noteRng = [&] (size_t i, uint64_t salt)
    {
        const int bar = barOf (notes[i]);
        const int step = (int) std::lround ((notes[i].start - bar * 4.0) / stepLen - 0.2);
        return Rng ((uint64_t) p.seed * 2654435761ull + labelHash (sections[(size_t) bar].label) * 97ull + (uint64_t) step * 131ull + salt);
    };

    // ---- chromatic approach notes (lead a semitone into the next note)
    for (size_t i = 0; i + 1 < count; ++i)
    {
        int direction = 0;
        if (sections.empty())
        {
            if (i == 0 || ! rng.chance (p.chroma))
                continue;
            direction = rng.chance (0.6f) ? -1 : 1;
        }
        else
        {
            // Only inside a section, so the approach is the same wherever the section comes back.
            if (barOf (notes[i]) != barOf (notes[i + 1]))
                continue;
            auto r = noteRng (i, 1);
            if (! r.chance (p.chroma))
                continue;
            direction = r.chance (0.6f) ? -1 : 1;
        }
        const int target = notes[i + 1].pitch;
        const int approach = target + direction;
        if (approach != notes[i].pitch && approach != target && ! scales::inScale (approach, p.key, p.scale))
        {
            notes[i].pitch = approach;
            notes[i].chromatic = true;
        }
    }

    // ---- durations, legato and slides
    const float gate = std::clamp (p.gate + def.gateBias, 0.08f, 1.0f);
    const float slideProb = std::clamp (p.slide + def.slideBias, 0.0f, 1.0f);

    for (size_t i = 0; i < count; ++i)
    {
        const double nextStart = (i + 1 < count) ? notes[i + 1].start : out.lengthBeats;
        const double span = std::max (0.05, nextStart - notes[i].start);
        notes[i].length = std::max (0.05, span * gate);

        const bool slide = sections.empty() ? (i > 0 && notes[i - 1].pitch != notes[i].pitch && rng.chance (slideProb))
                                            : (i > 0 && notes[i - 1].pitch != notes[i].pitch && noteRng (i, 2).chance (slideProb));
        if (slide)
        {
            notes[i - 1].length = notes[i].start - notes[i - 1].start; // tie into the slide
            notes[i].glideFrom = notes[i - 1].pitch;
        }
    }

    // Form: closing notes hold until the next note (the cadence breathes).
    if (! heldStarts.empty())
        for (size_t i = 0; i < count; ++i)
            if (heldStarts.count (startKey (notes[i].start)) != 0)
                notes[i].length = ((i + 1 < count) ? notes[i + 1].start : out.lengthBeats) - notes[i].start;

    if (count > 0)
        notes.back().length = std::min (notes.back().length, out.lengthBeats - notes.back().start - 1.0e-3);

    return out;
}

} // namespace dmpe
