#include "PluginEditor.h"

using namespace theme;

// ------------------------------------------------------------------ widgets
DarkMPEEditor::Knob::Knob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& text)
    : slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow),
      attachment (s, id, slider)
{
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 14);
    slider.setNumDecimalPlacesToDisplay (2);
    label.setText (text.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, textDim());
    addAndMakeVisible (slider);
    addAndMakeVisible (label);
}

void DarkMPEEditor::Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (14));
    slider.setBounds (r);
}

DarkMPEEditor::Choice::Choice (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& text)
{
    label.setText (text.toUpperCase(), juce::dontSendNotification);
    label.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, textDim());
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (id)))
        box.addItemList (p->choices, 1);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, id, box);
    addAndMakeVisible (box);
    addAndMakeVisible (label);
}

void DarkMPEEditor::Choice::resized()
{
    auto r = getLocalBounds().reduced (3, 0);
    label.setBounds (r.removeFromTop (14));
    box.setBounds (r.removeFromTop (26));
}

DarkMPEEditor::Toggle::Toggle (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& text)
    : button (text.toUpperCase()), attachment (s, id, button)
{
    button.setClickingTogglesState (true);
    addAndMakeVisible (button);
}

void DarkMPEEditor::DragOut::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (accent().withAlpha (isMouseOver() ? 0.35f : 0.18f));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (accent());
    const float dashes[] = { 4.0f, 3.0f };
    juce::Path p;
    p.addRoundedRectangle (r, 3.0f);
    juce::Path dashed;
    juce::PathStrokeType (1.2f).createDashedStroke (dashed, p, dashes, 2);
    g.fillPath (dashed);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (juce::CharPointer_UTF8 ("DRAG MPE \xe2\x96\xb6 LIVE"), getLocalBounds(), juce::Justification::centred);
}

void DarkMPEEditor::DragOut::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 4)
        return;

    dragging = true;
    const auto file = editor.proc.writeTempMidiForDrag();
    if (file.existsAsFile())
    {
        editor.setStatus ("Dragging " + file.getFileName()
                          + "  -  Live imports .mid files WITHOUT MPE: for MPE clips in Live record from " + editor.proc.getPortName());
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                    [this] { dragging = false; });
    }
    else
    {
        editor.setStatus ("Could not write the MIDI file");
        dragging = false;
    }
}

// ------------------------------------------------------------------ editor
DarkMPEEditor::DarkMPEEditor (DarkMPEProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      previewToggle (p.apvts, "preview", "Preview"),
      roll (p),
      monitor (p)
{
    setLookAndFeel (&lnf);
    auto& s = proc.apvts;

    for (auto* b : { &genTab, &xformTab, &cineTab })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
    }
    genTab.onClick = [this] { proc.setMode (DarkMPEProcessor::Mode::generate); };
    xformTab.onClick = [this]
    {
        if (! proc.hasSource())
            setStatus ("Load or drop a .mid file (or CAPTURE from the track) to transform it");
        proc.setMode (DarkMPEProcessor::Mode::transform);
    };

    cineTab.onClick = [this]
    {
        proc.setMode (DarkMPEProcessor::Mode::cinematic);
        setStatus (proc.hasSource() ? "Cinematic: " + proc.getSourceName() + "  -  " + proc.describeSource()
                                    : "Cinematic demo in the selected key - load or drop your chords to transform them");
    };

    newBtn.onClick = [this] { proc.generateNew(); setStatus ("New seed"); };
    mutateBtn.onClick = [this] { proc.mutate(); setStatus ("Mutated the later bars"); };
    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load MIDI", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.mid;*.midi");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto f = fc.getResult();
                                  if (f == juce::File())
                                      return;
                                  juce::String err;
                                  setStatus (proc.loadMidi (f, err) ? "Loaded " + f.getFileName() + "  -  " + proc.describeSource() : err);
                              });
    };
    captureBtn.setClickingTogglesState (true);
    captureBtn.onClick = [this]
    {
        const bool on = captureBtn.getToggleState();
        proc.setCapturing (on);
        setStatus (on ? "Capturing incoming MIDI... play your chords/melody, then press CAPTURE again"
                      : (proc.hasSource() ? "Captured  -  " + proc.describeSource() : "Nothing captured"));
    };
    exportBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Export MPE MIDI",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                                           .getChildFile (juce::File::createLegalFileName (proc.suggestedFileName()) + ".mid"),
                                                       "*.mid");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f == juce::File())
                                      return;
                                  f = f.withFileExtension ("mid");
                                  setStatus (proc.writeMidiFile (f).existsAsFile() ? "Exported " + f.getFileName() : "Export failed");
                              });
    };

    for (auto* b : { &newBtn, &mutateBtn, &loadBtn, &captureBtn, &exportBtn })
        addAndMakeVisible (*b);
    addAndMakeVisible (previewToggle);
    addAndMakeVisible (dragOut);

    status.setFont (juce::FontOptions (11.0f));
    status.setColour (juce::Label::textColourId, textDim());
    addAndMakeVisible (status);
    addAndMakeVisible (roll);
    addAndMakeVisible (monitor);

    auto knob = [&] (auto& list, const char* id, const char* label) { list.push_back (std::make_unique<Knob> (s, id, label)); };
    auto choice = [&] (auto& list, const char* id, const char* label) { list.push_back (std::make_unique<Choice> (s, id, label)); };
    auto toggle = [&] (auto& list, const char* id, const char* label) { list.push_back (std::make_unique<Toggle> (s, id, label)); };

    choice (genControls, "style", "Style");
    choice (genControls, "key", "Key");
    choice (genControls, "scale", "Scale");
    choice (genControls, "bars", "Bars");
    for (auto [id, label] : { std::pair { "density", "Density" }, { "pedal", "Pedal" }, { "octave", "Octave" },
                              { "chroma", "Chroma" }, { "slide", "Slide" }, { "gate", "Gate" }, { "swing", "Swing" },
                              { "baseOct", "Oct Base" }, { "range", "Range" } })
        knob (genControls, id, label);

    choice (voiceControls, "vMode", "Voicing");
    toggle (voiceControls, "voiceLead", "Voice Lead");
    toggle (voiceControls, "bassAnchor", "Bass Anchor");
    toggle (voiceControls, "glide", "Voice Glide");
    toggle (voiceControls, "strumDown", "Strum Down");
    toggle (voiceControls, "keepExpr", "Keep Input Expr");
    for (auto [id, label] : { std::pair { "voices", "Voices" }, { "vLow", "Low Note" }, { "vHigh", "High Note" },
                              { "strum", "Strum" }, { "slide", "Legato Glide" } })
        knob (voiceControls, id, label);

    choice (cineControls, "cMotion", "Motion");
    choice (cineControls, "cReharm", "Reharmonise");
    choice (cineControls, "cVoicing", "Voicing");
    choice (cineControls, "cShape", "Glide Shape");
    for (auto [id, label] : { std::pair { "cVoices", "Voices" }, { "cLow", "Low Note" }, { "cHigh", "High Note" },
                              { "cGlide", "Glide" }, { "cAnticip", "Anticipate" }, { "cStagger", "Stagger" },
                              { "cSwell", "Swell" } })
        knob (cineControls, id, label);
    choice (cineControls, "key", "Demo Key");
    choice (cineControls, "scale", "Demo Scale");
    choice (cineControls, "bars", "Demo Bars");

    for (auto [id, label] : { std::pair { "glideTime", "Glide Time" }, { "glideCurve", "Glide Curve" }, { "detune", "Detune" },
                              { "vibDepth", "Vibrato" }, { "vibRate", "Vib Rate" }, { "vibDelay", "Vib Delay" },
                              { "slideAmt", "Timbre" }, { "slideSpread", "Timbre Sprd" }, { "pressAmt", "Pressure" },
                              { "breath", "Breath" }, { "pbRange", "Bend Rng" } })
        knob (exprControls, id, label);
    toggle (exprControls, "virtualOut", "MIDI Port Out");

    for (auto* list : { &genControls, &voiceControls, &cineControls, &exprControls })
        for (auto& c : *list)
            addChildComponent (*c);
    for (auto& c : exprControls)
        c->setVisible (true);

    proc.onRebuilt = [this] { roll.refresh(); };

    setSize (1200, 780);
    updateModeVisibility();
    startTimerHz (10);
}

DarkMPEEditor::~DarkMPEEditor()
{
    proc.onRebuilt = nullptr;
    setLookAndFeel (nullptr);
}

void DarkMPEEditor::timerCallback()
{
    if (proc.getMode() != shownMode)
        updateModeVisibility();
}

void DarkMPEEditor::updateModeVisibility()
{
    shownMode = proc.getMode();
    const bool gen = shownMode == DarkMPEProcessor::Mode::generate;
    const bool cine = shownMode == DarkMPEProcessor::Mode::cinematic;
    const bool xform = ! gen && ! cine;
    genTab.setToggleState (gen, juce::dontSendNotification);
    xformTab.setToggleState (xform, juce::dontSendNotification);
    cineTab.setToggleState (cine, juce::dontSendNotification);
    for (auto& c : genControls) c->setVisible (gen);
    for (auto& c : voiceControls) c->setVisible (xform);
    for (auto& c : cineControls) c->setVisible (cine);
    newBtn.setEnabled (true);
    mutateBtn.setEnabled (gen);
    if (xform && proc.hasSource())
        setStatus ("Source: " + proc.getSourceName() + "  -  " + proc.describeSource());
    resized();
    repaint();
}

void DarkMPEEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg());

    // logo
    g.setColour (chrome());
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold).withKerningFactor (0.18f));
    g.drawText ("DARK", 14, 10, 100, 34, juce::Justification::centredLeft, false);
    g.setColour (accent());
    g.drawText ("MPE", 108, 10, 76, 34, juce::Justification::centredLeft, false);

    auto section = [&] (juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour (panel());
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        g.setColour (grid());
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f, 1.0f);
        g.setColour (accent());
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold).withKerningFactor (0.2f));
        g.drawText (title, r.reduced (10, 6).removeFromTop (14), juce::Justification::topLeft);
    };
    section (genArea, shownMode == DarkMPEProcessor::Mode::generate    ? "LEAD GENERATOR"
                      : shownMode == DarkMPEProcessor::Mode::cinematic ? "CINEMATIC MOTION"
                                                                       : "MPE VOICING");
    section (exprArea, "MPE EXPRESSION");

    if (dropHover)
    {
        g.setColour (accent().withAlpha (0.25f));
        g.fillRect (getLocalBounds());
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        g.drawText ("DROP MIDI TO TRANSFORM", getLocalBounds(), juce::Justification::centred);
    }
}

void DarkMPEEditor::resized()
{
    auto r = getLocalBounds().reduced (12, 8);

    auto header = r.removeFromTop (40);
    header.removeFromLeft (178);
    genTab.setBounds (header.removeFromLeft (100).reduced (2, 4));
    xformTab.setBounds (header.removeFromLeft (100).reduced (2, 4));
    cineTab.setBounds (header.removeFromLeft (100).reduced (2, 4));
    header.removeFromLeft (16);
    dragOut.setBounds (header.removeFromRight (160).reduced (2, 2));
    header.removeFromRight (8);
    previewToggle.setBounds (header.removeFromRight (86));
    for (auto* b : { &exportBtn, &captureBtn, &loadBtn, &mutateBtn, &newBtn })
        b->setBounds (header.removeFromRight (86).reduced (2, 4));

    status.setBounds (r.removeFromTop (20));
    roll.setBounds (r.removeFromTop (320));
    r.removeFromTop (4);
    monitor.setBounds (r.removeFromTop (58));
    r.removeFromTop (10);

    genArea = r.removeFromLeft ((int) (r.getWidth() * 0.56f));
    r.removeFromLeft (10);
    exprArea = r;

    auto flow = [] (std::vector<std::unique_ptr<juce::Component>>& list, juce::Rectangle<int> area)
    {
        juce::FlexBox fb;
        fb.flexWrap = juce::FlexBox::Wrap::wrap;
        fb.alignContent = juce::FlexBox::AlignContent::flexStart;
        for (auto& c : list)
        {
            float w = 72.0f, h = 92.0f;
            if (dynamic_cast<Choice*> (c.get()) != nullptr) { w = 140.0f; h = 46.0f; }
            if (dynamic_cast<Toggle*> (c.get()) != nullptr) { w = 108.0f; h = 46.0f; }
            fb.items.add (juce::FlexItem (*c).withWidth (w).withHeight (h));
        }
        fb.performLayout (area);
    };

    const auto inner = [] (juce::Rectangle<int> a) { return a.reduced (10, 6).withTrimmedTop (20); };
    flow (genControls, inner (genArea));
    flow (voiceControls, inner (genArea));
    flow (cineControls, inner (genArea));
    flow (exprControls, inner (exprArea));
}

bool DarkMPEEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi"))
            return true;
    return false;
}

void DarkMPEEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    for (const auto& path : files)
        if (path.endsWithIgnoreCase (".mid") || path.endsWithIgnoreCase (".midi"))
        {
            juce::String err;
            const juce::File f (path);
            setStatus (proc.loadMidi (f, err) ? "Loaded " + f.getFileName() + "  -  " + proc.describeSource() : err);
            break;
        }
    repaint();
}
