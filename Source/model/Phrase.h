#pragma once

#include <cstdint>
#include <vector>

namespace dmpe
{

// A point of a per-note expression curve. t is in beats, relative to the note start.
struct CurvePoint
{
    double t = 0.0;
    float v = 0.0f;
};

using Curve = std::vector<CurvePoint>;

// Linear interpolation over a curve sorted by t. Returns def for an empty curve.
float evalCurve (const Curve& c, double t, float def);

// Same interpolation for increasing t, in amortised O(1) per call (renderers walk curves in time order).
struct CurveCursor
{
    CurveCursor (const Curve& c, float def) : curve (c), fallback (def) {}
    float at (double t);

    const Curve& curve;
    float fallback;
    size_t index = 0;
};

struct Note
{
    double start = 0.0;   // beats
    double length = 0.25; // beats
    int pitch = 60;
    float velocity = 0.8f;        // 0..1
    float releaseVelocity = 0.5f; // 0..1

    // Generation metadata consumed by ExpressionShaper.
    int glideFrom = -1;   // pitch this note slides in from, -1 = none
    int voiceIndex = 0;   // voice slot within a chord / unison stack
    int voiceCount = 1;
    bool accent = false;
    bool chromatic = false;
    bool lockedExpr = false;    // curves were authored by an engine (cinematic): the shaper only adds detune
    bool hasSourceExpr = false; // bend/slide/pressure came from an imported (MPE) performance
    bool pedal = false;         // the riff's root pedal (follows the chord), not the moving voice

    // Phrase form: the kind of section the note belongs to (SectionKind), -1 = no form.
    int sectionKind = -1;
    // Seed for everything random about this note after generation (humanize, detune, vibrato phase, gestures).
    // Generators give notes of repeated sections the same seed, so a section comes back identical; 0 = none.
    uint64_t exprSeed = 0;

    // MPE expression (filled by ExpressionShaper).
    Curve bend;     // semitones relative to pitch
    Curve slide;    // CC74 0..1
    Curve pressure; // channel pressure 0..1

    double end() const { return start + length; }
};

struct Phrase
{
    std::vector<Note> notes;
    double lengthBeats = 4.0;

    bool empty() const { return notes.empty(); }
    void sortByStart();
};

} // namespace dmpe
