#pragma once

#include "model/Phrase.h"

#include <vector>

namespace dmpe
{

enum class VoicingMode
{
    asPlayed,
    drop2,
    openSpread,
    darkCluster,
    quartal,
    powerOctave,
    add9_11,
    unisonStack,
    epicSpread,
    count
};

inline const char* const voicingNames[] = {
    "As Played", "Drop 2", "Open Spread", "Dark Cluster", "Quartal", "Power + Oct", "Add 9+11", "Unison Stack", "Epic Spread"
};

struct VoicingParams
{
    VoicingMode mode = VoicingMode::openSpread;
    int voices = 4;           // 2..6
    int lowPitch = 40;        // E2
    int highPitch = 84;       // C6
    bool bassAnchor = true;   // lowest voice stays on the chord root
    bool voiceLeading = true; // true = minimal motion, false = keep the voicing shape
    bool glide = true;        // voices slide from their previous pitch
    float glideProb = 1.0f;   // for melodies: probability a legato note slides
    double strum = 0.0;       // beats between voices
    bool strumDown = false;
};

struct Chord
{
    double start = 0.0;
    double length = 1.0;
    std::vector<int> pitches; // sorted ascending
    float velocity = 0.8f;
    std::vector<Note> sources; // the input notes of this chord (carry imported MPE expression)
};

// Groups notes whose onsets fall within `window` beats of the chord's first note (played chords are rarely tight).
std::vector<Chord> detectChords (const Phrase& input, double window = 0.1);

// True for a single line, tolerating the overlaps a legato MPE player leaves between notes.
bool isMonophonic (const Phrase& input);

// Voices one chord into slots (index = voice). With `prev`, slots move minimally from the previous chord.
std::vector<int> voiceChordSlots (const Chord& chord, const VoicingParams& p, const std::vector<int>* prev);

// Voices chords (or thickens a melody) and fills glideFrom / voiceIndex metadata for ExpressionShaper.
Phrase applyVoicing (const Phrase& input, const VoicingParams& p);

} // namespace dmpe
