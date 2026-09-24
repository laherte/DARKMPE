#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

// Draws the rendered phrase: notes with their pitch-bend path, plus Slide (CC74) and Pressure lanes.
class PianoRoll : public juce::Component, private juce::Timer
{
public:
    explicit PianoRoll (DarkMPEProcessor& p);
    ~PianoRoll() override;

    void paint (juce::Graphics& g) override;
    void refresh();

private:
    void timerCallback() override;

    DarkMPEProcessor& proc;
    std::shared_ptr<const Rendered> shown;
    double lastPlayhead = -1.0;
};
