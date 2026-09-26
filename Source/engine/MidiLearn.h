#pragma once

#include "model/Phrase.h"
#include "engine/HarmonyEngine.h"
#include "engine/MelodyGenerator.h"
#include "engine/Scales.h"

#include <array>
#include <string>
#include <vector>

namespace dmpe
{

// MIDI Learn: LEARN listens to 4 bars of the clip on the plugin's track and understands one thing, the one the
// plugin asks about before it listens.
enum class LearnKind
{
    chords, // chords (block, broken, with a melody on top) -> Harmony = MIDI In
    bass,   // a bass line: one root per half bar, the scale's chord on it -> Harmony = MIDI In
    lead,   // a melody -> Motif = Your Lead
    rhythm, // the onsets of anything (drums too) -> Motif = Your Rhythm
    count
};

inline const char* const learnKindNames[] = { "Chords", "Bass", "Lead", "Rhythm" };

constexpr double learnBeats = 16.0; // LEARN always listens to 4 bars

struct KeyGuess
{
    int key = 9;
    scales::Scale scale = scales::Scale::naturalMinor;
    bool found = false;
};

// What points at a key: note weights (by length), root weights (chord roots, bass notes on strong beats), the
// first / last root or note and the lowest note (pitch classes, -1 = none).
struct KeyEvidence
{
    std::array<float, 12> notes {};
    std::array<float, 12> roots {};
    int first = -1, last = -1, lowest = -1;
};

// Among the plugin's scales on every tonic: the ones that hold the notes best, then the tonic the roots point
// at. When the clip does not tell (a 5-note melody fits several scales) the current key and scale are kept.
KeyGuess detectKey (const KeyEvidence& evidence, int currentKey, scales::Scale currentScale);

// The chords of a clip: a chord template (triads, sevenths, sus, power chords) for every stretch of half beats,
// found by dynamic programming (a change must be worth it). Block chords, broken chords and a melody on top
// all work; the lowest note gives slash chords (Em/G). Regions are root first, around C3..B3.
std::vector<Region> chordsFromClip (const Phrase& clip, double length);

// The roots a bass line implies: the strongest note of every half bar (length, and a note on the half bar's
// downbeat counts double), equal neighbours merged. {start, length, pitch class}.
struct BassRoot { double start = 0.0; double length = 2.0; int pc = 0; };
std::vector<BassRoot> bassRoots (const Phrase& clip, double length);

// The chord of the scale on each root (a major triad on a root outside the scale).
std::vector<Region> chordsOnRoots (const std::vector<BassRoot>& roots, int key, scales::Scale scale);

// The grid a line sits on: 1/16, 1/16 triplets or 1/32.
Rate detectRate (const std::vector<double>& onsets);

struct LearnResult
{
    LearnKind kind = LearnKind::chords;
    bool ok = false;
    std::string message;              // what was understood (or why nothing)
    KeyGuess key;
    Rate rate = Rate::sixteenth;      // lead, rhythm
    std::vector<Region> chords;       // chords, bass
    std::vector<LearnedEvent> events; // lead (one line, the top note), rhythm (onsets, pitch -1)
};

// `clip`: the notes heard, in beats of the 4-bar loop (0..learnBeats).
LearnResult analyseClip (LearnKind kind, const Phrase& clip, int currentKey, scales::Scale currentScale);

} // namespace dmpe
