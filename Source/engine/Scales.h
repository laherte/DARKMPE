#pragma once

#include <array>
#include <cstdlib>
#include <vector>

namespace dmpe::scales
{

enum class Scale
{
    naturalMinor,
    phrygian,
    harmonicMinor,
    phrygianDominant,
    dorian,
    locrian,
    hungarianMinor,
    doubleHarmonic,
    neapolitanMinor,
    aeolianFlat5,
    minorPentatonic,
    count
};

inline const char* const scaleNames[] = {
    "Natural Minor", "Phrygian", "Harmonic Minor", "Phrygian Dominant", "Dorian", "Locrian", "Hungarian Minor",
    "Double Harmonic", "Neapolitan Minor", "Aeolian b5", "Minor Pentatonic"
};

inline const char* const keyNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

inline const std::vector<int>& intervals (Scale s)
{
    static const std::array<std::vector<int>, (size_t) Scale::count> table { {
        { 0, 2, 3, 5, 7, 8, 10 }, // natural minor
        { 0, 1, 3, 5, 7, 8, 10 }, // phrygian
        { 0, 2, 3, 5, 7, 8, 11 }, // harmonic minor
        { 0, 1, 4, 5, 7, 8, 10 }, // phrygian dominant
        { 0, 2, 3, 5, 7, 9, 10 }, // dorian
        { 0, 1, 3, 5, 6, 8, 10 }, // locrian
        { 0, 2, 3, 6, 7, 8, 11 }, // hungarian minor
        { 0, 1, 4, 5, 7, 8, 11 }, // double harmonic
        { 0, 1, 3, 5, 7, 8, 11 }, // neapolitan minor
        { 0, 2, 3, 5, 6, 8, 10 }, // aeolian b5
        { 0, 3, 5, 7, 10 },       // minor pentatonic
    } };
    return table[(size_t) s];
}

inline int floorDiv (int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
inline int mod (int a, int b) { return ((a % b) + b) % b; }

// Scale degree (any integer, 0 = tonic) -> MIDI pitch, with tonic at `tonicPitch`.
inline int degreeToPitch (int tonicPitch, Scale s, int degree)
{
    const auto& iv = intervals (s);
    const int n = (int) iv.size();
    return tonicPitch + 12 * floorDiv (degree, n) + iv[(size_t) mod (degree, n)];
}

// Degrees are written for 7-note scales (progressions, chord tones); on a scale with fewer notes a degree is
// mapped to the nearest note by pitch (natural minor as the reference).
inline int mapDegree (Scale s, int degree7)
{
    const int n = (int) intervals (s).size();
    if (n == 7)
        return degree7;
    const int target = degreeToPitch (0, Scale::naturalMinor, degree7);
    const int approx = floorDiv (degree7 * n, 7);
    int best = approx, bestDistance = 1000;
    for (int d = approx - 2; d <= approx + 2; ++d)
    {
        const int distance = std::abs (degreeToPitch (0, s, d) - target);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = d;
        }
    }
    return best;
}

inline bool inScale (int pitch, int key, Scale s)
{
    const int pc = mod (pitch - key, 12);
    for (int i : intervals (s))
        if (i == pc)
            return true;
    return false;
}

// Semitones from `pitch` to the note `steps` scale steps away (negative = down). A chromatic pitch counts from its
// scale neighbours; the walk stops after an octave.
inline int stepOffset (int pitch, int key, Scale s, int steps)
{
    const int dir = steps < 0 ? -1 : 1;
    int q = pitch, found = 0;
    while (found < std::abs (steps) && std::abs (q - pitch) < 12)
    {
        q += dir;
        if (inScale (q, key, s))
            ++found;
    }
    return q - pitch;
}

} // namespace dmpe::scales
