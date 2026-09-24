#pragma once

#include <array>
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
    count
};

inline const char* const scaleNames[] = {
    "Natural Minor", "Phrygian", "Harmonic Minor", "Phrygian Dominant", "Dorian", "Locrian", "Hungarian Minor"
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

inline bool inScale (int pitch, int key, Scale s)
{
    const int pc = mod (pitch - key, 12);
    for (int i : intervals (s))
        if (i == pc)
            return true;
    return false;
}

} // namespace dmpe::scales
