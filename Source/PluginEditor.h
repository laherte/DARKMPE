#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/MpeMonitor.h"
#include "ui/PianoRoll.h"
#include "ui/Theme.h"

class DarkMPEEditor : public juce::AudioProcessorEditor,
                      public juce::DragAndDropContainer,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer
{
public:
    explicit DarkMPEEditor (DarkMPEProcessor&);
    ~DarkMPEEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

    // ---- small widgets
    struct Knob : juce::Component
    {
        Knob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& label);
        void resized() override;
        juce::Slider slider;
        juce::Label label;
        juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    };

    struct Choice : juce::Component
    {
        Choice (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& label);
        void resized() override;
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    struct Toggle : juce::Component
    {
        Toggle (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& label);
        void resized() override { button.setBounds (getLocalBounds().reduced (2, 6)); }
        juce::TextButton button;
        juce::AudioProcessorValueTreeState::ButtonAttachment attachment;
    };

    struct DragOut : juce::Component
    {
        explicit DragOut (DarkMPEEditor& e) : editor (e) { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
        void paint (juce::Graphics&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override { dragging = false; }
        DarkMPEEditor& editor;
        bool dragging = false;
    };

private:
    void timerCallback() override;
    void updateModeVisibility();
    void showHarmony();
    void setStatus (const juce::String& s) { status.setText (s, juce::dontSendNotification); }

    DarkMPEProcessor& proc;
    theme::LookAndFeel lnf;

    juce::TextButton genTab { "GENERATE" }, xformTab { "TRANSFORM" }, cineTab { "CINEMATIC" };
    juce::TextButton newBtn { "NEW" }, mutateBtn { "MUTATE" }, loadBtn { "LOAD MIDI" }, captureBtn { "CAPTURE" }, exportBtn { "EXPORT" };
    Toggle previewToggle;
    DragOut dragOut { *this };
    juce::Label status;

    PianoRoll roll;
    MpeMonitor monitor;

    std::vector<std::unique_ptr<juce::Component>> genControls, voiceControls, cineControls, exprControls;
    juce::Rectangle<int> genArea, exprArea;

    std::unique_ptr<juce::FileChooser> chooser;
    bool dropHover = false;
    DarkMPEProcessor::Mode shownMode { DarkMPEProcessor::Mode::generate };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DarkMPEEditor)
};
