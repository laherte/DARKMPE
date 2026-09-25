#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

#include <vector>

// Live preview of the MPE expression settings: a short demo phrase shaped with the current parameters, drawn on
// the bend axis of the roll (cents near the note, semitones beyond), with its timbre underneath. Moving a knob
// redraws it, so glide, vibrato, detune drift and the gestures can be seen before anything plays.
class ExprScope : public juce::Component, private juce::Timer
{
public:
    explicit ExprScope (DarkMPEProcessor& p);
    ~ExprScope() override;

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;

    DarkMPEProcessor& proc;
    dmpe::Phrase demo;
    std::vector<float> snapshot; // parameter values the demo was shaped with
};
