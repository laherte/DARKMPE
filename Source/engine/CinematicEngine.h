#pragma once

#include "model/Phrase.h"
#include "engine/Scales.h"
#include "engine/VoicingEngine.h"

namespace dmpe
{

enum class Motion
{
    morph,    // each voice is one long note that bends from chord to chord
    bloom,    // every chord fans out of a unison point
    collapse, // every chord folds back into a unison point
    breathe,  // bloom in, collapse out
    count
};
inline const char* const motionNames[] = { "Morph", "Bloom", "Collapse", "Breathe" };

enum class Reharm
{
    off,
    susResolve,        // every chord first appears as sus4, the 4th bends down into the 3rd
    chromaticApproach, // the last beat of a chord becomes the next chord a semitone up, then slides down into it
    mediantShift,      // second half of every chord moves to a chromatic mediant (+-4 semitones)
    count
};
inline const char* const reharmNames[] = { "Off", "Sus Resolve", "Chromatic Approach", "Mediant Shift" };

enum class GlideShape { linear, ease, swoopIn, swoopOut, count };
inline const char* const glideShapeNames[] = { "Linear", "Ease", "Swoop In", "Swoop Out" };

struct CineParams
{
    Motion motion = Motion::morph;
    Reharm reharm = Reharm::susResolve;
    VoicingMode voicing = VoicingMode::epicSpread;
    GlideShape shape = GlideShape::linear;
    int voices = 6;
    int lowPitch = 33;         // A1
    int highPitch = 88;        // E6
    float glide = 0.5f;        // transition length as a fraction of the shorter neighbouring chord
    float anticipation = 0.5f; // 0 = move after the change, 1 = arrive exactly on the change
    float stagger = 0.4f;      // voices move one after another
    float swell = 0.7f;        // pressure / timbre swell per chord
    int seed = 1;
};

// A sustained chord area of the output.
struct Region
{
    double start = 0.0;
    double length = 4.0;
    std::vector<int> pitches; // root first, then the other tones
};

int estimateRoot (const std::vector<int>& pitches); // pitch class 0..11

// Chords of the input (one region per chord, legato), after optional re-harmonisation.
std::vector<Region> buildRegions (const Phrase& input, Reharm reharm, double lengthBeats);

// Fallback when nothing is loaded: i - VI - III - VII in the given key/scale, one bar each.
Phrase demoProgression (int key, scales::Scale scale, int bars);

// The cinematic transformation. Notes come out with finished bend/slide/pressure curves (lockedExpr).
Phrase cinematic (const Phrase& input, const CineParams& p);

} // namespace dmpe
