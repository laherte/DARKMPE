#pragma once

#include "engine/HarmonyEngine.h"
#include "engine/Scales.h"

#include <vector>

namespace dmpe
{

// What the lead (GENERATE) and the KIT layers are built on: the chord under every beat of the loop.
enum class HarmonySource
{
    style,       // the style's own progression, one chord per bar (i i VI VII for Pursuit...)
    progression, // the CINEMATIC Progression and Chord Length (also 2-beat chords, borrowed and chromatic ones)
    tonic,       // no changes: everything on the tonic chord (for Key Trigger, or over your own chords)
    learned,     // MIDI In: the chords LEARN heard on the plugin's track (kept as played, repeated every 4 bars)
    count
};

inline const char* const harmonySourceNames[] = { "Style", "Progression", "Tonic", "MIDI In" };

// One chord of the loop, and the scale the lines use over it: the key scale for diatonic chords, a related
// scale that contains a borrowed chord (E7 in A minor: A harmonic minor; Fm: C minor), the key scale again
// when Scale Lock is on (the chords themselves are then diatonic).
struct ChordSpan
{
    double start = 0.0;
    double length = 4.0;
    Region chord;           // root first, around C3..B3
    int scaleTonic = 9;     // pitch class of that scale's tonic
    scales::Scale scale = scales::Scale::phrygian;
    int rootDegree = 0;     // the chord root as a 7-note degree of that scale
};

struct HarmonyTrack
{
    int key = 9;
    scales::Scale scale = scales::Scale::phrygian;
    std::vector<ChordSpan> spans;

    bool inKey() const; // every chord's lines use the key scale (no borrowed chord)

    const ChordSpan& at (double beat) const;

    // The note `degree` scale steps above the chord root sounding at `beat`. `tonicPitch` is the key's tonic in
    // the register wanted (a borrowed scale's tonic is folded next to it, so the register stays put).
    int pitch (double beat, int tonicPitch, int degree) const;

    // The chord's bass note (the slash bass of Em/G, the tonic under a pedal chord, else the root) next to the
    // root that pitch (tonicPitch, 0) gives.
    int bass (double beat, int tonicPitch) const;

    // The note `steps` steps of the chord's scale away from `pitch` (diatonic neighbours, approach notes).
    int step (double beat, int pitch, int steps) const;

    bool contains (double beat, int pitch) const; // `pitch` is a note of the scale over the chord at `beat`

    std::vector<Region> chords() const;
};

struct HarmonySpec
{
    HarmonySource source = HarmonySource::style;
    int key = 9;
    scales::Scale scale = scales::Scale::phrygian;
    int bars = 4;
    std::vector<int> styleDegrees { 0 }; // Style: one degree per bar, repeated
    HarmonyParams progression;           // Progression: the CINEMATIC settings (key, scale, bars are taken from here)
    std::vector<Region> learnedChords;   // MIDI In: 4 bars of learned chords (empty: the tonic)
    bool scaleLock = true;               // generated chords (and so every line) stay in the key scale; learned
                                         // chords are kept as played, so the lines never fight your clip
};

// Deterministic for a given spec. `marks` receives the form's sections of a Progression (as generateProgression).
HarmonyTrack buildHarmony (const HarmonySpec& spec, SectionMarks* marks = nullptr);

// Scale Lock for finished regions (colours, reharm): every pitch pulled to the nearest scale note, doubles removed.
void lockRegions (std::vector<Region>& regions, int key, scales::Scale scale);

} // namespace dmpe
