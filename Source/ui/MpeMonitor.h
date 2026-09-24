#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

#include <array>

// Live view of the MPE member channels 2..16: note, per-channel bend, slide and pressure.
// Several channels bending differently at once is the proof that the output is real MPE.
class MpeMonitor : public juce::Component, private juce::Timer
{
public:
    explicit MpeMonitor (DarkMPEProcessor& p);
    ~MpeMonitor() override;

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;

    DarkMPEProcessor& proc;
    std::array<int, 64> snapshot {}; // what was last drawn: repaint only on change
};
