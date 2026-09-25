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

// A seeded walk: diatonic moves of the scale, plus dark chromatic ones weighted by `darkness`; the last chord
// always leads back to the tonic.
std::vector<ChordDef> autoProgression (const HarmonyParams& p, int count)
{
    Rng rng ((uint64_t) p.seed * 7727ull + 11ull);

    std::vector<ChordDef> diatonic;
    for (int d = 1; d < 7; ++d)
    {
        auto c = diatonicTriad (p.scale, d);
        if (c.iv[2] == 7 || c.iv[2] == 6) // keep real triads (pentatonic stacks can be odd)
            diatonic.push_back (c);
    }
    const std::vector<ChordDef> dark {
        { 1, q::maj },  // bII: phrygian / neapolitan
        { 8, q::min },  // bvi: chromatic mediant
        { 4, q::min },  // iii: chromatic mediant
        { 6, q::min },  // #iv: tritone
        { 11, q::dim }, // vii dim
        { 7, q::dom7 }, // V7 with the leading tone
    };
    const std::vector<ChordDef> cadence { { 7, q::maj }, { 1, q::maj }, { 10, q::maj }, { 5, q::min }, { 8, q::maj } };
    const std::vector<float> cadenceWeights { 1.0f + p.darkness, 0.3f + 1.5f * p.darkness, 1.0f, 1.0f - 0.5f * p.darkness, 0.8f };

    std::vector<ChordDef> out;
    out.push_back (diatonicTriad (p.scale, 0));

    for (int i = 1; i < count; ++i)
    {
        const bool last = i == count - 1 && count > 2;
        std::vector<ChordDef> pool;
        std::vector<float> weights;
        if (last)
        {
            pool = cadence;
            weights = cadenceWeights;
        }
        else
        {
            for (const auto& c : diatonic) { pool.push_back (c); weights.push_back (1.0f - 0.75f * p.darkness); }
            for (const auto& c : dark)     { pool.push_back (c); weights.push_back (0.1f + 1.2f * p.darkness); }
        }

        // Never repeat the previous chord.
        for (size_t k = 0; k < pool.size(); ++k)
            if (pool[k].root == out.back().root && pool[k].iv == out.back().iv)
                weights[k] = 0.0f;
        out.push_back (pickWeighted (rng, pool, weights));
    }
    return out;
}

} // namespace

std::vector<Region> generateProgression (const HarmonyParams& p)
{
    const double total = std::clamp (p.bars, 1, 16) * 4.0;
    const double len = chordBeats (p.chordLength);
    const int count = std::max (1, (int) std::ceil (total / len - 1.0e-9));

    const auto base = p.progression == Progression::autoSeed ? autoProgression (p, std::max (count, 4)) : fixedProgression (p.progression);
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

    Rng rng ((uint64_t) seed * 4099ull + 3ull);
    for (auto& r : regions)
    {
        if (r.pitches.empty())
            continue;
        const int root = r.pitches.front();
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
