#include "MelodyGenerator.h"
#include "Rng.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace dmpe
{
namespace
{

struct StyleDef
{
    std::vector<int> progression; // scale-degree roots, one per bar
    int minPulses, maxPulses;     // onsets per 16-step bar (scaled to the grid)
    float pedalBias, octaveBias, slideBias, gateBias;
    bool upperVoice;              // moving voice sits an octave above the pedal
    bool arp;
    bool octaveOnOffbeats;
    std::vector<int> pool;        // candidate offsets in scale steps from the chord root
    std::vector<int> weights;
    std::vector<int> fixedRhythm; // 16th steps of a fixed rhythm (instead of a generated one)
    float syncopation;            // how far onsets may move off the strong steps
};

const StyleDef& styleDef (Style s)
{
    static const StyleDef defs[] = {
        // pursuit: i i VI VII, pedal on the root with a voice that walks above it
        { { 0, 0, 5, 6 }, 11, 16, 0.2f, -0.1f, 0.0f, -0.1f, true, false, false,
          { 2, 4, 1, 3, 6, 7, 0 }, { 4, 3, 2, 1, 1, 2, 1 }, {}, 0.5f },
        // hate or glory: i VI iv v, octave-jumping riff
        { { 0, 5, 3, 4 }, 6, 11, -0.1f, 0.35f, 0.0f, 0.0f, false, false, true,
          { 0, 2, 4, 7, 1 }, { 4, 2, 3, 2, 1 }, {}, 0.8f },
        // opr: i bII (phrygian), short stabs
        { { 0, 1, 0, 1 }, 5, 9, 0.1f, 0.05f, -0.2f, -0.35f, false, false, false,
          { 0, 1, 2, 4, -1, -3 }, { 3, 3, 2, 2, 1, 1 }, {}, 1.0f },
        // dark arp: i VI VII v
        { { 0, 5, 6, 4 }, 12, 16, -0.3f, 0.0f, -0.1f, 0.0f, false, true, false,
          { 0, 2, 4, 7 }, { 1, 1, 1, 1 }, {}, 0.4f },
        // acid slide: syncopated, glides everywhere
        { { 0, 0, 5, 1 }, 8, 12, 0.0f, 0.1f, 0.35f, 0.15f, false, false, false,
          { 0, 1, 2, 4, 6, 7, -2 }, { 3, 2, 2, 2, 1, 2, 1 }, {}, 1.2f },
        // gallop: i i bVI bVII, 8th + two 16ths on every beat, mostly the root
        { { 0, 0, 5, 6 }, 12, 12, 0.3f, 0.1f, -0.1f, -0.2f, false, false, false,
          { 0, 4, 7, 2, -2 }, { 6, 2, 2, 1, 1 }, { 0, 2, 3, 4, 6, 7, 8, 10, 11, 12, 14, 15 }, 0.0f },
        // rave stab: i VI iv v, sparse syncopated hits, octaves on the offbeats
        { { 0, 5, 3, 4 }, 5, 8, -0.1f, 0.3f, -0.15f, -0.35f, false, false, true,
          { 0, 4, 7, 2, 1 }, { 4, 3, 2, 1, 1 }, {}, 1.4f },
    };
    return defs[(size_t) s];
}

// Salts of the keyed decisions.
enum Salt : uint64_t { rhythmSalt = 1, fixedSalt, holdSalt, lineSalt, arpSalt, varSalt, dropSalt, velSalt, chromaSalt, slideSalt };

// 96 ticks per bar: a position that is the same on every grid (the 8th after beat 1 is tick 12 at 1/8 and 1/16).
int tickOf (int step, int n) { return step * 96 / n; }

// Metric weight of a step: 5 downbeat, 4 half bar, 3 beat, 2 8th (or 8th triplet), 1 16th (or 16th triplet), 0 finer.
int metricLevel (int s, int n)
{
    if (s == 0)
        return 5;
    if (s * 2 == n)
        return 4;
    const int beat = std::max (1, n / 4);
    const int o = s % beat;
    if (o == 0)
        return 3;
    if (beat % 3 == 0)
        return o % (beat / 3) == 0 ? 2 : 1;
    if (o * 2 == beat)
        return 2;
    if (beat >= 4 && o % (beat / 4) == 0)
        return 1;
    return 0;
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
    int step;       // 0..n-1
    int offset;     // scale steps from chord root
    int octave;     // extra octaves
    bool pedal;
    bool accent;
    bool hold = false;   // a long note: holds over the steps it swallowed
    float jitter = 0.0f; // velocity variation fixed per event (forms: repeated sections stay identical)
};

// A section of a phrase form built from the basic idea A. Offsets are scale steps from the chord root; pedal
// events stay on the root in every section (they carry the style). `tail` marks the note held to the next one.
std::vector<MotifEvent> sectionMotif (const std::vector<MotifEvent>& a, const Section& s, const StyleDef& def, Rng& rng,
                                      int n, std::vector<bool>& tail)
{
    const int half = n / 2;
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
                if (e.step < half && ! e.pedal)
                    pivot = e.offset;
            for (auto& e : m)
                if (e.step >= half && ! e.pedal)
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
                if (m[i].step >= half && ! m[i].pedal)
                    second.push_back (i);
            if (second.empty())
            {
                if (const int i = lastMovable(); i >= 0)
                    second.push_back ((size_t) i);
            }
            const int count = (int) second.size();
            for (int k = 0; k < count; ++k)
                m[second[(size_t) k]].offset = count - 1 - k;
            if (count > 0)
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
                if (e.step < half)
                    head.push_back (e);
            m = head;
            for (auto e : head)
            {
                e.step += half;
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

// Where each generated note came from: the decisions after the notes are laid out are keyed by it.
struct NoteInfo
{
    int bar = 0;
    int step = 0;
    int level = 0;
    bool pedal = false;
    bool hold = false;
    bool tail = false;
    uint64_t place = 0; // the bar (classic) or the section label (forms): repeated sections decide alike
};

} // namespace

int stepsPerBar (Rate r)
{
    static const int steps[] = { 4, 8, 12, 16, 24, 32 };
    return steps[(size_t) std::clamp ((int) r, 0, (int) Rate::count - 1)];
}

const std::vector<int>& styleProgression (Style s)
{
    return styleDef (s).progression;
}

HarmonyTrack leadHarmony (const GenParams& p, SectionMarks* marks)
{
    // A form (or the seeded Auto progression) shapes the chords over the whole loop; the lead is built on a
    // 4-bar cycle of it, repeated, so its sections line up with the lead's (one per bar) and more bars keep the
    // first ones.
    const bool cycle = p.harmony == HarmonySource::progression && p.bars > 4
                    && (p.form != Form::classic || p.progression == Progression::autoSeed);
    if (cycle)
    {
        auto one = p;
        one.bars = 4;
        auto track = leadHarmony (one, marks);
        const size_t count = track.spans.size();
        for (int rep = 1; rep * 4 < p.bars; ++rep)
            for (size_t i = 0; i < count; ++i)
            {
                auto span = track.spans[i];
                span.start += rep * 16.0;
                span.chord.start = span.start;
                if (span.start < p.bars * 4.0 - 1.0e-9)
                    track.spans.push_back (span);
            }
        return track;
    }

    HarmonySpec spec;
    spec.source = p.harmony;
    spec.key = p.key;
    spec.scale = p.scale;
    spec.bars = p.bars;
    spec.styleDegrees = styleProgression (p.style);
    spec.progression.progression = p.progression;
    spec.progression.chordLength = p.chordLength;
    spec.progression.darkness = p.darkness;
    spec.progression.form = p.form;
    spec.progression.seed = p.seed;
    spec.scaleLock = p.scaleLock;
    return buildHarmony (spec, marks);
}

Phrase generateMelody (const GenParams& p)
{
    const auto& def = styleDef (p.style);
    const uint64_t seed = (uint64_t) p.seed * 131ull + (uint64_t) p.style;
    const uint64_t var = (uint64_t) p.variation;

    const int bars = std::clamp (p.bars, 1, 16);
    const int n = stepsPerBar (p.rate);
    const int beat = std::max (1, n / 4);
    const double stepLen = 4.0 / n;
    const auto harmony = leadHarmony (p);

    // ---- rhythm: every step has a fixed priority (metric weight + a seeded push off the beat); density takes
    // the first ones, so more density only adds notes.
    std::vector<bool> pattern ((size_t) n, false);
    if (! def.fixedRhythm.empty())
    {
        // Fixed rhythm; low density thins out the off-beat notes.
        for (int step16 : def.fixedRhythm)
        {
            const int s = std::min (n - 1, (int) std::lround (step16 * n / 16.0));
            auto r = keyedRng ({ seed, fixedSalt, (uint64_t) tickOf (s, n) });
            if (s % beat == 0 || ! r.chance ((1.0f - p.density) * 0.6f))
                pattern[(size_t) s] = true;
        }
    }
    else
    {
        const double per16 = def.minPulses + (def.maxPulses - def.minPulses) * (double) p.density;
        const int pulses = std::clamp ((int) std::lround (per16 * n / 16.0), 1, n);
        std::vector<float> priority ((size_t) n);
        for (int s = 0; s < n; ++s)
            priority[(size_t) s] = (float) metricLevel (s, n)
                                 + def.syncopation * 2.5f * keyedRng ({ seed, rhythmSalt, (uint64_t) tickOf (s, n) }).uniform();
        std::vector<int> order ((size_t) n);
        std::iota (order.begin(), order.end(), 0);
        std::stable_sort (order.begin(), order.end(), [&] (int a, int b) { return priority[(size_t) a] > priority[(size_t) b]; });
        for (int k = 0; k < pulses; ++k)
            pattern[(size_t) order[(size_t) k]] = true;
    }
    pattern[0] = true; // the downbeat always speaks

    // ---- long notes: a note on a strong step may hold over the next ones
    std::vector<bool> hold ((size_t) n, false);
    if (p.longNotes > 0.0f)
        for (int s = 0; s < n; ++s)
        {
            if (! pattern[(size_t) s])
                continue;
            auto r = keyedRng ({ seed, holdSalt, (uint64_t) tickOf (s, n) });
            const int level = metricLevel (s, n);
            const float chance = p.longNotes * (level >= 3 ? 0.75f : (level == 2 ? 0.35f : 0.1f));
            if (! r.chance (chance))
                continue;
            std::vector<int> spans;
            if (beat % 3 == 0)
                spans = { beat * 2 / 3, beat, 2 * beat }; // quarter triplet, quarter, half
            else
                spans = { std::max (1, beat / 2), std::max (1, beat * 3 / 4), beat, 2 * beat }; // 8th, dotted 8th, quarter, half
            const int h = std::max (1, spans[(size_t) std::min ((int) spans.size() - 1, (int) (r.uniform() * (float) spans.size() * (0.55f + 0.45f * p.longNotes)))]);
            if (h <= 1)
                continue;
            hold[(size_t) s] = true;
            for (int k = s + 1; k < std::min (n, s + h); ++k)
                pattern[(size_t) k] = false;
        }

    // ---- the line: a pitch for every step of the bar (not only the sounding ones), so density never changes it
    const float pedalProb = std::clamp (p.pedal + def.pedalBias, 0.0f, 0.95f);
    const float octaveProb = std::clamp (p.octave + def.octaveBias, 0.0f, 0.95f);
    std::vector<MotifEvent> line;
    int prevOffset = 0;
    for (int s = 0; s < n; ++s)
    {
        auto r = keyedRng ({ seed, lineSalt, (uint64_t) tickOf (s, n) });
        const int level = metricLevel (s, n);
        MotifEvent e { s, 0, 0, false, level >= 3 || r.chance (0.2f) };

        const bool strong = level >= 2;
        const float pp = def.upperVoice ? (strong ? pedalProb + 0.25f : pedalProb * 0.4f) : pedalProb;
        e.pedal = (s == 0) || r.chance (pp);
        if (! e.pedal)
        {
            e.offset = pickWeighted (r, def.pool, def.weights, prevOffset);
            prevOffset = e.offset;
            if (def.upperVoice)
                e.octave = 1;
        }

        const bool offbeat = s % beat != 0;
        if (! e.pedal && (! def.octaveOnOffbeats || offbeat) && r.chance (octaveProb))
            e.octave += 1;
        else if (def.octaveOnOffbeats && offbeat && r.chance (octaveProb))
            e.octave += 1;
        line.push_back (e);
    }

    // ---- motif (one bar): the sounding steps of the line
    std::vector<MotifEvent> motif;
    {
        static const int arpShape[] = { 0, 2, 4, 7, 9, 7, 4, 2 };
        const int arpDir = keyedRng ({ seed, arpSalt }).chance (0.5f) ? 1 : -1;
        int arpIndex = 0;
        for (int s = 0; s < n; ++s)
        {
            if (! pattern[(size_t) s])
                continue;
            auto e = line[(size_t) s];
            e.hold = hold[(size_t) s];
            if (def.arp)
            {
                const int count = (int) std::size (arpShape);
                e.offset = arpShape[scales::mod (arpDir > 0 ? arpIndex : count - 1 - arpIndex, count)];
                e.pedal = false;
                e.octave = keyedRng ({ seed, arpSalt, (uint64_t) tickOf (s, n) }).chance (octaveProb) ? 1 : 0;
                ++arpIndex;
            }
            motif.push_back (e);
        }
    }

    // ---- unfold over bars
    const int tonic = p.key + 12 * (p.baseOctave + 1);
    const int lowest = tonic - 5;
    const int highest = tonic + 12 * std::max (1, p.rangeOctaves) + 7;

    Phrase out;
    out.lengthBeats = bars * 4.0;
    std::vector<NoteInfo> info;

    auto place = [&] (const MotifEvent& e, int bar, float jitter, const NoteInfo& ni)
    {
        Note note;
        note.start = bar * 4.0 + e.step * stepLen;
        if (e.step % 2 == 1 && beat % 3 != 0 && n >= 8)
            note.start += p.swing * (stepLen / 3.0);
        int pitch = harmony.pitch (bar * 4.0 + e.step * stepLen, tonic, e.offset) + 12 * e.octave;
        while (pitch > highest) pitch -= 12;
        while (pitch < lowest)  pitch += 12;
        note.pitch = pitch;
        note.accent = e.accent;
        note.velocity = std::clamp ((e.pedal ? 0.66f : 0.74f) + (e.accent ? 0.2f : 0.0f) + 0.08f * jitter, 0.05f, 1.0f);
        out.notes.push_back (note);
        info.push_back (ni);
    };

    const auto sections = formSections (p.form, bars);
    if (! sections.empty())
    {
        // Phrase form: every bar is a section built from the motif; the same label gives the same notes
        // (MUTATE re-draws the answers, never A).
        for (auto& e : motif)
            e.jitter = keyedRng ({ seed, velSalt, (uint64_t) tickOf (e.step, n) }).uniform();

        for (int bar = 0; bar < bars; ++bar)
        {
            const auto& sec = sections[(size_t) bar];
            const uint64_t label = hashLabel (sec.label.c_str());
            Rng secRng = keyedRng ({ seed, varSalt, var, label });
            std::vector<bool> tail;
            const auto events = sectionMotif (motif, sec, def, secRng, n, tail);
            for (size_t i = 0; i < events.size(); ++i)
                place (events[i], bar, events[i].jitter,
                       { bar, events[i].step, metricLevel (events[i].step, n), events[i].pedal, events[i].hold, tail[i], label });
        }
    }
    else
        for (int bar = 0; bar < bars; ++bar)
        {
            // Every 2nd bar varies a little, every 4th answers (MUTATE re-draws these).
            const bool responseBar = bar % 4 == 3;
            const bool evenVariation = bar % 2 == 1;
            const size_t first = out.notes.size();

            for (auto e : motif)
            {
                auto r = keyedRng ({ seed, varSalt, var, (uint64_t) bar, (uint64_t) tickOf (e.step, n) });
                if (responseBar && ! e.pedal && r.chance (0.35f))
                    e.offset = pickWeighted (r, def.pool, def.weights, e.offset);
                else if (evenVariation && ! e.pedal && r.chance (0.12f))
                    e.offset = pickWeighted (r, def.pool, def.weights, e.offset);

                if (responseBar && e.step * 4 >= n * 3 && r.chance (0.3f))
                    e.octave += 1; // lift at the end of the phrase

                const float jitter = keyedRng ({ seed, velSalt, (uint64_t) bar, (uint64_t) tickOf (e.step, n) }).uniform();
                place (e, bar, jitter, { bar, e.step, metricLevel (e.step, n), e.pedal, e.hold, false, (uint64_t) bar });
            }

            // Response bar: occasionally drop a note (never the downbeat) for breathing room: the one whose
            // position draws lowest, so a denser bar drops the same note or one of the new ones.
            const size_t count = out.notes.size() - first;
            if (responseBar && count > 2 && keyedRng ({ seed, dropSalt, var, (uint64_t) bar }).chance (0.25f))
            {
                size_t victim = first + 1;
                float lowest = 2.0f;
                for (size_t i = first + 1; i < out.notes.size(); ++i)
                    if (const float v = keyedRng ({ seed, dropSalt, var, (uint64_t) bar, (uint64_t) tickOf (info[i].step, n) }).uniform(); v < lowest)
                    {
                        lowest = v;
                        victim = i;
                    }
                out.notes.erase (out.notes.begin() + (long) victim);
                info.erase (info.begin() + (long) victim);
            }
        }

    // Notes were laid out in time order (swing only delays inside a step), so `info` stays aligned.
    auto& notes = out.notes;
    const size_t count = notes.size();
    auto noteRng = [&] (size_t i, uint64_t salt)
    {
        return keyedRng ({ seed, salt, info[i].place, (uint64_t) tickOf (info[i].step, n) });
    };

    // ---- approach notes: a weak moving note leads into the next one, a semitone away (or a scale step with
    // Scale Lock). With a form only inside a bar, so the approach is the same wherever the section comes back.
    for (size_t i = 0; i + 1 < count; ++i)
    {
        const auto& ni = info[i];
        if (ni.level >= 3 || ni.pedal || ni.hold || ni.tail)
            continue;
        if (! sections.empty() && info[i].bar != info[i + 1].bar)
            continue;
        auto r = noteRng (i, chromaSalt);
        if (! r.chance (p.chroma))
            continue;
        const int direction = r.chance (0.6f) ? -1 : 1;
        const int target = notes[i + 1].pitch;
        if (p.scaleLock)
        {
            const int approach = harmony.step (notes[i + 1].start, target, direction);
            if (approach != notes[i].pitch && approach != target && scales::inScale (approach, p.key, p.scale))
                notes[i].pitch = approach;
        }
        else
        {
            const int approach = target + direction;
            if (approach != notes[i].pitch && ! scales::inScale (approach, p.key, p.scale))
            {
                notes[i].pitch = approach;
                notes[i].chromatic = true;
            }
        }
    }

    // ---- durations, legato and slides
    const float gate = std::clamp (p.gate + def.gateBias, 0.08f, 1.0f);
    const float slideProb = std::clamp (p.slide + def.slideBias, 0.0f, 1.0f);

    for (size_t i = 0; i < count; ++i)
    {
        const double nextStart = (i + 1 < count) ? notes[i + 1].start : out.lengthBeats;
        const double span = std::max (0.05, nextStart - notes[i].start);
        notes[i].length = std::max (0.05, span * (info[i].hold ? std::max (gate, 0.92f) : gate));

        if (i > 0 && notes[i - 1].pitch != notes[i].pitch && noteRng (i, slideSalt).chance (slideProb))
        {
            notes[i - 1].length = notes[i].start - notes[i - 1].start; // tie into the slide
            notes[i].glideFrom = notes[i - 1].pitch;
        }
    }

    // Form: closing notes hold until the next note (the cadence breathes).
    for (size_t i = 0; i < count; ++i)
        if (info[i].tail)
            notes[i].length = ((i + 1 < count) ? notes[i + 1].start : out.lengthBeats) - notes[i].start;

    if (count > 0)
        notes.back().length = std::min (notes.back().length, out.lengthBeats - notes.back().start - 1.0e-3);

    out.sortByStart();
    return out;
}

} // namespace dmpe
