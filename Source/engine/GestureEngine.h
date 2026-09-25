#pragma once

#include "model/Phrase.h"
#include "engine/MelodyGenerator.h"
#include "engine/Scales.h"

namespace dmpe
{

// MPE gestures: what a note does with its pitch, timbre and pressure beyond landing on its note. A profile weighs
// a vocabulary of gestures; the phrase form makes repeated sections repeat their gestures.
enum class GestureProfile { classic, liquid, vocal, acid, aggressive, glitch, autoStyle, count };
inline const char* const gestureProfileNames[] = { "Classic", "Liquid", "Vocal", "Acid", "Aggressive", "Glitch", "Auto" };

enum class Gesture
{
    none,
    dip,       // bends down a scale step (1 or 2 semitones) and comes back
    lift,      // bends up a scale step and comes back
    scoop,     // enters from below (from above in an answer)
    fall,      // drops a few scale steps at the end (an octave with full Depth)
    approach,  // leans part of the way towards the next note before it arrives
    overshoot, // the glide-in goes past the note and settles
    stepGlide, // the glide-in walks through the scale notes
    trill,     // alternates with the upper neighbour, by bend
    wobble,    // tempo-synced pitch LFO with a matching timbre sweep
    dive,      // swoops down (an octave in the bass)
    riff,      // several short notes played as one note bent through their pitches (Bend Riff)
    rip,       // chords: the voices rip up into the chord one after another
    count
};
inline const char* const gestureNames[] = { "-", "Dip", "Lift", "Scoop", "Fall", "Approach", "Overshoot", "Stepped Glide",
                                            "Trill", "Wobble", "Dive", "Bend Riff", "Rip" };

// Timbre / pressure layer, chosen independently of the pitch gesture.
enum class Articulation { none, pluck, wah, steps, tremolo, swell, count };

enum class GestureRole
{
    lead,  // any gesture, Bend Riffs from short runs
    bass,  // scoops, falls, dives, overshoots, octave riffs
    arp,   // riffs, dips, plucks
    chord  // every voice of a chord does the same gesture, staggered (stabs, transformed chords)
};

struct GestureParams
{
    GestureProfile profile = GestureProfile::autoStyle;
    float amount = 0.5f;     // share of notes that get a gesture
    float depth = 0.4f;      // largest excursion: 0 = 1 semitone .. 1 = 12 semitones
    float riff = 0.35f;      // how often a short run becomes one Bend Riff note
    float glideTime = 0.12f; // beats, for the authored glides (overshoot, stepped)
    int key = 9;
    scales::Scale scale = scales::Scale::phrygian;
    Style style = Style::pursuit; // Auto picks the profile from the style
    int seed = 1;
};

// Auto -> the profile that suits the style; any other profile is returned as is.
GestureProfile resolveProfile (GestureProfile p, Style s);

// Largest gesture excursion in semitones for a Depth setting.
float gestureDepthSemitones (float depth);

// Adds gestures to a generated phrase: merges short runs into Bend Riffs, writes the gesture curves and sets
// Note::gestureKind. Deterministic; notes with the same exprSeed get the same gesture, so a repeated section
// repeats its gestures (answers mirror them: dips become lifts). Classic changes nothing.
void applyGestures (Phrase& phrase, const GestureParams& p, GestureRole role);

} // namespace dmpe
