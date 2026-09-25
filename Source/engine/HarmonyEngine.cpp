#include "HarmonyEngine.h"
#include "Rng.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace dmpe
{
namespace
{

using scales::mod;

namespace q
{
const std::vector<int> min { 0, 3, 7 }, maj { 0, 4, 7 }, dim { 0, 3, 6 }, aug { 0, 4, 8 };
const std::vector<int> min7 { 0, 3, 7, 10 }, minMaj7 { 0, 3, 7, 11 }, min6 { 0, 3, 7, 9 }, dom7 { 0, 4, 7, 10 };
} // namespace q

struct ChordDef
{
    int root;                // semitones above the tonic
    std::vector<int> iv;     // intervals above the root
    int bass = -1;           // semitones above the tonic, -1 = root position
};

const std::vector<ChordDef>& fixedProgression (Progression p)
{
    static const std::vector<ChordDef> table[] = {
        { { 0, q::min }, { 8, q::maj }, { 3, q::maj }, { 10, q::maj } },                    // epic minor
        { { 0, q::min }, { 1, q::maj }, { 10, q::min }, { 1, q::maj } },                    // phrygian dark
        { { 0, q::min }, { 8, q::maj }, { 5, q::min }, { 7, q::dom7 } },                    // harmonic dominant
        { { 0, q::min }, { 7, q::min, 10 }, { 5, q::min, 8 }, { 7, q::maj } },              // lament bass
        { { 0, q::min }, { 8, q::min }, { 0, q::min }, { 4, q::min } },                     // mediant chain
        { { 0, q::min }, { 6, q::min }, { 1, q::maj }, { 7, q::maj } },                     // tritone abyss
        { { 0, q::min }, { 1, q::maj, 0 }, { 10, q::maj, 0 }, { 8, q::maj, 0 } },           // tonic pedal
        { { 0, q::min }, { 0, q::minMaj7 }, { 0, q::min7 }, { 0, q::min6 } },               // line cliche
        { { 0, q::min }, { 1, q::maj, 5 }, { 7, q::dom7 }, { 0, q::min } },                 // neapolitan
        { { 0, q::min }, { 10, q::maj }, { 8, q::maj }, { 7, q::maj } },                    // andalusian
    };
    return table[(size_t) std::clamp ((int) p, 0, (int) Progression::andalusian)];
}

// Triad on a scale degree (stacked scale thirds), as a chord relative to the tonic.
ChordDef diatonicTriad (scales::Scale s, int degree)
{
    const auto at = [s] (int d) { return scales::degreeToPitch (0, s, scales::mapDegree (s, d)); };
    const int root = at (degree);
    return { mod (root, 12), { 0, at (degree + 2) - root, at (degree + 4) - root } };
}

template <typename T>
const T& pickWeighted (Rng& rng, const std::vector<T>& items, const std::vector<float>& weights)
{
    float total = 0.0f;
    for (float w : weights)
        total += w;
    float r = rng.uniform() * total;
    for (size_t i = 0; i < items.size(); ++i)
    {
        r -= weights[i];
        if (r <= 0.0f)
            return items[i];
    }
    return items.back();
}

// Harmonic functions of a minor-key phrase.
enum class Fn { tonic, pre, dom };

// A seeded progression that follows functional grammar: every phrase starts on the tonic (or a substitute), moves
// through predominant chords and ends on a dominant, so the loop always leads home. From 8 chords on the loop is
// two phrases (a half cadence in the middle). Darkness weighs the chromatic chords (bII, bvi, #iv, vii dim, V7...)
// against the diatonic ones, and some chords are inverted so the bass walks by step.
std::vector<ChordDef> autoProgression (const HarmonyParams& p, int count)
{
    Rng rng ((uint64_t) p.seed * 7727ull + 11ull);
    const float bright = 1.0f - 0.7f * p.darkness, dark = 0.1f + 1.3f * p.darkness;

    auto pool = [&] (Fn f, std::vector<ChordDef>& chords, std::vector<float>& weights)
    {
        chords.clear();
        weights.clear();
        auto diatonic = [&] (int degree, float w)
        {
            const auto c = diatonicTriad (p.scale, degree);
            if (c.iv[2] == 7 || c.iv[2] == 6) // real triads only (pentatonic stacks can be odd)
            {
                chords.push_back (c);
                weights.push_back (w * bright);
            }
        };
        auto chromatic = [&] (ChordDef c, float w)
        {
            chords.push_back (std::move (c));
            weights.push_back (w * dark);
        };
        switch (f)
        {
            case Fn::tonic: // i, bIII, bVI; bvi (chromatic mediant), i(maj7)
                diatonic (0, 1.0f); diatonic (2, 1.0f); diatonic (5, 0.8f);
                chromatic ({ 8, q::min }, 0.8f); chromatic ({ 0, q::minMaj7 }, 0.4f);
                break;
            case Fn::pre:   // iv, bVI, ii; bII (neapolitan), iv6, bvi
                diatonic (3, 1.2f); diatonic (5, 1.0f); diatonic (1, 0.6f);
                chromatic ({ 1, q::maj }, 1.0f); chromatic ({ 5, q::min6 }, 0.4f); chromatic ({ 8, q::min }, 0.5f);
                break;
            case Fn::dom:   // v, bVII; V7, vii dim, bII (phrygian cadence), #iv (tritone), iii (mediant)
                diatonic (4, 1.0f); diatonic (6, 1.0f);
                chromatic ({ 7, q::dom7 }, 1.2f); chromatic ({ 11, q::dim }, 0.5f); chromatic ({ 1, q::maj }, 0.6f);
                chromatic ({ 6, q::min }, 0.6f); chromatic ({ 4, q::min }, 0.5f);
                break;
        }
    };

    const int phraseLen = count >= 8 ? count / 2 : count;
    std::vector<ChordDef> out;
    std::vector<ChordDef> chords;
    std::vector<float> weights;
    for (int i = 0; i < count; ++i)
    {
        if (i == 0)
        {
            out.push_back (diatonicTriad (p.scale, 0));
            continue;
        }
        const int k = i % phraseLen;
        Fn f = rng.chance (0.5f) ? Fn::tonic : Fn::pre;
        if (k == 0)
            f = Fn::tonic;
        else if (k == phraseLen - 1)
            f = Fn::dom;
        else if (k == phraseLen - 2)
            f = Fn::pre;

        pool (f, chords, weights);
        for (size_t c = 0; c < chords.size(); ++c) // never the previous chord again
            if (chords[c].root == out.back().root && chords[c].iv == out.back().iv)
                weights[c] = 0.0f;
        out.push_back (pickWeighted (rng, chords, weights));
    }

    // Bass line: where the root would leap, an inversion may let the bass move by step instead.
    int bass = 0;
    for (size_t i = 1; i < out.size(); ++i)
    {
        auto& c = out[i];
        auto distance = [bass] (int pc) { const int d = mod (pc - bass, 12); return std::min (d, 12 - d); };
        int best = c.root;
        for (int iv : c.iv)
            if (distance (mod (c.root + iv, 12)) < distance (best))
                best = mod (c.root + iv, 12);
        if (distance (c.root) > 2 && distance (best) <= 2 && rng.chance (0.3f + 0.4f * p.darkness))
            c.bass = best;
        bass = c.bass >= 0 ? c.bass : c.root;
    }
    return out;
}

} // namespace

std::vector<Region> generateProgression (const HarmonyParams& p)
{
    const double total = std::clamp (p.bars, 1, 16) * 4.0;
    const double len = chordBeats (p.chordLength);
    const int count = std::max (1, (int) std::ceil (total / len - 1.0e-9));

    const auto base = p.progression == Progression::autoSeed ? autoProgression (p, count) : fixedProgression (p.progression);
    const int tonic = p.key + 48; // around C3..B3: the voicing places everything anyway

    // The chord of every slot: the progression, repeated.
    std::vector<ChordDef> defs;
    for (int i = 0; i < count; ++i)
        defs.push_back (base[(size_t) i % base.size()]);

    std::vector<Region> out;
    for (int i = 0; i < count; ++i)
    {
        const auto& d = defs[(size_t) i];
        Region r;
        r.start = i * len;
        r.length = std::min (len, total - r.start);
        const int root = tonic + d.root;
        for (int iv : d.iv)
            r.pitches.push_back (root + iv);
        r.bassPc = d.bass >= 0 ? mod (p.key + d.bass, 12) : -1;
        out.push_back (r);
    }
    return out;
}

void colourRegions (std::vector<Region>& regions, float tension, float darkness, int seed)
{
    if (tension <= 0.0f)
        return;

    for (auto& r : regions)
    {
        if (r.pitches.empty())
            continue;
        const int root = r.pitches.front();

        // Seeded by the chord itself: a chord that comes back gets the same colours (the loop sounds like one).
        uint64_t id = mixSeed ((uint64_t) seed * 4099ull + 3ull, (uint64_t) (r.bassPc + 1));
        id = mixSeed (id, (uint64_t) mod (root, 12));
        for (int x : r.pitches)
            id = mixSeed (id, (uint64_t) (x - root + 64));
        Rng rng (id);
        std::set<int> rel;
        for (int x : r.pitches)
            rel.insert (mod (x - root, 12));

        const bool minor = rel.count (3) != 0 && rel.count (4) == 0;
        const bool major = rel.count (4) != 0 && rel.count (3) == 0;

        std::vector<int> bright, dark;
        if (minor)      { bright = { 2, 5, 10 }; dark = { 11, 1, 8, 6 }; }
        else if (major) { bright = { 2, 11, 9 }; dark = { 1, 8, 10, 6 }; }
        else            { bright = { 2, 10 };    dark = { 1, 11 }; }

        const int count = std::clamp ((int) std::floor (tension * 2.6f + rng.uniform() * 0.8f - 0.4f), 0, 3);
        for (int k = 0; k < count; ++k)
        {
            auto& pool = rng.chance (darkness) ? dark : bright;
            std::vector<int> free;
            for (int c : pool)
                if (rel.count (c) == 0)
                    free.push_back (c);
            if (free.empty())
                continue;
            const int c = free[(size_t) rng.range (0, (int) free.size() - 1)];
            rel.insert (c);
            r.pitches.push_back (root + 12 + c);
        }
    }
}

std::string chordSymbol (const Region& r)
{
    if (r.pitches.empty())
        return {};

    const int root = mod (r.pitches.front(), 12);
    std::set<int> rel;
    for (int x : r.pitches)
        rel.insert (mod (x - root, 12));
    auto has = [&] (int i) { return rel.count (i) != 0; };

    std::string s = scales::keyNames[root];
    std::vector<std::string> ext;
    const bool m3 = has (3), M3 = has (4);

    if (m3 && has (6) && ! has (7))
        s += has (10) ? "m7b5" : (has (9) ? "dim7" : "dim");
    else if (M3 && has (8) && ! has (7))
        s += "+";
    else if (m3 && ! M3)
    {
        s += "m";
        if (has (11))      s += "(maj7)";
        else if (has (10)) s += "7";
        else if (has (9))  s += "6";
    }
    else if (M3)
    {
        if (has (11))      s += "maj7";
        else if (has (10)) s += "7";
        else if (has (9))  s += "6";
        if (m3)            ext.push_back ("#9");
    }
    else if (has (5))      s += "sus4";
    else if (has (2))      s += "sus2";
    else if (has (7))      s += "5";

    const bool third = m3 || M3;
    if (has (1))                          ext.push_back ("b9");
    if (has (2) && third)                 ext.push_back ("add9");
    if (has (5) && third)                 ext.push_back ("11");
    if (has (6) && has (7))               ext.push_back ("#11");
    if (has (8) && has (7))               ext.push_back ("b13");

    if (! ext.empty())
    {
        s += "(";
        for (size_t i = 0; i < ext.size(); ++i)
            s += (i > 0 ? "," : "") + ext[i];
        s += ")";
    }

    if (r.bassPc >= 0 && r.bassPc != root)
        s += std::string ("/") + scales::keyNames[r.bassPc];
    return s;
}

} // namespace dmpe
