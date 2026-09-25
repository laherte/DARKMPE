#pragma once

#include "engine/Scales.h"

#include <string>
#include <utility>
#include <vector>

namespace dmpe
{

// A sustained chord area of the cinematic output.
struct Region
{
    double start = 0.0;
    double length = 4.0;
    std::vector<int> pitches; // root first, then the other tones
    int bassPc = -1;          // pitch class under the chord (inversion / pedal), -1 = the root
};

enum class Progression
{
    epicMinor,        // i  bVI  bIII  bVII
    phrygianDark,     // i  bII  bvii  bII
    harmonicDominant, // i  bVI  iv  V7   (raised leading tone)
    lamentBass,       // i  v/b7  iv/b6  V  (descending tetrachord in the bass)
    mediantChain,     // i  bvi  i  iii   (chromatic mediants, all minor)
    tritoneAbyss,     // i  #iv  bII  V
    tonicPedal,       // i  bII/1  bVII/1  bVI/1
    lineCliche,       // i  i(maj7)  i7  i6
    neapolitan,       // i  bII6  V7  i
    andalusian,       // i  bVII  bVI  V
    autoSeed,         // seeded walk over dark harmonic moves (Darkness = how chromatic)
    count
};

inline const char* const progressionNames[] = {
    "Epic Minor", "Phrygian Dark", "Harmonic Dominant", "Lament Bass", "Mediant Chain", "Tritone Abyss",
    "Tonic Pedal", "Line Cliche", "Neapolitan", "Andalusian Dark", "Auto (Seed)"
};

enum class ChordLength { twoBeats, oneBar, twoBars, count };
inline const char* const chordLengthNames[] = { "2 Beats", "1 Bar", "2 Bars" };
inline double chordBeats (ChordLength c) { return c == ChordLength::twoBeats ? 2.0 : (c == ChordLength::twoBars ? 8.0 : 4.0); }

struct HarmonyParams
{
    int key = 9; // A
    scales::Scale scale = scales::Scale::naturalMinor;
    Progression progression = Progression::epicMinor;
    ChordLength chordLength = ChordLength::oneBar;
    int bars = 4;
    float darkness = 0.5f;
    int seed = 1;
};

// The chords of a progression, one region per chord, filling `bars` bars (the progression repeats).
// Phrase forms never touch the chords: they shape the melodic lines only.
std::vector<Region> generateProgression (const HarmonyParams& p);

// Adds colour tones: tension = how many per chord (0 = none), darkness = which ones
// (0: 9 / 11 / 6 / maj7 ... 1: b9 / b13 / m(maj7) / #11). Deterministic for a seed.
void colourRegions (std::vector<Region>& regions, float tension, float darkness, int seed);

// Chord symbol, e.g. "Am(maj7)", "F(add9)", "Bb/A", "Esus4".
std::string chordSymbol (const Region& r);

} // namespace dmpe
