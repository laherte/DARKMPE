#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/ExprScope.h"
#include "ui/MpeMonitor.h"
#include "ui/PianoRoll.h"
#include "ui/Theme.h"

class DarkMPEEditor : public juce::AudioProcessorEditor,
                      public juce::DragAndDropContainer,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer
{
public:
    // Everything is laid out at this size and scaled as a whole (60% .. 160%).
    static constexpr int baseWidth = 1200, baseHeight = 860;

    explicit DarkMPEEditor (DarkMPEProcessor&);
    ~DarkMPEEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; content.repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; content.repaint(); }
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
        void resized() override { button.setBounds (getLocalBounds().reduced (2, 5)); }
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

    // One KIT layer: focus (name), on, pattern, density, octave, mono.
    struct LayerRow : juce::Component
    {
        LayerRow (DarkMPEProcessor& p, int layer, const char* onId, const char* patternId, const char* densityId,
                  const char* octaveId, const char* monoId, const juce::String& note);
        void resized() override;
        void paint (juce::Graphics&) override;

        DarkMPEProcessor& proc;
        const int layer;
        juce::TextButton name, on { "ON" }, mono { "MONO" };
        juce::ComboBox pattern;
        juce::Slider density { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        juce::Slider octave { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        juce::Label note;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment, monoAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> patternAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> densityAttachment, octaveAttachment;
    };

    // The scaled surface holding every control.
    struct Content : juce::Component
    {
        explicit Content (DarkMPEEditor& e) : editor (e) { setOpaque (true); }
        void paint (juce::Graphics& g) override { editor.paintContent (g); }
        void paintOverChildren (juce::Graphics& g) override { editor.paintOverlay (g); }
        void resized() override { editor.layoutContent(); }
        DarkMPEEditor& editor;
    };

    using ControlList = std::vector<std::unique_ptr<juce::Component>>;

private:
    void timerCallback() override;
    void updateModeVisibility();
    void showHarmony();
    void setStatus (const juce::String& s) { status.setText (s, juce::dontSendNotification); }
    void paintContent (juce::Graphics&);
    void paintOverlay (juce::Graphics&);
    void layoutContent();
    void showScaleMenu();
    void showSeedMenu();
    void showPresetMenu();
    void updateSeedControls();
    void updateFormEnabled();
    void setScale (float scale);

    DarkMPEProcessor& proc;
    theme::LookAndFeel lnf;
    Content content { *this };

    juce::TextButton genTab { "GENERATE" }, xformTab { "TRANSFORM" }, cineTab { "CINEMATIC" }, kitTab { "KIT" };
    juce::TextButton newBtn { "NEW" }, mutateBtn { "MUTATE" }, loadBtn { "LOAD MIDI" }, captureBtn { "CAPTURE" }, exportBtn { "EXPORT" };
    juce::TextButton scaleBtn { "100%" };
    juce::TextButton prevBtn, nextBtn, seedBtn, favBtn; // seed history and favourites
    juce::TextButton presetBtn;
    Choice formChoice; // phrase form, for every mode
    Toggle previewToggle;
    DragOut dragOut { *this };
    juce::Label status;

    PianoRoll roll;
    MpeMonitor monitor;
    ExprScope scope;

    ControlList genControls, voiceControls, cineControls, exprControls, outControls, kitControls;
    std::vector<std::unique_ptr<LayerRow>> layerRows;
    juce::Rectangle<int> modeArea, exprArea, outArea, kitHeader;

    std::unique_ptr<juce::FileChooser> chooser;
    bool dropHover = false;
    bool initialised = false;
    DarkMPEProcessor::Mode shownMode { DarkMPEProcessor::Mode::generate };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DarkMPEEditor)
};
