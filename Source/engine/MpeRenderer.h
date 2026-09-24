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

} // namespace dmpe
