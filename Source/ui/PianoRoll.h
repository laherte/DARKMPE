#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

// Draws the rendered phrase: notes with their pitch-bend path, plus Slide (CC74) and Pressure lanes.
// The drawing is cached in an image (at the display's pixel density); while playing only the playhead moves.
class PianoRoll : public juce::Component, private juce::Timer
{
public:
    explicit PianoRoll (DarkMPEProcessor& p);
    ~PianoRoll() override;

    void paint (juce::Graphics& g) override;
    void resized() override { cacheValid = false; }
    void refresh();

private:
    void timerCallback() override;
    void drawStatic (juce::Graphics& g);
    float playheadX (double beats) const;

    DarkMPEProcessor& proc;
    std::shared_ptr<const Rendered> shown;
    double lastPlayhead = -1.0;

    juce::Image cache;
    bool cacheValid = false;
    juce::Rectangle<float> rollArea, laneArea; // laneArea: roll top .. pressure lane bottom
};
