#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <utility>
#include <vector>

namespace presets
{

// A preset is the default of every parameter plus these overrides (plain values; choices by index).
struct Preset
{
    const char* name; // "Group - Name": the group becomes a sub-menu
    std::vector<std::pair<const char*, float>> values;
};

const std::vector<Preset>& factory();

// Output settings belong to the user's setup, not to a sound: presets leave them alone.
bool isOutputSetting (const juce::String& paramId);

// Every parameter (except output settings) to its default or the preset's value.
void apply (juce::AudioProcessorValueTreeState& state, const Preset& preset);

// User presets: parameters (plain values) + seed, as XML.
juce::File userFolder(); // ~/Music/DarkMPE/Presets
bool save (juce::AudioProcessorValueTreeState& state, int seed, int variation, const juce::File& file);
bool load (juce::AudioProcessorValueTreeState& state, const juce::File& file, int& seed, int& variation);

} // namespace presets
