#pragma once

#include "model/Phrase.h"

namespace dmpe
{

struct ExprParams
{
    float glideTime = 0.12f;     // beats for a slide to land
    float glideCurve = 0.6f;     // 0 = linear, 1 = fast-then-settle
    float detuneCents = 8.0f;    // spread between stacked voices (+ tiny analog drift)
    float vibratoDepth = 0.25f;  // semitones
    float vibratoRate = 1.5f;    // cycles per beat
    float vibratoDelay = 0.5f;   // beats before vibrato fades in
    float slideAmount = 0.6f;    // CC74 opening over the note
    float slideSpread = 0.4f;    // CC74 offset by register / voice
    float pressureAmount = 0.7f; // pressure swell
    float breath = 0.2f;         // slow pressure LFO
    bool keepInput = true;       // imported MPE curves are kept instead of generated ones
    int seed = 1;
};

// Replaces bend / slide / pressure curves of every note. When keepInput is set, curves that came from an
// imported performance are kept (detune and glide-in are still layered on the pitch).
void shapeExpression (Phrase& phrase, const ExprParams& p);

constexpr double curveResolution = 1.0 / 32.0; // beats between curve points

} // namespace dmpe
