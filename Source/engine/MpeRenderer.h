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

} // namespace dmpe
