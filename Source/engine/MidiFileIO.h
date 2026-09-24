#pragma once

#include "model/Phrase.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace dmpe
{

struct LoadInfo
{
    bool mpe = false;          // notes spread over member channels / MCM found
    int channels = 0;          // distinct channels carrying notes
    int notes = 0;
    int notesWithExpression = 0;
    int bendRange = 48;        // semitones used to decode per-note pitch bend
};

// Reads every note of every track into a phrase. Notes are paired per channel (so MPE unisons survive)
// and per-channel pitch bend / CC74 / channel pressure / poly aftertouch become per-note curves.
// Length is rounded up to whole bars.
bool loadMidiFile (const juce::File& file, Phrase& out, juce::String& error, LoadInfo* info = nullptr);
bool loadMidiStream (juce::InputStream& in, Phrase& out, juce::String& error, LoadInfo* info = nullptr);

// Same decoding for a sequence whose timestamps are already in beats (e.g. captured input).
Phrase phraseFromBeatSequence (const juce::MidiMessageSequence& seq, LoadInfo* info = nullptr);

// Converts a beat-timed sequence to a Type-1 MIDI file (960 PPQ).
juce::MidiFile makeMidiFile (const juce::MidiMessageSequence& beatSeq, double bpm, const juce::String& trackName);
bool writeMidiFile (const juce::MidiFile& midi, const juce::File& file);

constexpr int filePpq = 960;

} // namespace dmpe
