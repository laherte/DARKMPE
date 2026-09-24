#pragma once

#include "model/Phrase.h"
#include "engine/Scales.h"

namespace dmpe
{

enum class Style
{
    pursuit,     // root pedal alternating with a moving upper voice, relentless 16ths
    hateOrGlory, // octave-jumping riff
    opr,         // staccato phrygian stabs, i - bII
    darkArp,     // chord-tone arpeggio on i - VI - VII - v
    acidSlide,   // syncopated line with lots of slides
    gallop,      // 8th + two 16ths, root-heavy, driving
    raveStab,    // sparse syncopated stabs, octave jumps on the offbeats
    count
};

inline const char* const styleNames[] = { "Pursuit", "Hate or Glory", "Opr", "Dark Arp", "Acid Slide", "Gallop", "Rave Stab" };

struct GenParams
{
    int key = 9;                            // A
    scales::Scale scale = scales::Scale::phrygian;
    Style style = Style::pursuit;
    int bars = 4;
    float density = 0.7f;  // 0..1 onsets per bar
    float octave = 0.3f;   // probability of octave jumps
    float pedal = 0.5f;    // probability of returning to the root pedal
    float chroma = 0.15f;  // probability of chromatic approach notes
    float slide = 0.3f;    // probability that a contiguous note glides in
    float gate = 0.6f;     // 0.1 staccato .. 1 legato
    float swing = 0.0f;    // 0..1
    int baseOctave = 3;    // octave of the root pedal (MIDI octave, C3 = 48)
    int rangeOctaves = 2;
    int seed = 1;
    int variation = 0;     // bumps the later-bar mutations without touching the core motif
};

// Scale-degree roots of a style's chord progression, one per bar (degrees written for 7-note scales).
const std::vector<int>& styleProgression (Style s);

// Deterministic for a given GenParams.
Phrase generateMelody (const GenParams& p);

} // namespace dmpe
