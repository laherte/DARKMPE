#pragma once

#include "model/Phrase.h"
#include "engine/CurveShapes.h"
#include "engine/HarmonyEngine.h"
#include "engine/Scales.h"
#include "engine/VoicingEngine.h"

namespace dmpe
{

enum class Motion
{
    morph,       // each voice is one long note that bends from chord to chord
    bloom,       // every chord fans out of a unison point
    collapse,    // every chord folds back into a unison point
    breathe,     // bloom in, collapse out
    deepNote,    // the first chord grows out of a drifting random cluster, then morph
    pulse,       // chords re-struck in a euclidean 16th pattern, sliding on the changes
    tensionRise, // voices stretch apart (top up, bass down) towards every change
    count
};
inline const char* const motionNames[] = { "Morph", "Bloom", "Collapse", "Breathe", "Deep Note", "Pulse", "Tension Rise" };

enum class Reharm
{
    off,
    susResolve,        // every chord first appears as sus4, the 4th bends down into the 3rd
    chromaticApproach, // the last beat of a chord becomes the next chord a semitone up, then slides down into it
    mediantShift,      // second half of every chord moves to a chromatic mediant (+-4 semitones)
    suspensions,       // upper voices enter a step above (4-3, 9-8, b6-5) and resolve with a bend
    tonicPedal,        // the bass stays on the tonic under every chord
    planing,           // every chord takes the shape of the first one (parallel harmony)
    tritoneApproach,   // the last beat of a chord becomes the next chord a tritone away, then slides into it
    count
};
inline const char* const reharmNames[] = { "Off", "Sus Resolve", "Chromatic Approach", "Mediant Shift",
                                           "Suspensions", "Tonic Pedal", "Planing", "Tritone Approach" };


struct CineParams
{
    Motion motion = Motion::morph;
    Reharm reharm = Reharm::susResolve;
    VoicingMode voicing = VoicingMode::epicSpread;
    GlideShape shape = GlideShape::linear;
    int voices = 6;            // 2..8
    int lowPitch = 33;         // A1
    int highPitch = 88;        // E6
    float glide = 0.5f;        // transition length as a fraction of the shorter neighbouring chord
    float anticipation = 0.5f; // 0 = move after the change, 1 = arrive exactly on the change
    float stagger = 0.4f;      // voices move one after another
    float swell = 0.7f;        // pressure / timbre swell per chord
    float tension = 0.0f;      // colour tones added to every chord (0 = keep the chords)
    float darkness = 0.5f;     // which colours: bright 9/11 .. dark b9/b13/m(maj7)
    float pulse = 0.5f;        // Pulse: hits per bar (4..16)
    float arc = 0.0f;          // phrase-long crescendo towards the last bar
    float fall = 0.0f;         // pitch falls at the end of chords
    bool sub = false;          // extra voice an octave under the bass
    int key = 9;               // Stepped glides walk through this key/scale
    scales::Scale scale = scales::Scale::naturalMinor;
    int seed = 1;
};

int estimateRoot (const std::vector<int>& pitches); // pitch class 0..11

// One region per chord of the input (legato), root first.
std::vector<Region> regionsFromPhrase (const Phrase& input, double lengthBeats);

// Re-harmonisation of regions (splits, pedal, planing). `tonicPc` is the pedal note for Tonic Pedal.
std::vector<Region> reharmonise (std::vector<Region> regions, Reharm reharm, int tonicPc);

// regionsFromPhrase + reharmonise (tonic = root of the first chord).
std::vector<Region> buildRegions (const Phrase& input, Reharm reharm, double lengthBeats);

// Fallback when nothing is loaded: i - VI - III - VII in the given key/scale, one bar each.
Phrase demoProgression (int key, scales::Scale scale, int bars);

// The cinematic transformation of loaded chords. Notes come out with finished bend/slide/pressure curves (lockedExpr).
Phrase cinematic (const Phrase& input, const CineParams& p, std::vector<Region>* usedRegions = nullptr);

// The same from ready-made regions (e.g. generateProgression). Colour and reharm are applied here;
// `usedRegions` receives the chords actually played (for chord symbols in the UI).
Phrase cinematicRegions (std::vector<Region> regions, double lengthBeats, const CineParams& p, int tonicPc,
                         std::vector<Region>* usedRegions = nullptr);

} // namespace dmpe
