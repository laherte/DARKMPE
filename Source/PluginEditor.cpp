#include "PluginEditor.h"
#include "presets/Presets.h"

#include <map>

using namespace theme;

namespace
{
// Control sizes at 100%: compact so every mode fits its panel.
constexpr int knobW = 64, knobH = 84, choiceW = 118, choiceH = 44, toggleW = 96, toggleH = 40;
} // namespace

// ------------------------------------------------------------------ widgets
DarkMPEEditor::Knob::Knob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& text)
    : slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow),
      attachment (s, id, slider)
{
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 14);
    slider.setNumDecimalPlacesToDisplay (2);
    // Set on the slider itself: its text box is created before the editor's look-and-feel reaches it.
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    label.setText (text.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, textDim());
    label.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (slider);
    addAndMakeVisible (label);
}

void DarkMPEEditor::Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (13));
    slider.setBounds (r);
}

DarkMPEEditor::Choice::Choice (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& text)
{
    label.setText (text.toUpperCase(), juce::dontSendNotification);
    label.setFont (juce::FontOptions (9.5f, juce::Font::bold));
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
    const auto files = editor.proc.writeTempMidiForDrag();
    if (! files.isEmpty())
    {
        editor.setStatus ((files.size() > 1 ? "Dragging " + juce::String (files.size()) + " layers (one track each)"
                                            : "Dragging " + juce::File (files[0]).getFileName())
                          + "  -  Live imports .mid files WITHOUT MPE: for MPE clips in Live record from the DarkMPE ports");
        juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false, this, [this] { dragging = false; });
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
      formChoice (p.apvts, "form", "Phrase Form"),
      roll (p),
      monitor (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    auto& s = proc.apvts;

    for (auto* b : { &genTab, &xformTab, &cineTab, &kitTab })
    {
        b->setClickingTogglesState (false);
        content.addAndMakeVisible (*b);
    }
    kitTab.onClick = [this] { proc.setMode (DarkMPEProcessor::Mode::kit); };
    genTab.onClick = [this] { proc.setMode (DarkMPEProcessor::Mode::generate); };
    xformTab.onClick = [this]
    {
        if (! proc.hasSource())
            setStatus ("Load or drop a .mid file (or CAPTURE from the track) to transform it");
        proc.setMode (DarkMPEProcessor::Mode::transform);
    };
    cineTab.onClick = [this] { proc.setMode (DarkMPEProcessor::Mode::cinematic); };

    newBtn.onClick = [this]
    {
        proc.generateNew();
        if (proc.getMode() != DarkMPEProcessor::Mode::cinematic && proc.getMode() != DarkMPEProcessor::Mode::kit)
            setStatus ("New seed");
    };
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
                                  const auto written = proc.writeAllStreams (f);
                                  setStatus (written.isEmpty() ? juce::String ("Export failed")
                                             : written.size() == 1 ? "Exported " + written[0].getFileName()
                                                                   : "Exported " + juce::String (written.size()) + " layers next to " + f.getFileName());
                              });
    };
    scaleBtn.onClick = [this] { showScaleMenu(); };

    prevBtn.setButtonText (juce::CharPointer_UTF8 ("\xe2\x97\x80"));
    nextBtn.setButtonText (juce::CharPointer_UTF8 ("\xe2\x96\xb6"));
    prevBtn.setTooltip ("Previous seed");
    nextBtn.setTooltip ("Next seed");
    seedBtn.setTooltip ("Seed: favourites, or type one");
    favBtn.setTooltip ("Mark this seed as a favourite");
    prevBtn.onClick = [this] { proc.historyBack(); updateSeedControls(); };
    nextBtn.onClick = [this] { proc.historyForward(); updateSeedControls(); };
    favBtn.onClick = [this] { proc.toggleFavourite(); updateSeedControls(); };
    seedBtn.onClick = [this] { showSeedMenu(); };
    presetBtn.onClick = [this] { showPresetMenu(); };
    presetBtn.setTooltip ("Factory and user presets");

    for (auto* b : { &newBtn, &mutateBtn, &loadBtn, &captureBtn, &exportBtn, &scaleBtn, &prevBtn, &nextBtn, &seedBtn, &favBtn, &presetBtn })
        content.addAndMakeVisible (*b);
    content.addAndMakeVisible (previewToggle);
    content.addAndMakeVisible (formChoice);
    formChoice.box.setTooltip ("Classic, or call & response: A B A C, A A B A, A A A B, period, sentence, sequence");
    content.addAndMakeVisible (dragOut);

    status.setFont (juce::FontOptions (11.5f));
    status.setColour (juce::Label::textColourId, chrome().withAlpha (0.75f));
    status.setMinimumHorizontalScale (0.6f);
    content.addAndMakeVisible (status);
    content.addAndMakeVisible (roll);
    content.addAndMakeVisible (monitor);

    auto knob = [&] (auto& list, const char* id, const char* label) { list.push_back (std::make_unique<Knob> (s, id, label)); };
    auto choice = [&] (auto& list, const char* id, const char* label) { list.push_back (std::make_unique<Choice> (s, id, label)); };
    auto toggle = [&] (auto& list, const char* id, const char* label) { list.push_back (std::make_unique<Toggle> (s, id, label)); };

    choice (genControls, "style", "Style");
    choice (genControls, "key", "Key");
    choice (genControls, "scale", "Scale");
    choice (genControls, "bars", "Bars");
    for (auto [id, label] : { std::pair { "density", "Density" }, { "pedal", "Pedal" }, { "octave", "Octave" },
                              { "chroma", "Chroma" }, { "slide", "Slide" }, { "gate", "Gate" }, { "swing", "Swing" },
                              { "baseOct", "Oct Base" }, { "range", "Range" }, { "humanize", "Humanize" } })
        knob (genControls, id, label);

    choice (voiceControls, "vMode", "Voicing");
    toggle (voiceControls, "voiceLead", "Voice Lead");
    toggle (voiceControls, "bassAnchor", "Bass Anchor");
    toggle (voiceControls, "glide", "Voice Glide");
    toggle (voiceControls, "strumDown", "Strum Down");
    toggle (voiceControls, "keepExpr", "Keep Expr");
    for (auto [id, label] : { std::pair { "voices", "Voices" }, { "vLow", "Low Note" }, { "vHigh", "High Note" },
                              { "strum", "Strum" }, { "slide", "Legato Glide" }, { "humanize", "Humanize" } })
        knob (voiceControls, id, label);

    choice (cineControls, "cProg", "Progression");
    choice (cineControls, "key", "Key");
    choice (cineControls, "scale", "Scale");
    choice (cineControls, "cChordLen", "Chord Length");
    choice (cineControls, "bars", "Bars");
    choice (cineControls, "cMotion", "Motion");
    choice (cineControls, "cReharm", "Reharmonise");
    choice (cineControls, "cVoicing", "Voicing");
    choice (cineControls, "cShape", "Glide Shape");
    toggle (cineControls, "cSub", "Sub");
    for (auto [id, label] : { std::pair { "cTension", "Tension" }, { "cDark", "Darkness" }, { "cVoices", "Voices" },
                              { "cGlide", "Glide" }, { "cAnticip", "Anticipate" }, { "cStagger", "Stagger" },
                              { "cSwell", "Swell" }, { "cArc", "Arc" }, { "cFall", "Fall" }, { "cPulse", "Pulse" },
                              { "cLow", "Low Note" }, { "cHigh", "High Note" } })
        knob (cineControls, id, label);

    for (auto [id, label] : { std::pair { "glideTime", "Glide Time" }, { "glideCurve", "Glide Curve" }, { "detune", "Detune" },
                              { "vibDepth", "Vibrato" }, { "vibRate", "Vib Rate" }, { "vibDelay", "Vib Delay" },
                              { "slideAmt", "Timbre" }, { "slideSpread", "Timbre Sprd" }, { "pressAmt", "Pressure" },
                              { "breath", "Breath" } })
        knob (exprControls, id, label);

    choice (outControls, "trigMode", "Key Trigger");
    toggle (outControls, "virtualOut", "Port Out");
    toggle (outControls, "leadMono", "Mono Lead");
    knob (outControls, "pbRange", "MPE Bend");
    knob (outControls, "monoBend", "Mono Bend");

    choice (kitControls, "style", "Style");
    choice (kitControls, "key", "Key");
    choice (kitControls, "scale", "Scale");
    choice (kitControls, "bars", "Bars");
    layerRows.push_back (std::make_unique<LayerRow> (proc, 0, "kLead", nullptr, nullptr, nullptr, "leadMono", "lead from GENERATE"));
    layerRows.push_back (std::make_unique<LayerRow> (proc, 1, "kBass", "kBassPat", "kBassDen", "kBassOct", "kBassMono", ""));
    layerRows.push_back (std::make_unique<LayerRow> (proc, 2, "kArp", "kArpPat", "kArpDen", "kArpOct", "kArpMono", ""));
    layerRows.push_back (std::make_unique<LayerRow> (proc, 3, "kSiren", "kSirenPat", "kSirenDen", "kSirenOct", "kSirenMono", ""));
    layerRows.push_back (std::make_unique<LayerRow> (proc, 4, "kStab", "kStabPat", "kStabDen", "kStabOct", nullptr, ""));
    layerRows.push_back (std::make_unique<LayerRow> (proc, 5, "kPad", "cMotion", nullptr, nullptr, nullptr, "harmony from CINEMATIC"));
    for (auto& row : layerRows)
        content.addChildComponent (*row);

    for (auto* list : { &genControls, &voiceControls, &cineControls, &exprControls, &outControls, &kitControls })
        for (auto& c : *list)
            content.addChildComponent (*c);
    for (auto* list : { &exprControls, &outControls })
        for (auto& c : *list)
            c->setVisible (true);

    proc.onRebuilt = [this]
    {
        roll.refresh();
        if (proc.getMode() == DarkMPEProcessor::Mode::cinematic || proc.getMode() == DarkMPEProcessor::Mode::kit)
            showHarmony();
        for (auto& row : layerRows)
            row->repaint();
    };

    // Resizable as a whole, at a fixed aspect ratio; the scale is remembered with the plugin state
    // (read first: setting the limits resizes the editor, and only a finished editor stores its scale).
    const float saved = juce::jlimit (0.6f, 1.6f, (float) proc.apvts.state.getProperty ("uiScale", 1.0f));
    setResizable (true, true);
    setResizeLimits (baseWidth * 6 / 10, baseHeight * 6 / 10, baseWidth * 16 / 10, baseHeight * 16 / 10);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setScale (saved);
    initialised = true;

    updateModeVisibility();
    updateSeedControls();
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
    updateSeedControls();
}

void DarkMPEEditor::updateSeedControls()
{
    const int v = proc.getVariation();
    const auto text = "#" + juce::String (proc.getSeed()) + (v > 0 ? "  v" + juce::String (v) : juce::String());
    if (seedBtn.getButtonText() != text)
        seedBtn.setButtonText (text);
    prevBtn.setEnabled (proc.canGoBack());
    nextBtn.setEnabled (proc.canGoForward());
    const auto preset = juce::String (juce::CharPointer_UTF8 ("PRESET \xe2\x96\xbe  ")) + proc.getPresetName();
    if (presetBtn.getButtonText() != preset)
        presetBtn.setButtonText (preset);
    const bool fav = proc.isFavourite();
    favBtn.setButtonText (juce::CharPointer_UTF8 (fav ? "\xe2\x98\x85" : "\xe2\x98\x86"));
    favBtn.setToggleState (fav, juce::dontSendNotification);
}

void DarkMPEEditor::showPresetMenu()
{
    const auto& factory = presets::factory();
    const int current = proc.getCurrentProgram();

    juce::PopupMenu m;
    m.addItem (1, "Init", true, current == 0);
    std::map<juce::String, juce::PopupMenu> groups;
    for (size_t i = 1; i < factory.size(); ++i)
    {
        const juce::String name (factory[i].name);
        groups[name.upToFirstOccurrenceOf (" - ", false, false)]
            .addItem (1 + (int) i, name.fromFirstOccurrenceOf (" - ", false, false), true, current == (int) i);
    }
    for (const char* group : { "Lead", "Cinematic", "Kit", "Transform" })
        m.addSubMenu (group, groups[group]);

    auto files = presets::userFolder().findChildFiles (juce::File::findFiles, false, "*.dmpreset");
    files.sort();
    juce::PopupMenu user;
    for (int i = 0; i < files.size(); ++i)
        user.addItem (1000 + i, files[i].getFileNameWithoutExtension());
    if (files.isEmpty())
        user.addItem (-1, "(none yet)", false);
    m.addSubMenu ("User", user);
    m.addSeparator();
    m.addItem (5000, "Save preset...");
    m.addItem (5001, "Show preset folder");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetBtn),
                     [safe = juce::Component::SafePointer<DarkMPEEditor> (this), files] (int result)
                     {
                         if (safe == nullptr || result <= 0)
                             return;
                         auto& ed = *safe;
                         if (result < 1000)
                         {
                             ed.proc.setCurrentProgram (result - 1);
                             ed.setStatus ("Preset: " + ed.proc.getPresetName());
                         }
                         else if (result < 5000 && result - 1000 < files.size())
                         {
                             const auto f = files[result - 1000];
                             ed.setStatus (ed.proc.loadUserPreset (f) ? "Preset: " + f.getFileNameWithoutExtension() : "Could not read " + f.getFileName());
                         }
                         else if (result == 5001)
                         {
                             presets::userFolder().createDirectory();
                             presets::userFolder().revealToUser();
                         }
                         else if (result == 5000)
                         {
                             presets::userFolder().createDirectory();
                             ed.chooser = std::make_unique<juce::FileChooser> ("Save DarkMPE preset",
                                                                               presets::userFolder().getChildFile (juce::File::createLegalFileName (ed.proc.getPresetName()) + ".dmpreset"),
                                                                               "*.dmpreset");
                             ed.chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                                      [safe] (const juce::FileChooser& fc)
                                                      {
                                                          const auto f = fc.getResult().withFileExtension ("dmpreset");
                                                          if (safe == nullptr || fc.getResult() == juce::File())
                                                              return;
                                                          safe->setStatus (safe->proc.saveUserPreset (f) ? "Saved preset " + f.getFileNameWithoutExtension()
                                                                                                         : juce::String ("Could not save the preset"));
                                                      });
                         }
                         ed.updateSeedControls();
                     });
}

void DarkMPEEditor::showSeedMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Type a seed...");
    const auto favs = proc.getFavourites();
    if (! favs.empty())
    {
        m.addSectionHeader ("Favourites");
        for (size_t i = 0; i < favs.size(); ++i)
            m.addItem (100 + (int) i, "#" + juce::String (favs[i].first) + (favs[i].second > 0 ? "  v" + juce::String (favs[i].second) : juce::String()),
                       true, favs[i].first == proc.getSeed() && favs[i].second == proc.getVariation());
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&seedBtn),
                     [safe = juce::Component::SafePointer<DarkMPEEditor> (this), favs] (int result)
                     {
                         if (safe == nullptr || result <= 0)
                             return;
                         if (result >= 100 && result - 100 < (int) favs.size())
                         {
                             safe->proc.setSeed (favs[(size_t) (result - 100)].first, favs[(size_t) (result - 100)].second);
                             safe->updateSeedControls();
                             return;
                         }

                         auto* w = new juce::AlertWindow ("Seed", "Type a seed number", juce::MessageBoxIconType::NoIcon);
                         w->addTextEditor ("seed", juce::String (safe->proc.getSeed()));
                         w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
                         w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                         w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int r)
                         {
                             if (safe != nullptr && r == 1)
                             {
                                 safe->proc.setSeed (juce::jmax (0, w->getTextEditorContents ("seed").getIntValue()), 0);
                                 safe->updateSeedControls();
                             }
                         }), true);
                     });
}

void DarkMPEEditor::showHarmony()
{
    if (proc.getMode() == DarkMPEProcessor::Mode::kit)
    {
        const int focus = proc.getFocusLayer();
        setStatus ("Kit:  " + proc.getHarmonyText() + "     focus " + dmpe::layerNames[focus] + " -> "
                   + proc.getLayerPortName (focus) + (proc.isLayerPortOpen (focus) ? "" : " (port off)"));
        return;
    }
    setStatus ((proc.hasSource() ? proc.getSourceName() + ":  " : juce::String ("Harmony:  ")) + proc.getHarmonyText()
               + (proc.hasSource() ? juce::String() : "   (drop your own chords to transform them)"));
}

void DarkMPEEditor::updateModeVisibility()
{
    shownMode = proc.getMode();
    const bool gen = shownMode == DarkMPEProcessor::Mode::generate;
    const bool cine = shownMode == DarkMPEProcessor::Mode::cinematic;
    const bool kit = shownMode == DarkMPEProcessor::Mode::kit;
    const bool xform = shownMode == DarkMPEProcessor::Mode::transform;
    genTab.setToggleState (gen, juce::dontSendNotification);
    xformTab.setToggleState (xform, juce::dontSendNotification);
    cineTab.setToggleState (cine, juce::dontSendNotification);
    kitTab.setToggleState (kit, juce::dontSendNotification);
    for (auto& c : genControls) c->setVisible (gen);
    for (auto& c : voiceControls) c->setVisible (xform);
    for (auto& c : cineControls) c->setVisible (cine);
    for (auto& c : kitControls) c->setVisible (kit);
    for (auto& r : layerRows) r->setVisible (kit);
    mutateBtn.setEnabled (gen || kit);
    if (xform && proc.hasSource())
        setStatus ("Source: " + proc.getSourceName() + "  -  " + proc.describeSource());
    if (cine || kit)
        showHarmony();
    layoutContent();
    content.repaint();
}

// ------------------------------------------------------------------ scale
void DarkMPEEditor::showScaleMenu()
{
    juce::PopupMenu m;
    const float current = (float) getWidth() / (float) baseWidth;
    for (int pct : { 60, 75, 90, 100, 125, 150 })
        m.addItem (pct, juce::String (pct) + "%", true, std::abs (current * 100.0f - (float) pct) < 1.0f);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&scaleBtn),
                     [safe = juce::Component::SafePointer<DarkMPEEditor> (this)] (int result)
                     {
                         if (safe != nullptr && result > 0)
                             safe->setScale ((float) result / 100.0f);
                     });
}

void DarkMPEEditor::setScale (float scale)
{
    setSize (juce::roundToInt (baseWidth * scale), juce::roundToInt (baseHeight * scale));
}

void DarkMPEEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg());
}

void DarkMPEEditor::resized()
{
    const float scale = (float) getWidth() / (float) baseWidth;
    content.setTransform (juce::AffineTransform::scale (scale));
    content.setBounds (0, 0, baseWidth, baseHeight);
    if (initialised)
        proc.apvts.state.setProperty ("uiScale", scale, nullptr);
    scaleBtn.setButtonText (juce::String (juce::roundToInt (scale * 100.0f)) + "%");
}

// ------------------------------------------------------------------ content
void DarkMPEEditor::paintContent (juce::Graphics& g)
{
    g.fillAll (bg());

    // logo
    g.setColour (chrome());
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold).withKerningFactor (0.18f));
    g.drawText ("DARK", 14, 8, 100, 34, juce::Justification::centredLeft, false);
    g.setColour (accent());
    g.drawText ("MPE", 108, 8, 76, 34, juce::Justification::centredLeft, false);

    auto section = [&] (juce::Rectangle<int> r, const juce::String& title)
    {
        if (r.isEmpty())
            return;
        g.setColour (panel());
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        g.setColour (grid());
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f, 1.0f);
        g.setColour (accent());
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold).withKerningFactor (0.2f));
        g.drawText (title, r.reduced (10, 6).removeFromTop (14), juce::Justification::topLeft);
    };
    section (modeArea, shownMode == DarkMPEProcessor::Mode::generate    ? "LEAD GENERATOR"
                       : shownMode == DarkMPEProcessor::Mode::cinematic ? "CINEMATIC HARMONY"
                       : shownMode == DarkMPEProcessor::Mode::kit       ? "KIT  -  ONE TRACK, SIX LAYERS"
                                                                        : "MPE VOICING");

    if (shownMode == DarkMPEProcessor::Mode::kit && ! kitHeader.isEmpty())
    {
        g.setColour (textDim());
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        int x = kitHeader.getX();
        for (auto [title, w] : { std::pair { "LAYER", 74 }, { "", 46 }, { "PATTERN", 132 }, { "DENSITY", 150 }, { "OCTAVE", 110 }, { "OUT", 64 } })
        {
            g.drawText (title, x + 4, kitHeader.getY(), w, kitHeader.getHeight(), juce::Justification::centredLeft);
            x += w + 6;
        }
    }
    section (exprArea, "MPE EXPRESSION");
    section (outArea, "OUTPUT");
}

void DarkMPEEditor::paintOverlay (juce::Graphics& g)
{
    if (! dropHover)
        return;
    g.setColour (accent().withAlpha (0.25f));
    g.fillRect (content.getLocalBounds());
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("DROP MIDI TO TRANSFORM", content.getLocalBounds(), juce::Justification::centred);
}

void DarkMPEEditor::layoutContent()
{
    auto r = content.getLocalBounds().reduced (12, 8);

    // ---- header: logo, mode tabs, scale
    auto header = r.removeFromTop (40);
    header.removeFromLeft (178);
    for (auto* b : { &genTab, &xformTab, &cineTab })
        b->setBounds (header.removeFromLeft (100).reduced (2, 4));
    kitTab.setBounds (header.removeFromLeft (64).reduced (2, 4));
    header.removeFromLeft (14);
    presetBtn.setBounds (header.removeFromLeft (160).reduced (2, 6));
    header.removeFromLeft (6);
    formChoice.setBounds (header.removeFromLeft (170).withTrimmedTop (-2));
    scaleBtn.setBounds (header.removeFromRight (58).reduced (2, 6));
    header.removeFromRight (12);
    favBtn.setBounds (header.removeFromRight (34).reduced (2, 6));
    nextBtn.setBounds (header.removeFromRight (34).reduced (2, 6));
    seedBtn.setBounds (header.removeFromRight (92).reduced (2, 6));
    prevBtn.setBounds (header.removeFromRight (34).reduced (2, 6));

    // ---- toolbar: actions, status, playback / export
    auto bar = r.removeFromTop (34);
    for (auto* b : { &newBtn, &mutateBtn, &loadBtn, &captureBtn })
        b->setBounds (bar.removeFromLeft (84).reduced (2, 3));
    dragOut.setBounds (bar.removeFromRight (160).reduced (2, 2));
    bar.removeFromRight (6);
    exportBtn.setBounds (bar.removeFromRight (80).reduced (2, 3));
    previewToggle.setBounds (bar.removeFromRight (84).withTrimmedTop (-2).withTrimmedBottom (-2));
    status.setBounds (bar.reduced (8, 0));

    r.removeFromTop (4);
    roll.setBounds (r.removeFromTop (282));
    r.removeFromTop (4);
    monitor.setBounds (r.removeFromTop (56));
    r.removeFromTop (8);

    // ---- panels: mode | expression | output
    const int w = r.getWidth();
    modeArea = r.removeFromLeft ((int) (w * 0.535f));
    r.removeFromLeft (8);
    exprArea = r.removeFromLeft ((int) (w * 0.285f));
    r.removeFromLeft (8);
    outArea = r;

    auto flow = [] (ControlList& list, juce::Rectangle<int> area)
    {
        juce::FlexBox fb;
        fb.flexWrap = juce::FlexBox::Wrap::wrap;
        fb.alignContent = juce::FlexBox::AlignContent::flexStart;
        for (auto& c : list)
        {
            int cw = knobW, ch = knobH;
            if (dynamic_cast<Choice*> (c.get()) != nullptr) { cw = choiceW; ch = choiceH; }
            if (dynamic_cast<Toggle*> (c.get()) != nullptr) { cw = toggleW; ch = toggleH; }
            fb.items.add (juce::FlexItem (*c).withWidth ((float) cw).withHeight ((float) ch).withMargin ({ 0, 0, 3, 0 }));
        }
        fb.performLayout (area);
    };

    const auto inner = [] (juce::Rectangle<int> a) { return a.reduced (8, 6).withTrimmedTop (18); };
    flow (genControls, inner (modeArea));
    flow (voiceControls, inner (modeArea));
    flow (cineControls, inner (modeArea));
    flow (exprControls, inner (exprArea));
    flow (outControls, inner (outArea));

    // KIT: shared choices, then one row per layer.
    auto kitArea = inner (modeArea);
    flow (kitControls, kitArea.removeFromTop (choiceH + 3));
    kitHeader = kitArea.removeFromTop (14);
    for (auto& row : layerRows)
        row->setBounds (kitArea.removeFromTop (40).withTrimmedBottom (2));
}

// ------------------------------------------------------------------ KIT layer row
DarkMPEEditor::LayerRow::LayerRow (DarkMPEProcessor& p, int l, const char* onId, const char* patternId, const char* densityId,
                                   const char* octaveId, const char* monoId, const juce::String& text)
    : proc (p), layer (l)
{
    auto& s = proc.apvts;
    name.setButtonText (juce::String (dmpe::layerNames[layer]).toUpperCase());
    name.setColour (juce::TextButton::textColourOffId, theme::layerColour (layer));
    name.setTooltip ("Focus: shown in the roll, sent to the host MIDI out, dragged / exported first");
    name.onClick = [this] { proc.setFocusLayer (layer); };
    addAndMakeVisible (name);

    on.setClickingTogglesState (true);
    onAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, onId, on);
    addAndMakeVisible (on);

    if (patternId != nullptr)
    {
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (patternId)))
            pattern.addItemList (c->choices, 1);
        patternAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, patternId, pattern);
        addAndMakeVisible (pattern);
    }
    for (auto [slider, id] : { std::pair { &density, densityId }, { &octave, octaveId } })
    {
        if (id == nullptr)
            continue;
        slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 34, 18);
        slider->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider->setColour (juce::Slider::trackColourId, theme::layerColour (layer));
        slider->setNumDecimalPlacesToDisplay (2);
        addAndMakeVisible (*slider);
    }
    if (densityId != nullptr)
        densityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, densityId, density);
    if (octaveId != nullptr)
        octaveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, octaveId, octave);
    if (monoId != nullptr)
    {
        mono.setClickingTogglesState (true);
        mono.setTooltip ("Mono: one line on channel 1 with channel pitch bend (synths without MPE)");
        monoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, monoId, mono);
        addAndMakeVisible (mono);
    }
    note.setText (text, juce::dontSendNotification);
    note.setFont (juce::FontOptions (10.0f));
    note.setColour (juce::Label::textColourId, theme::textDim());
    addAndMakeVisible (note);
}

void DarkMPEEditor::LayerRow::resized()
{
    auto r = getLocalBounds().reduced (0, 3);
    auto take = [&] (int w) { auto a = r.removeFromLeft (w); r.removeFromLeft (6); return a; };
    name.setBounds (take (74));
    on.setBounds (take (46));
    const auto patternArea = take (132);
    const auto densityArea = take (150);
    const auto octaveArea = take (110);
    mono.setBounds (take (64));
    pattern.setBounds (patternArea);
    density.setBounds (densityArea);
    octave.setBounds (octaveArea);
    // The lead and the pad take their settings from other modes: say so where the controls would be.
    note.setBounds (pattern.isVisible() ? densityArea.getUnion (octaveArea) : patternArea.getUnion (octaveArea));
}

void DarkMPEEditor::LayerRow::paint (juce::Graphics& g)
{
    if (proc.getFocusLayer() != layer)
        return;
    g.setColour (theme::layerColour (layer).withAlpha (0.12f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
    g.setColour (theme::layerColour (layer));
    g.fillRect (getLocalBounds().toFloat().withWidth (2.0f));
}

// ------------------------------------------------------------------ files
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
    content.repaint();
}
