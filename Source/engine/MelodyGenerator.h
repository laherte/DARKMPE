#pragma once

#include "model/Phrase.h"
#include "engine/HarmonyEngine.h"
#include "engine/LeadHarmony.h"
#include "engine/PhraseForm.h"
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

// The lead's step grid.
enum class Rate { quarter, eighth, eighthTriplet, sixteenth, sixteenthTriplet, thirtySecond, count };
inline const char* const rateNames[] = { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
int stepsPerBar (Rate r); // 4, 8, 12, 16, 24, 32

// Riff: the style generators (rhythmic riffs). Voice: syllables with vowel, pitch and loudness contours,
// a synth that almost talks (see VoiceGenerator).
enum class LeadEngine { riff, voice, count };
inline const char* const leadEngineNames[] = { "Riff", "Voice" };

enum class VoiceStyle
{
    prophet, // declaimed on a reciting tone, the sentence falls (or rises: a question) at the end
    lament,  // sighs: a stressed note falling a step, the whole line sinking
    titan,   // few syllables, a big leap up, long vowels that fall off: epic
    talkbox, // syllabic riff, strong vowel "wah / yeah" on every note
    count
};
inline const char* const voiceStyleNames[] = { "Prophet", "Lament", "Titan", "Talkbox" };

struct GenParams
{
    int key = 9;                            // A
    scales::Scale scale = scales::Scale::phrygian;
    Style style = Style::pursuit;
    int bars = 4;
    float density = 0.7f;  // 0..1 onsets per bar
    float octave = 0.3f;   // probability of octave jumps
    float pedal = 0.5f;    // probability of returning to the root of the chord (Voice: of staying on the reciting tone)
    float chroma = 0.15f;  // probability of approach notes (a semitone; a scale step when scaleLock is on)
    float slide = 0.3f;    // probability that a contiguous note glides in
    float gate = 0.6f;     // 0.1 staccato .. 1 legato
    float swing = 0.0f;    // 0..1 (binary grids)
    int baseOctave = 3;    // octave of the root pedal (MIDI octave, C3 = 48)
    int rangeOctaves = 2;
    int seed = 1;
    int variation = 0;     // bumps the later-bar mutations without touching the core motif
    Form form = Form::classic; // phrase structure: one section per bar (A stays identical, MUTATE varies the answers)

    Rate rate = Rate::sixteenth;
    float longNotes = 0.0f; // 0..1: notes that hold over the next steps (8ths, dotted 8ths, quarters, halves)

    // The chords under the lead (and the KIT). Progression uses the CINEMATIC progression settings below.
    HarmonySource harmony = HarmonySource::style;
    Progression progression = Progression::epicMinor;
    ChordLength chordLength = ChordLength::oneBar;
    float darkness = 0.5f;
    bool scaleLock = true; // every note in the key scale (chords included)

    LeadEngine engine = LeadEngine::riff;
    VoiceStyle voice = VoiceStyle::prophet;
    float vowels = 0.6f;     // Voice: depth of the vowel (CC74) motion
    float inflection = 0.5f; // Voice: pitch contour of speech: scoops, falls, question rises
    float growl = 0.2f;      // Voice: rough flutter on the consonants
};

// Scale-degree roots of a style's chord progression, one per bar (degrees written for 7-note scales).
const std::vector<int>& styleProgression (Style s);

// The chords the lead is built on (Style, Progression or Tonic), with Scale Lock applied.
HarmonyTrack leadHarmony (const GenParams& p, SectionMarks* marks = nullptr);

// The Riff engine. Deterministic for a given GenParams. Every random decision is keyed by its place (bar or
// section, position in the bar), so more bars keep the first ones, and more density only adds notes.
Phrase generateMelody (const GenParams& p);

} // namespace dmpe
