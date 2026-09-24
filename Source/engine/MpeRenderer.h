#pragma once

#include "model/Phrase.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace dmpe
{

struct RenderOptions
{
    int pitchBendRange = 48;       // per-note range, MPE default
    int firstMemberChannel = 2;    // lower zone: master = 1, members 2..16
    int lastMemberChannel = 16;
    bool includeZoneConfig = true; // MCM + RPN pitch-bend range at t = 0
};

// Renders a phrase into an MPE MIDI sequence. Timestamps are in beats.
juce::MidiMessageSequence renderMpe (const Phrase& phrase, const RenderOptions& opts = {});

int bendToMidi (float semitones, int range);

// Renders a single line on MIDI channel 1 with channel pitch bend (for synths without MPE, and for DAWs that
// import .mid files without MPE: the glides survive as the clip's pitch-bend envelope). Overlapping notes are
// cut where the next one starts; a glide ties legato into the next note (note-on before the note-off).
// Bends beyond the range are clamped. `includeSetup` adds the pitch-bend range (RPN 0) at t = 0.
juce::MidiMessageSequence renderMono (const Phrase& phrase, int pitchBendRange, bool includeSetup);

// A short MIDI message at a beat position: what the audio thread plays (no allocation, binary-searchable).
struct PlayEvent
{
    double beat = 0.0;
    juce::uint8 data[3] {};
    juce::uint8 size = 0;
};

// Channel-voice messages of a beat-timed sequence, in order (meta and sysex events are dropped).
std::vector<PlayEvent> toPlayEvents (const juce::MidiMessageSequence& seq);

// MPE lower-zone configuration (MCM + member pitch-bend range), sent when playback starts.
std::vector<PlayEvent> zoneConfigEvents (int pitchBendRange, int memberChannels = 15);

// Pitch-bend range (RPN 0) on channel 1, for the mono output.
std::vector<PlayEvent> monoSetupEvents (int pitchBendRange);

} // namespace dmpe
