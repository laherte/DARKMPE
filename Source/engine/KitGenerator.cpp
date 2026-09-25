#include "KitGenerator.h"
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

int barsOf (const GenParams& g) { return std::clamp (g.bars, 1, 16); }

int rootDegree (const GenParams& g, int bar)
{
    const auto& prog = styleProgression (g.style);
    return prog[(size_t) (bar % (int) prog.size())];
}

int pitchOf (const GenParams& g, int tonicPitch, int degree)
{
    return scales::degreeToPitch (tonicPitch, g.scale, scales::mapDegree (g.scale, degree));
}

Rng layerRng (const GenParams& g, Layer l)
{
    return Rng ((uint64_t) g.seed * 2246822519ull + (uint64_t) l * 3266489917ull + (uint64_t) g.variation * 668265263ull + 1ull);
}

// With a phrase form, the same section label gives the same random choices (A bars repeat exactly).
Rng sectionRng (const GenParams& g, Layer l, const std::string& label, int bar, bool form)
{
    const uint64_t salt = form ? labelHash (label.c_str()) : (uint64_t) bar * 40503ull;
    return Rng ((uint64_t) g.seed * 2246822519ull + (uint64_t) l * 3266489917ull + (uint64_t) g.variation * 668265263ull + salt);
}

// Per-note seeds (Note::exprSeed): with a form the same (section, position) gives the same seed, so a repeated
// section is humanized and gestured identically; statements ignore MUTATE.
void seedNotes (Phrase& p, const GenParams& g, Layer l, const std::vector<Section>& sections)
{
    for (auto& n : p.notes)
    {
        const int bar = std::clamp ((int) std::floor (n.start / 4.0 + 1.0e-9), 0, 1 << 20);
        const auto step = (uint64_t) std::lround ((n.start - bar * 4.0) * 64.0);
        uint64_t s = mixSeed ((uint64_t) g.seed, (uint64_t) l * 977ull + 13ull);
        if (sections.empty())
            s = mixSeed (mixSeed (s, (uint64_t) g.variation), (uint64_t) bar * 4096ull + step);
        else
        {
            const auto& sec = sections[(size_t) bar % sections.size()];
            n.sectionKind = (int) sec.kind;
            const uint64_t v = sec.kind == SectionKind::statement ? 0 : (uint64_t) g.variation;
            s = mixSeed (mixSeed (s, labelHash (sec.label.c_str())), step * 131ull + v * 7919ull + (uint64_t) n.pitch);
        }
        n.exprSeed = s;
    }
}

// The key's tonic in the octave nearest to `near`.
int nearestTonic (const GenParams& g, int near)
{
    const int pc = scales::mod (g.key, 12);
    return near - scales::mod (near - pc + 6, 12) + 6;
}

std::vector<bool> euclid (int pulses, int steps, int rotation)
{
    std::vector<bool> out ((size_t) steps, false);
    for (int i = 0; i < steps; ++i)
        out[(size_t) ((i + rotation) % steps)] = ((i * pulses) % steps) < pulses;
    return out;
}

float smooth (float x)
{
    x = std::clamp (x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

Note makeNote (double start, double length, int pitch, float velocity)
{
    Note n;
    n.start = start;
    n.length = length;
    n.pitch = std::clamp (pitch, 0, 127);
    n.velocity = std::clamp (velocity, 0.05f, 1.0f);
    return n;
}

// Keeps every note inside the loop.
void clip (Phrase& p)
{
    for (auto& n : p.notes)
        n.length = std::max (1.0 / 64.0, std::min (n.length, p.lengthBeats - n.start - 1.0e-3));
}

// ------------------------------------------------------------------ bass
Phrase bassLine (const GenParams& g, const LayerParams& lp)
{
    Phrase out;
    out.lengthBeats = barsOf (g) * 4.0;
    Rng layerWide = layerRng (g, Layer::bass);
    const int tonic = g.key + 12 * (2 + std::clamp (lp.octave, -1, 2)); // A1 for A
    const float d = lp.density;
    const auto sections = formSections (g.form, barsOf (g));

    for (int bar = 0; bar < barsOf (g); ++bar)
    {
        const double b0 = bar * 4.0;
        const int r = rootDegree (g, bar);
        const int root = pitchOf (g, tonic, r);
        Rng barRng = sectionRng (g, Layer::bass, sections.empty() ? std::string() : sections[(size_t) bar].label, bar, ! sections.empty());
        Rng& rng = sections.empty() ? layerWide : barRng;
        const size_t firstOfBar = out.notes.size();

        switch ((BassPattern) lp.pattern)
        {
            case BassPattern::rolling: // the three 16ths after every beat
                for (int beat = 0; beat < 4; ++beat)
                    for (int sub = 1; sub <= 3; ++sub)
                        if (sub == 1 || rng.chance (0.35f + 0.65f * d))
                            out.notes.push_back (makeNote (b0 + beat + sub * 0.25, 0.2, root, sub == 1 ? 0.86f : 0.72f));
                break;

            case BassPattern::offbeat:
                for (int beat = 0; beat < 4; ++beat)
                {
                    out.notes.push_back (makeNote (b0 + beat + 0.5, 0.36, root, 0.86f));
                    if (d > 0.6f && rng.chance (d - 0.4f))
                        out.notes.push_back (makeNote (b0 + beat + 0.75, 0.18, root, 0.7f));
                }
                break;

            case BassPattern::pedalOctave:
                for (int e = 0; e < 8; ++e)
                {
                    const bool up = e % 2 == 1 && rng.chance (0.4f + 0.6f * d);
                    out.notes.push_back (makeNote (b0 + e * 0.5, 0.4, up ? root + 12 : root, e % 2 == 0 ? 0.86f : 0.74f));
                }
                break;

            case BassPattern::arpDown: // octave, fifth, third, root on every beat
            case BassPattern::count:
            {
                static const int shape[] = { 7, 4, 2, 0 };
                for (int s = 0; s < 16; ++s)
                    if (s % 4 == 0 || rng.chance (0.3f + 0.7f * d))
                        out.notes.push_back (makeNote (b0 + s * 0.25, 0.2, pitchOf (g, tonic, r + shape[s % 4]), s % 4 == 0 ? 0.86f : 0.72f));
                break;
            }
        }

        if (! sections.empty())
        {
            const auto kind = sections[(size_t) bar].kind;
            auto lastBeat = [&] { out.notes.erase (std::remove_if (out.notes.begin() + (long) firstOfBar, out.notes.end(),
                                                                   [b0] (const Note& n) { return n.start >= b0 + 3.0; }), out.notes.end()); };
            if (kind == SectionKind::close)
            {
                // Cadence fill: the last beat walks by step from the chord root home to the key's tonic
                // (from the tonic itself: up to the octave).
                lastBeat();
                const int home = scales::mod (r, 7) == 0 ? r + 7 : 7 * (int) std::lround ((double) r / 7.0);
                for (int k = 0; k < 4; ++k)
                {
                    const int deg = r + (int) std::lround ((double) k * (home - r) / 3.0);
                    const int pitch = k == 3 ? nearestTonic (g, pitchOf (g, tonic, deg)) : pitchOf (g, tonic, deg);
                    out.notes.push_back (makeNote (b0 + 3.0 + k * 0.25, 0.2, pitch, 0.8f));
                }
            }
            else if (kind == SectionKind::answer || kind == SectionKind::closedAnswer)
            {
                // The answer lifts (the last 8th jumps an octave); the closed answer lands on the key's tonic.
                out.notes.erase (std::remove_if (out.notes.begin() + (long) firstOfBar, out.notes.end(),
                                                 [b0] (const Note& n) { return n.start >= b0 + 3.5; }), out.notes.end());
                const int last = kind == SectionKind::answer ? root + 12 : nearestTonic (g, root);
                out.notes.push_back (makeNote (b0 + 3.5, 0.36, last, 0.84f));
            }
        }
    }

    // Slide from the last note of a bar into the next bar.
    out.sortByStart();
    auto& n = out.notes;
    for (size_t i = 0; i + 1 < n.size(); ++i)
    {
        const bool lastOfBar = (int) (n[i].start / 4.0) != (int) (n[i + 1].start / 4.0);
        if (lastOfBar && n[i].pitch != n[i + 1].pitch && layerWide.chance (g.slide))
        {
            n[i].length = n[i + 1].start - n[i].start;
            n[i + 1].glideFrom = n[i].pitch;
        }
    }
    clip (out);
    seedNotes (out, g, Layer::bass, sections);
    return out;
}

// ------------------------------------------------------------------ arp
Phrase arpLine (const GenParams& g, const LayerParams& lp)
{
    Phrase out;
    out.lengthBeats = barsOf (g) * 4.0;
    Rng rng = layerRng (g, Layer::arp);
    const int tonic = g.key + 12 * (4 + std::clamp (lp.octave, -2, 2)); // A3 for A
    const auto pattern = euclid (std::clamp ((int) std::lround (6.0f + 10.0f * lp.density), 4, 16), 16, 0);

    const auto sections = formSections (g.form, barsOf (g));
    int index = 0, previous = -1;
    for (int bar = 0; bar < barsOf (g); ++bar)
    {
        const int r = rootDegree (g, bar);
        std::vector<int> tones; // chord tones over two octaves, ascending, plus the top root
        for (int o = 0; o < 2; ++o)
            for (int deg : { 0, 2, 4 })
                tones.push_back (pitchOf (g, tonic, r + deg + 7 * o));
        tones.push_back (pitchOf (g, tonic, r + 14));
        const int n = (int) tones.size();

        // With a form every bar restarts: A bars repeat exactly, answers turn round, the close descends home.
        const Section* sec = sections.empty() ? nullptr : &sections[(size_t) bar];
        Rng barRng = sectionRng (g, Layer::arp, sec != nullptr ? sec->label : std::string(), bar, sec != nullptr);
        if (sec != nullptr)
        {
            index = 0;
            previous = -1;
        }

        std::vector<std::pair<int, int>> hits; // step, tone index
        for (int s = 0; s < 16; ++s)
        {
            if (! pattern[(size_t) s] && s != 0)
                continue;
            int k = 0;
            switch ((ArpPattern) lp.pattern)
            {
                case ArpPattern::up:     k = index % n; break;
                case ArpPattern::down:   k = n - 1 - index % n; break;
                case ArpPattern::upDown: { const int cycle = 2 * n - 2; const int x = index % cycle; k = x < n ? x : cycle - x; break; }
                case ArpPattern::random:
                case ArpPattern::count:
                {
                    Rng& pick = sec != nullptr ? barRng : rng;
                    do { k = pick.range (0, n - 1); } while (k == previous && n > 1);
                    break;
                }
            }
            previous = k;
            ++index;
            hits.push_back ({ s, k });
        }

        bool holdLast = false;
        if (sec != nullptr)
        {
            switch (sec->kind)
            {
                case SectionKind::statement:
                    for (auto& h : hits) h.second = std::min (n - 1, h.second + sec->shift);
                    break;
                case SectionKind::answer:
                case SectionKind::closedAnswer:
                    for (auto& h : hits) h.second = n - 1 - h.second;
                    if (! hits.empty() && sec->kind == SectionKind::closedAnswer) hits.back().second = 0;
                    break;
                case SectionKind::close:
                    for (size_t i = 0; i < hits.size(); ++i)
                        hits[i].second = std::max (0, (int) (hits.size() - 1 - i) % n);
                    holdLast = true;
                    break;
                case SectionKind::fragment:
                {
                    std::vector<std::pair<int, int>> head;
                    for (const auto& h : hits) if (h.first < 8) head.push_back (h);
                    hits = head;
                    for (const auto& h : head) hits.push_back ({ h.first + 8, std::min (n - 1, h.second + 1) });
                    break;
                }
            }
        }

        // Closing sections land on the key's tonic, whatever the chord.
        const bool landHome = sec != nullptr && (sec->kind == SectionKind::close || sec->kind == SectionKind::closedAnswer);
        for (size_t i = 0; i < hits.size(); ++i)
        {
            const int s = hits[i].first;
            const bool last = i + 1 == hits.size();
            int pitch = tones[(size_t) hits[i].second];
            if (landHome && last)
                pitch = nearestTonic (g, i > 0 ? out.notes.back().pitch : pitch);
            out.notes.push_back (makeNote (bar * 4.0 + s * 0.25, holdLast && last ? 4.0 - s * 0.25 - 0.02 : 0.14,
                                           pitch, s % 4 == 0 ? 0.84f : 0.7f));
        }
    }
    clip (out);
    seedNotes (out, g, Layer::arp, sections);
    return out;
}

// ------------------------------------------------------------------ siren
Phrase sirenLine (const GenParams& g, const LayerParams& lp)
{
    Phrase out;
    out.lengthBeats = barsOf (g) * 4.0;
    const int tonic = g.key + 12 * (5 + std::clamp (lp.octave, -2, 1)); // A4 for A
    const auto pattern = (SirenPattern) lp.pattern;
    const double span = pattern == SirenPattern::rise && barsOf (g) >= 2 ? 8.0 : 4.0;
    const float rate = 0.25f + 0.75f * lp.density; // wail cycles per beat

    for (double t0 = 0.0; t0 < out.lengthBeats - 1.0e-9; t0 += span)
    {
        const int bar = (int) (t0 / 4.0);
        Note n = makeNote (t0, std::min (span, out.lengthBeats - t0) - 0.02, pitchOf (g, tonic, rootDegree (g, bar)), 0.85f);
        n.lockedExpr = true;
        const double len = n.length;

        for (int i = 0;; ++i)
        {
            const double t = std::min (len, i * curveResolution);
            const float x = (float) (t / len);
            float v = 0.0f;
            switch (pattern)
            {
                case SirenPattern::rise:  v = 12.0f * std::pow (smooth (x / 0.95f), 1.6f); break;
                case SirenPattern::wail:  v = 2.5f - 2.5f * (float) std::cos (6.283185307 * rate * t); break;
                case SirenPattern::fall:  v = 12.0f * (1.0f - smooth ((float) (t / 0.45))) - 3.0f * smooth ((float) ((t - (len - 0.4)) / 0.4)); break;
                case SirenPattern::alarm:
                case SirenPattern::count:
                {
                    // Minor third up and down in 8ths, with a short slew so it is a bend, not a jump.
                    const double phase = std::fmod (t, 1.0);
                    const float up = smooth ((float) ((phase - 0.5) / 0.03)) * (1.0f - smooth ((float) ((phase - 0.97) / 0.03)));
                    v = 3.0f * up;
                    break;
                }
            }
            n.bend.push_back ({ t, v });
            n.pressure.push_back ({ t, 0.35f + 0.55f * smooth (x / 0.6f) });
            n.slide.push_back ({ t, std::clamp (0.3f + 0.45f * x + 0.15f * lp.density, 0.0f, 1.0f) });
            if (t >= len)
                break;
        }
        // The alarm's edges fall between grid points: add them so the square keeps its shape.
        if (pattern == SirenPattern::alarm)
        {
            Curve dense;
            for (double t = 0.0; t < len; t += 1.0 / 128.0)
            {
                const double phase = std::fmod (t, 1.0);
                const float up = smooth ((float) ((phase - 0.5) / 0.03)) * (1.0f - smooth ((float) ((phase - 0.97) / 0.03)));
                dense.push_back ({ t, 3.0f * up });
            }
            dense.push_back ({ len, n.bend.back().v });
            n.bend = std::move (dense);
        }
        out.notes.push_back (n);
    }
    return out;
}

// ------------------------------------------------------------------ stab
Phrase stabLine (const GenParams& g, const LayerParams& lp, const Phrase& lead)
{
    Phrase out;
    out.lengthBeats = barsOf (g) * 4.0;
    const float d = lp.density;
    const auto pattern = (StabPattern) lp.pattern;

    std::set<double> hits;
    switch (pattern)
    {
        case StabPattern::accents:
            for (const auto& n : lead.notes)
                if (n.accent)
                    hits.insert (std::round (n.start * 4.0) / 4.0);
            break;
        case StabPattern::offbeat:
            for (int beat = 0; beat < barsOf (g) * 4; ++beat)
                hits.insert (beat + 0.5);
            break;
        case StabPattern::syncopated:
        {
            const auto e = euclid (3 + (int) std::lround (d * 4.0f), 16, 3);
            for (int bar = 0; bar < barsOf (g); ++bar)
                for (int s = 0; s < 16; ++s)
                    if (e[(size_t) s])
                        hits.insert (bar * 4.0 + s * 0.25);
            break;
        }
        case StabPattern::downbeat:
        case StabPattern::count:
            for (int bar = 0; bar < barsOf (g); ++bar)
            {
                hits.insert (bar * 4.0);
                if (d > 0.6f)
                    hits.insert (bar * 4.0 + 2.5);
            }
            break;
    }

    VoicingParams vp;
    vp.mode = VoicingMode::powerOctave;
    vp.voices = 4;
    vp.lowPitch = 45 + 12 * std::clamp (lp.octave, -2, 2);
    vp.highPitch = vp.lowPitch + 36;
    vp.bassAnchor = true;
    vp.voiceLeading = true;

    const double baseLen = pattern == StabPattern::downbeat ? 0.75 : 0.12 + 0.2 * d;
    std::vector<int> prev;
    const std::vector<double> times (hits.begin(), hits.end());
    for (size_t i = 0; i < times.size(); ++i)
    {
        const double t = times[i];
        if (t >= out.lengthBeats - 0.05)
            continue;
        const int bar = (int) (t / 4.0);
        const int r = rootDegree (g, bar);
        Chord c;
        for (int deg : { 0, 2, 4 })
            c.pitches.push_back (pitchOf (g, g.key + 48, r + deg));
        const auto slots = voiceChordSlots (c, vp, prev.empty() ? nullptr : &prev);
        prev = slots;

        const double next = i + 1 < times.size() ? times[i + 1] : out.lengthBeats;
        const double len = std::max (0.05, std::min (baseLen, next - t - 0.01));
        const bool accent = std::fmod (t, 1.0) < 1.0e-9;
        for (size_t k = 0; k < slots.size(); ++k)
        {
            Note n = makeNote (t, len, slots[k], accent ? 0.9f : 0.78f);
            n.lockedExpr = true;
            n.voiceIndex = (int) k;
            n.voiceCount = (int) slots.size();
            const float fall = 1.0f + d;
            for (int j = 0; j <= 8; ++j)
            {
                const double tt = len * j / 8.0;
                const float x = (float) j / 8.0f;
                const float f = x > 0.6f ? (x - 0.6f) / 0.4f : 0.0f;
                n.bend.push_back ({ tt, -fall * f * f });
                n.pressure.push_back ({ tt, 0.9f - 0.5f * x });
                n.slide.push_back ({ tt, (accent ? 0.75f : 0.6f) - 0.3f * x });
            }
            out.notes.push_back (n);
        }
    }
    return out;
}

} // namespace

std::vector<Region> kitChords (const GenParams& gen)
{
    std::vector<Region> regions;
    for (int bar = 0; bar < barsOf (gen); ++bar)
    {
        Region r;
        r.start = bar * 4.0;
        r.length = 4.0;
        const int deg = rootDegree (gen, bar);
        for (int step : { 0, 2, 4 })
            r.pitches.push_back (pitchOf (gen, gen.key + 48, deg + step));
        regions.push_back (r);
    }
    return regions;
}

std::vector<KitPart> generateKit (const KitParams& p)
{
    std::vector<KitPart> out;
    const Phrase lead = generateMelody (p.gen); // also gives the Stab its accents
    const double length = barsOf (p.gen) * 4.0;

    for (int l = 0; l < numLayers; ++l)
    {
        const auto& lp = p.layers[(size_t) l];
        if (! lp.on)
            continue;

        KitPart part { (Layer) l, {} };
        switch ((Layer) l)
        {
            case Layer::lead:  part.phrase = lead; break;
            case Layer::bass:  part.phrase = bassLine (p.gen, lp); break;
            case Layer::arp:   part.phrase = arpLine (p.gen, lp); break;
            case Layer::siren: part.phrase = sirenLine (p.gen, lp); break;
            case Layer::stab:  part.phrase = stabLine (p.gen, lp, lead); break;
            case Layer::pad:
            case Layer::count: part.phrase = cinematicRegions (kitChords (p.gen), length, p.pad, p.gen.key); break;
        }
        part.phrase.lengthBeats = length;
        part.phrase.sortByStart();
        out.push_back (std::move (part));
    }
    return out;
}

} // namespace dmpe
