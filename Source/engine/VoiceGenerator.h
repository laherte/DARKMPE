#pragma once

#include "engine/ExpressionShaper.h"
#include "engine/MelodyGenerator.h"

namespace dmpe
{

// The Voice engine: a lead that almost talks. Every bar is a sentence of syllables grouped in words, with
// stressed syllables longer and higher, a reciting tone, and a cadence that falls (statement), rises
// (question) or peaks (exclamation). Each syllable is a note with its own
//   - vowel on CC74 (Slide): a consonant onset (closed "m/b", flick "d/t", "y" glide, "r/l"), the vowel,
//     sometimes a diphthong ("ai", "au", "ou"), a mouth that closes at the end of a word;
//   - loudness on Pressure: consonant attack, stressed peak, words separated by dips;
//   - pitch on the per-note bend: speech scoops, portamento between syllables, growl on the consonants,
//     vibrato on long vowels, falls and rises at the end of the sentence.
// Map CC74 to a formant / vowel filter (or wavetable position) and Pressure to level or drive on the synth.
// Every note stays in the scale over its chord (the inflections are bends). Deterministic for a given input.
Phrase generateVoice (const GenParams& p, const ExprParams& e);

// The lead of the selected engine (GENERATE, and the KIT's Lead layer).
Phrase generateLead (const GenParams& p, const ExprParams& e);

} // namespace dmpe
