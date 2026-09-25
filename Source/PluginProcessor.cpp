#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "engine/MidiFileIO.h"
#include "engine/Humanize.h"
#include "engine/Rng.h"
#include "presets/Presets.h"
#include "engine/MpeRenderer.h"

#include <cmath>

using namespace dmpe;

namespace ids
{
// generator
constexpr const char* mode = "mode";
constexpr const char* key = "key";
constexpr const char* scale = "scale";
constexpr const char* style = "style";
constexpr const char* bars = "bars";
constexpr const char* density = "density";
constexpr const char* octave = "octave";
constexpr const char* pedal = "pedal";
constexpr const char* chroma = "chroma";
constexpr const char* slide = "slide";
constexpr const char* gate = "gate";
constexpr const char* swing = "swing";
constexpr const char* baseOct = "baseOct";
constexpr const char* range = "range";
constexpr const char* humanize = "humanize";
constexpr const char* form = "form";
// voicing
constexpr const char* vMode = "vMode";
constexpr const char* voices = "voices";
constexpr const char* vLow = "vLow";
constexpr const char* vHigh = "vHigh";
constexpr const char* voiceLead = "voiceLead";
constexpr const char* bassAnchor = "bassAnchor";
constexpr const char* glide = "glide";
constexpr const char* strum = "strum";
constexpr const char* strumDown = "strumDown";
constexpr const char* keepExpr = "keepExpr";
// cinematic
constexpr const char* cMotion = "cMotion";
constexpr const char* cReharm = "cReharm";
constexpr const char* cVoicing = "cVoicing";
constexpr const char* cShape = "cShape";
constexpr const char* cVoices = "cVoices";
constexpr const char* cLow = "cLow";
constexpr const char* cHigh = "cHigh";
constexpr const char* cGlide = "cGlide";
constexpr const char* cAnticip = "cAnticip";
constexpr const char* cStagger = "cStagger";
constexpr const char* cSwell = "cSwell";
constexpr const char* cProg = "cProg";
constexpr const char* cChordLen = "cChordLen";
constexpr const char* cTension = "cTension";
constexpr const char* cDark = "cDark";
constexpr const char* cPulse = "cPulse";
constexpr const char* cArc = "cArc";
constexpr const char* cFall = "cFall";
constexpr const char* cSub = "cSub";
// expression
constexpr const char* glideTime = "glideTime";
constexpr const char* glideCurve = "glideCurve";
constexpr const char* detune = "detune";
constexpr const char* vibDepth = "vibDepth";
constexpr const char* vibRate = "vibRate";
constexpr const char* vibDelay = "vibDelay";
constexpr const char* slideAmt = "slideAmt";
constexpr const char* slideSpread = "slideSpread";
constexpr const char* pressAmt = "pressAmt";
constexpr const char* breath = "breath";
constexpr const char* gesture = "gesture";
constexpr const char* gAmount = "gAmount";
constexpr const char* gDepth = "gDepth";
constexpr const char* gRiff = "gRiff";
constexpr const char* pbRange = "pbRange";
constexpr const char* preview = "preview";
constexpr const char* virtualOut = "virtualOut";
constexpr const char* leadMono = "leadMono";
constexpr const char* monoBend = "monoBend";
constexpr const char* trigMode = "trigMode";
// kit: per layer (Lead, Bass, Arp, Siren, Stab, Pad)
constexpr const char* layerOn[] = { "kLead", "kBass", "kArp", "kSiren", "kStab", "kPad" };
constexpr const char* layerPattern[] = { nullptr, "kBassPat", "kArpPat", "kSirenPat", "kStabPat", nullptr };
constexpr const char* layerDensity[] = { nullptr, "kBassDen", "kArpDen", "kSirenDen", "kStabDen", nullptr };
constexpr const char* layerOctave[] = { nullptr, "kBassOct", "kArpOct", "kSirenOct", "kStabOct", nullptr };
constexpr const char* layerMono[] = { "leadMono", "kBassMono", "kArpMono", "kSirenMono", nullptr, nullptr };
constexpr const char* kFocus = "kFocus";
} // namespace ids

static const int barChoices[] = { 1, 2, 4, 8 };

juce::AudioProcessorValueTreeState::ParameterLayout DarkMPEProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;

    auto choice = [&] (const char* id, const char* name, StringArray items, int def)
    { l.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def)); };
    auto flt = [&] (const char* id, const char* name, float lo, float hi, float def, const char* suffix = "")
    { l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi), def,
                                                   AudioParameterFloatAttributes().withLabel (suffix)
                                                       .withStringFromValueFunction ([] (float v, int) { return String (v, 2); }))); };
    auto integer = [&] (const char* id, const char* name, int lo, int hi, int def)
    { l.add (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def)); };
    auto boolean = [&] (const char* id, const char* name, bool def)
    { l.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };

    auto names = [] (const char* const* arr, int n) { StringArray s; for (int i = 0; i < n; ++i) s.add (arr[i]); return s; };

    choice (ids::mode, "Mode", { "Generate", "Transform", "Cinematic", "Kit" }, 0);

    choice (ids::key, "Key", names (scales::keyNames, 12), 9);
    choice (ids::scale, "Scale", names (scales::scaleNames, (int) scales::Scale::count), 1);
    choice (ids::style, "Style", names (styleNames, (int) Style::count), 0);
    choice (ids::bars, "Bars", { "1", "2", "4", "8" }, 2);
    flt (ids::density, "Density", 0.0f, 1.0f, 0.7f);
    flt (ids::octave, "Octave", 0.0f, 1.0f, 0.3f);
    flt (ids::pedal, "Pedal", 0.0f, 1.0f, 0.5f);
    flt (ids::chroma, "Chroma", 0.0f, 1.0f, 0.15f);
    flt (ids::slide, "Slide", 0.0f, 1.0f, 0.3f);
    flt (ids::gate, "Gate", 0.1f, 1.0f, 0.6f);
    flt (ids::swing, "Swing", 0.0f, 1.0f, 0.0f);
    integer (ids::baseOct, "Octave Base", 1, 5, 3);
    integer (ids::range, "Range", 1, 3, 2);
    flt (ids::humanize, "Humanize", 0.0f, 1.0f, 0.0f);
    choice (ids::form, "Form", names (formNames, (int) Form::count), 0);

    choice (ids::vMode, "Voicing", names (voicingNames, (int) VoicingMode::count), 2);
    integer (ids::voices, "Voices", 1, 6, 4);
    integer (ids::vLow, "Low Note", 24, 72, 40);
    integer (ids::vHigh, "High Note", 48, 108, 84);
    boolean (ids::voiceLead, "Voice Leading", true);
    boolean (ids::bassAnchor, "Bass Anchor", true);
    boolean (ids::glide, "Voice Glide", true);
    flt (ids::strum, "Strum", 0.0f, 0.25f, 0.0f, "beats");
    boolean (ids::strumDown, "Strum Down", false);
    boolean (ids::keepExpr, "Keep Input Expression", true);

    choice (ids::cMotion, "Cine Motion", names (motionNames, (int) Motion::count), 0);
    choice (ids::cReharm, "Cine Reharm", names (reharmNames, (int) Reharm::count), 1);
    choice (ids::cVoicing, "Cine Voicing", names (voicingNames, (int) VoicingMode::count), (int) VoicingMode::epicSpread);
    choice (ids::cShape, "Cine Glide Shape", names (glideShapeNames, (int) GlideShape::count), 0);
    integer (ids::cVoices, "Cine Voices", 2, 8, 6);
    integer (ids::cLow, "Cine Low Note", 24, 60, 33);
    integer (ids::cHigh, "Cine High Note", 60, 108, 88);
    flt (ids::cGlide, "Cine Glide", 0.05f, 1.0f, 0.5f);
    flt (ids::cAnticip, "Cine Anticipation", 0.0f, 1.0f, 0.5f);
    flt (ids::cStagger, "Cine Stagger", 0.0f, 1.0f, 0.4f);
    flt (ids::cSwell, "Cine Swell", 0.0f, 1.0f, 0.7f);
    choice (ids::cProg, "Cine Progression", names (progressionNames, (int) Progression::count), 0);
    choice (ids::cChordLen, "Cine Chord Length", names (chordLengthNames, (int) ChordLength::count), 1);
    flt (ids::cTension, "Cine Tension", 0.0f, 1.0f, 0.35f);
    flt (ids::cDark, "Cine Darkness", 0.0f, 1.0f, 0.5f);
    flt (ids::cPulse, "Cine Pulse Rate", 0.0f, 1.0f, 0.5f);
    flt (ids::cArc, "Cine Arc", 0.0f, 1.0f, 0.3f);
    flt (ids::cFall, "Cine Fall", 0.0f, 1.0f, 0.0f);
    boolean (ids::cSub, "Cine Sub", false);

    flt (ids::glideTime, "Glide Time", 0.01f, 0.5f, 0.12f, "beats");
    flt (ids::glideCurve, "Glide Curve", 0.0f, 1.0f, 0.6f);
    flt (ids::detune, "Detune", 0.0f, 50.0f, 8.0f, "ct");
    flt (ids::vibDepth, "Vibrato", 0.0f, 1.0f, 0.2f, "st");
    flt (ids::vibRate, "Vib Rate", 0.25f, 4.0f, 1.5f, "/beat");
    flt (ids::vibDelay, "Vib Delay", 0.0f, 2.0f, 0.5f, "beats");
    flt (ids::slideAmt, "Timbre", 0.0f, 1.0f, 0.6f);
    flt (ids::slideSpread, "Timbre Spread", 0.0f, 1.0f, 0.4f);
    flt (ids::pressAmt, "Pressure", 0.0f, 1.0f, 0.7f);
    flt (ids::breath, "Breath", 0.0f, 1.0f, 0.2f);
    choice (ids::gesture, "Gesture", names (gestureProfileNames, (int) GestureProfile::count), (int) GestureProfile::autoStyle);
    flt (ids::gAmount, "Gesture Amount", 0.0f, 1.0f, 0.5f);
    flt (ids::gDepth, "Gesture Depth", 0.0f, 1.0f, 0.4f);
    flt (ids::gRiff, "Bend Riff", 0.0f, 1.0f, 0.35f);
    integer (ids::pbRange, "Bend Range", 1, 96, 48);
    boolean (ids::preview, "Preview", false);
    boolean (ids::virtualOut, "Virtual MIDI Out", true);
    boolean (ids::leadMono, "Mono Lead Out", false);
    integer (ids::monoBend, "Mono Bend Range", 1, 48, 12);
    choice (ids::trigMode, "Key Trigger", { "Off", "Transpose", "Gate" }, 0);

    const StringArray patternNames[] = { {}, names (bassPatternNames, (int) BassPattern::count), names (arpPatternNames, (int) ArpPattern::count),
                                         names (sirenPatternNames, (int) SirenPattern::count), names (stabPatternNames, (int) StabPattern::count), {} };
    const float densities[] = { 0.0f, 0.6f, 0.55f, 0.4f, 0.5f, 0.0f };
    for (int k = 0; k < numLayers; ++k)
    {
        const String layer = String ("Kit ") + layerNames[k];
        boolean (ids::layerOn[k], layer.toRawUTF8(), k != (int) Layer::siren);
        if (ids::layerPattern[k] != nullptr)
        {
            choice (ids::layerPattern[k], (layer + " Pattern").toRawUTF8(), patternNames[k], 0);
            flt (ids::layerDensity[k], (layer + " Density").toRawUTF8(), 0.0f, 1.0f, densities[k]);
            integer (ids::layerOctave[k], (layer + " Octave").toRawUTF8(), -2, 2, 0);
        }
        if (ids::layerMono[k] != nullptr && k != (int) Layer::lead)
            boolean (ids::layerMono[k], (layer + " Mono").toRawUTF8(), k == (int) Layer::bass);
    }
    choice (ids::kFocus, "Kit Focus", names (layerNames, numLayers), 0);

    return l;
}

static std::atomic<int> instanceCounter { 0 };

DarkMPEProcessor::DarkMPEProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DarkMPE", createLayout())
{
    const int instance = ++instanceCounter;
    instanceNumber = instance;
    portName = instance == 1 ? juce::String ("DarkMPE Out") : "DarkMPE Out " + juce::String (instance);

    apvts.state.setProperty ("seed", juce::Random::getSystemRandom().nextInt (100000), nullptr);
    apvts.state.setProperty ("variation", 0, nullptr);

    for (auto* p : getParameters())
        if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            if (wp->paramID != ids::preview)
                apvts.addParameterListener (wp->paramID, this);

    startTimerHz (4); // watches for the first playback (see allowPorts)

    pbRangeParam = apvts.getRawParameterValue (ids::pbRange);
    previewParam = apvts.getRawParameterValue (ids::preview);
    trigModeParam = apvts.getRawParameterValue (ids::trigMode);
    keyParam = apvts.getRawParameterValue (ids::key);

    rebuild();
}

DarkMPEProcessor::~DarkMPEProcessor()
{
    stopTimer();
    cancelPendingUpdate();
}

juce::String DarkMPEProcessor::getLayerPortName (int layer) const
{
    if (layer <= 0)
        return portName;
    return "DarkMPE " + juce::String (layerNames[juce::jlimit (0, numLayers - 1, layer)])
         + (instanceNumber > 1 ? " " + juce::String (instanceNumber) : juce::String());
}

void DarkMPEProcessor::updatePorts()
{
    const bool allowed = portAllowed.load() && pb (ids::virtualOut);
    ports.setOpen (0, portName, allowed);

    // KIT layers get their own port ("DarkMPE Bass", ...) once used; they stay so Live keeps its routing.
    for (int l = 1; l < numLayers; ++l)
    {
        if (! allowed)
            layerPortWanted[(size_t) l] = false;
        else if (getMode() == Mode::kit && pb (ids::layerOn[l]))
            layerPortWanted[(size_t) l] = true;
        ports.setOpen (l, getLayerPortName (l), layerPortWanted[(size_t) l]);
    }
}

int DarkMPEProcessor::getFocusLayer() const { return pi (ids::kFocus); }

void DarkMPEProcessor::setFocusLayer (int layer)
{
    if (auto* p = apvts.getParameter (ids::kFocus))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (0, numLayers - 1, layer)));
}

bool DarkMPEProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono() || out.isDisabled();
}

void DarkMPEProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    wasPlaying = false;

    // Room for a dense block on every layer, so the audio thread never grows a buffer.
    for (auto& l : layers)
        l.buffer.ensureSize (32768);
    hostBuffer.ensureSize (32768);

}

void DarkMPEProcessor::allowPorts()
{
    if (! portAllowed.exchange (true))
        triggerAsyncUpdate(); // opened by the rebuild, on the message thread
}

void DarkMPEProcessor::timerCallback()
{
    if (playedOnce.load() && ! portAllowed.load())
        allowPorts();
}

// ------------------------------------------------------------------ parameters
float DarkMPEProcessor::pf (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
int DarkMPEProcessor::pi (const char* id) const { return (int) std::lround (apvts.getRawParameterValue (id)->load()); }
bool DarkMPEProcessor::pb (const char* id) const { return apvts.getRawParameterValue (id)->load() > 0.5f; }

GenParams DarkMPEProcessor::readGenParams() const
{
    GenParams g;
    g.key = pi (ids::key);
    g.scale = (scales::Scale) pi (ids::scale);
    g.style = (Style) pi (ids::style);
    g.bars = barChoices[std::clamp (pi (ids::bars), 0, 3)];
    g.density = pf (ids::density);
    g.octave = pf (ids::octave);
    g.pedal = pf (ids::pedal);
    g.chroma = pf (ids::chroma);
    g.slide = pf (ids::slide);
    g.gate = pf (ids::gate);
    g.swing = pf (ids::swing);
    g.baseOctave = pi (ids::baseOct);
    g.rangeOctaves = pi (ids::range);
    g.seed = (int) apvts.state.getProperty ("seed", 1);
    g.variation = (int) apvts.state.getProperty ("variation", 0);
    g.form = (Form) pi (ids::form);
    return g;
}

VoicingParams DarkMPEProcessor::readVoicingParams() const
{
    VoicingParams v;
    v.mode = (VoicingMode) pi (ids::vMode);
    v.voices = pi (ids::voices);
    v.lowPitch = pi (ids::vLow);
    v.highPitch = std::max (pi (ids::vHigh), v.lowPitch + 12);
    v.voiceLeading = pb (ids::voiceLead);
    v.bassAnchor = pb (ids::bassAnchor);
    v.glide = pb (ids::glide);
    v.glideProb = std::max (0.05f, pf (ids::slide) * 2.0f);
    v.strum = pf (ids::strum);
    v.strumDown = pb (ids::strumDown);
    return v;
}

ExprParams DarkMPEProcessor::readExprParams() const
{
    ExprParams e;
    e.glideTime = pf (ids::glideTime);
    e.glideCurve = pf (ids::glideCurve);
    e.detuneCents = pf (ids::detune);
    e.vibratoDepth = pf (ids::vibDepth);
    e.vibratoRate = pf (ids::vibRate);
    e.vibratoDelay = pf (ids::vibDelay);
    e.slideAmount = pf (ids::slideAmt);
    e.slideSpread = pf (ids::slideSpread);
    e.pressureAmount = pf (ids::pressAmt);
    e.breath = pf (ids::breath);
    e.keepInput = pb (ids::keepExpr);
    e.seed = (int) apvts.state.getProperty ("seed", 1);
    return e;
}

GestureParams DarkMPEProcessor::readGestureParams() const
{
    GestureParams g;
    g.profile = (GestureProfile) pi (ids::gesture);
    g.amount = pf (ids::gAmount);
    g.depth = pf (ids::gDepth);
    g.riff = pf (ids::gRiff);
    g.glideTime = pf (ids::glideTime);
    g.key = pi (ids::key);
    g.scale = (scales::Scale) pi (ids::scale);
    g.style = (Style) pi (ids::style);
    g.seed = (int) apvts.state.getProperty ("seed", 1);
    return g;
}

CineParams DarkMPEProcessor::readCineParams() const
{
    CineParams c;
    c.motion = (Motion) pi (ids::cMotion);
    c.reharm = (Reharm) pi (ids::cReharm);
    c.voicing = (VoicingMode) pi (ids::cVoicing);
    c.shape = (GlideShape) pi (ids::cShape);
    c.voices = pi (ids::cVoices);
    c.lowPitch = pi (ids::cLow);
    c.highPitch = pi (ids::cHigh);
    c.glide = pf (ids::cGlide);
    c.anticipation = pf (ids::cAnticip);
    c.stagger = pf (ids::cStagger);
    c.swell = pf (ids::cSwell);
    c.tension = pf (ids::cTension);
    c.darkness = pf (ids::cDark);
    c.pulse = pf (ids::cPulse);
    c.arc = pf (ids::cArc);
    c.fall = pf (ids::cFall);
    c.sub = pb (ids::cSub);
    c.key = pi (ids::key);
    c.scale = (scales::Scale) pi (ids::scale);
    c.seed = (int) apvts.state.getProperty ("seed", 1);
    return c;
}

KitParams DarkMPEProcessor::readKitParams() const
{
    KitParams k;
    k.gen = readGenParams();
    k.pad = readCineParams();
    for (int l = 0; l < numLayers; ++l)
    {
        auto& lp = k.layers[(size_t) l];
        lp.on = pb (ids::layerOn[l]);
        if (ids::layerPattern[l] != nullptr)
        {
            lp.pattern = pi (ids::layerPattern[l]);
            lp.density = pf (ids::layerDensity[l]);
            lp.octave = pi (ids::layerOctave[l]);
        }
    }
    return k;
}

HarmonyParams DarkMPEProcessor::readHarmonyParams() const
{
    HarmonyParams h;
    h.key = pi (ids::key);
    h.scale = (scales::Scale) pi (ids::scale);
    h.progression = (Progression) pi (ids::cProg);
    h.chordLength = (ChordLength) pi (ids::cChordLen);
    h.bars = barChoices[std::clamp (pi (ids::bars), 0, 3)];
    h.darkness = pf (ids::cDark);
    h.seed = (int) apvts.state.getProperty ("seed", 1);
    return h;
}

// ------------------------------------------------------------------ expression preview
const juce::StringArray& DarkMPEProcessor::expressionParamIds()
{
    static const juce::StringArray list { ids::glideTime, ids::glideCurve, ids::detune, ids::vibDepth, ids::vibRate, ids::vibDelay,
                                          ids::slideAmt, ids::slideSpread, ids::pressAmt, ids::breath,
                                          ids::gesture, ids::gAmount, ids::gDepth, ids::gRiff, ids::style, ids::key, ids::scale };
    return list;
}

Phrase DarkMPEProcessor::expressionDemo() const
{
    // A pickup, a glide into a held note (glide, vibrato, detune drift), a run of 16ths (a Bend Riff candidate),
    // and a closing note, with the gestures of the current profile (shown more often than they occur, so the
    // profile's character is visible in four beats).
    Phrase p;
    p.lengthBeats = 4.0;
    auto add = [&p] (double start, double length, int pitch, int from)
    {
        Note n;
        n.start = start;
        n.length = length;
        n.pitch = pitch;
        n.glideFrom = from;
        n.velocity = 0.8f;
        n.exprSeed = mixSeed (4242, (uint64_t) std::lround (start * 64.0) + 1);
        p.notes.push_back (n);
    };
    const int key = pi (ids::key);
    const auto scale = (scales::Scale) pi (ids::scale);
    const int tonic = key + 60;
    auto deg = [&] (int d) { return scales::degreeToPitch (tonic, scale, scales::mapDegree (scale, d)); };
    add (0.0, 0.45, deg (-1), -1);
    add (0.5, 1.45, deg (2), deg (-1));
    add (2.0, 0.2, deg (1), -1);
    add (2.25, 0.2, deg (2), -1);
    add (2.5, 0.2, deg (1), -1);
    add (2.75, 0.2, deg (0), -1);
    add (3.0, 0.98, deg (0), -1);

    auto g = readGestureParams();
    g.amount = std::max (g.amount, 0.8f);
    g.riff = std::max (g.riff, 0.6f);
    g.seed = 4242;
    applyGestures (p, g, GestureRole::lead);
    shapeExpression (p, readExprParams());
    return p;
}

juce::String DarkMPEProcessor::describeExpression() const
{
    const auto profile = resolveProfile ((GestureProfile) pi (ids::gesture), (Style) pi (ids::style));
    return juce::String (gestureProfileNames[(int) profile]).toUpperCase() + "  -  vib " + juce::String (pf (ids::vibDepth), 2) + " st "
         + juce::String (pf (ids::vibRate), 1) + "/beat  -  detune " + juce::String (juce::roundToInt (pf (ids::detune))) + " c";
}

DarkMPEProcessor::Mode DarkMPEProcessor::getMode() const
{
    const int m = pi (ids::mode);
    return m == 3 ? Mode::kit : (m == 2 ? Mode::cinematic : (m == 1 ? Mode::transform : Mode::generate));
}

void DarkMPEProcessor::setMode (Mode m)
{
    if (auto* p = apvts.getParameter (ids::mode))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) (int) m));
}

// ------------------------------------------------------------------ rebuild
Stream DarkMPEProcessor::makeStream (int layer, Phrase phrase, const ExprParams& expr, bool mono) const
{
    // Keep every note-off strictly inside the loop so wrap-around never leaves hanging notes.
    const double L = phrase.lengthBeats;
    for (auto& n : phrase.notes)
        n.length = std::max (1.0 / 64.0, std::min (n.length, L - n.start - 1.0e-3));

    shapeExpression (phrase, expr);

    Stream s;
    s.layer = layer;
    s.mono = mono;
    if (mono)
    {
        s.events = toPlayEvents (renderMono (phrase, pi (ids::monoBend), false));
        s.startup = monoSetupEvents (pi (ids::monoBend));
    }
    else
    {
        RenderOptions ro;
        ro.pitchBendRange = pi (ids::pbRange);
        ro.includeZoneConfig = false; // sent by the audio thread at playback start
        s.events = toPlayEvents (renderMpe (phrase, ro));
        s.startup = zoneConfigEvents (ro.pitchBendRange);
    }
    s.phrase = std::move (phrase);
    return s;
}

void DarkMPEProcessor::buildKit (Rendered& r)
{
    const auto kp = readKitParams();
    const auto expr = readExprParams();
    const int focus = pi (ids::kFocus);
    r.lengthBeats = std::clamp (kp.gen.bars, 1, 16) * 4.0;

    const auto gestures = readGestureParams();
    for (auto& part : generateKit (kp))
    {
        const int l = (int) part.layer;
        switch (part.layer)
        {
            case Layer::lead:  applyGestures (part.phrase, gestures, GestureRole::lead); break;
            case Layer::bass:  applyGestures (part.phrase, gestures, GestureRole::bass); break;
            case Layer::arp:   applyGestures (part.phrase, gestures, GestureRole::arp); break;
            case Layer::stab:  applyGestures (part.phrase, gestures, GestureRole::chord); break;
            case Layer::siren:
            case Layer::pad:
            case Layer::count: break;
        }
        if (part.layer == Layer::lead || part.layer == Layer::bass || part.layer == Layer::arp)
            dmpe::humanize (part.phrase, pf (ids::humanize), getSeed() + l);
        const bool mono = ids::layerMono[l] != nullptr && pb (ids::layerMono[l]);
        if (l == focus)
            r.focus = (int) r.streams.size();
        r.streams.push_back (makeStream (l, std::move (part.phrase), expr, mono));
    }

    if (r.streams.empty()) // every layer off: an empty loop
    {
        Phrase silence;
        silence.lengthBeats = r.lengthBeats;
        r.streams.push_back (makeStream (0, silence, expr, false));
    }

    const auto chords = kitChords (kp.gen);
    for (size_t i = 0; i < chords.size() && i < 16; ++i)
        harmonyText << (i > 0 ? "  " : "") << chordSymbol (chords[i]);
}

void DarkMPEProcessor::rebuild()
{
    const juce::ScopedLock sl (rebuildLock);
    const bool messageThread = juce::MessageManager::existsAndIsCurrentThread();
    if (messageThread)
        updatePorts();

    auto r = std::make_shared<Rendered>();

    Phrase main;
    harmonyText = {};
    if (getMode() == Mode::kit)
        buildKit (*r);
    else if (getMode() == Mode::cinematic)
    {
        std::vector<Region> used;
        if (source.empty())
        {
            const auto hp = readHarmonyParams();
            main = cinematicRegions (generateProgression (hp), hp.bars * 4.0, readCineParams(), hp.key, &used);
        }
        else
            main = cinematic (source, readCineParams(), &used);

        for (size_t i = 0; i < used.size() && i < 24; ++i)
            harmonyText << (i > 0 ? "  " : "") << chordSymbol (used[i]);
    }
    else if (getMode() == Mode::transform && ! source.empty())
    {
        main = applyVoicing (source, readVoicingParams());
        applyGestures (main, readGestureParams(), GestureRole::chord); // the played expression is kept
    }
    else
    {
        main = generateMelody (readGenParams());
        applyGestures (main, readGestureParams(), GestureRole::lead);
    }

    // Melodic forms: one section per bar (the lead, and the kit layers that follow it).
    const bool melodic = getMode() == Mode::kit || getMode() == Mode::generate || (getMode() == Mode::transform && source.empty());
    if (melodic)
    {
        const int bars = barChoices[std::clamp (pi (ids::bars), 0, 3)];
        const auto secs = formSections ((Form) pi (ids::form), bars);
        for (size_t i = 0; i < secs.size(); ++i)
            r->sections.push_back ({ (double) i * 4.0, juce::String (secs[i].label) });
    }

    if (getMode() != Mode::kit)
    {
        if (getMode() != Mode::cinematic)
            dmpe::humanize (main, pf (ids::humanize), getSeed());

        r->lengthBeats = main.lengthBeats;
        const bool mono = getMode() == Mode::generate && pb (ids::leadMono);
        r->streams.push_back (makeStream (0, std::move (main), readExprParams(), mono));
    }

    {
        const juce::SpinLock::ScopedLockType sl (renderLock);
        if (rendered != nullptr)
            keepAlive.push_back (rendered);
        rendered = r;
    }

    // A render is freed here only once the audio thread has let go of it (it never drops the last reference).
    keepAlive.erase (std::remove_if (keepAlive.begin(), keepAlive.end(),
                                     [] (const auto& k) { return k.use_count() == 1; }),
                     keepAlive.end());

    if (! messageThread)
        triggerAsyncUpdate(); // ports and UI follow on the message thread
    else if (onRebuilt)
        onRebuilt();
}

std::shared_ptr<const Rendered> DarkMPEProcessor::getRendered() const
{
    const juce::SpinLock::ScopedLockType sl (renderLock);
    return rendered;
}

void DarkMPEProcessor::generateNew()
{
    if (getMode() == Mode::transform)
        setMode (Mode::generate); // the seed shapes the lead and the cinematic harmony, not a transform
    setSeed (juce::Random::getSystemRandom().nextInt (100000), 0);
}

void DarkMPEProcessor::mutate()
{
    setSeed (getSeed(), getVariation() + 1);
}

// ------------------------------------------------------------------ seed history / favourites
namespace
{
constexpr int maxHistory = 32;
juce::ValueTree entry (const char* type, int seed, int variation)
{
    juce::ValueTree e (type);
    e.setProperty ("seed", seed, nullptr);
    e.setProperty ("variation", variation, nullptr);
    return e;
}
bool sameEntry (const juce::ValueTree& e, int seed, int variation)
{
    return (int) e.getProperty ("seed") == seed && (int) e.getProperty ("variation") == variation;
}
} // namespace

int DarkMPEProcessor::getSeed() const { return (int) apvts.state.getProperty ("seed", 1); }
int DarkMPEProcessor::getVariation() const { return (int) apvts.state.getProperty ("variation", 0); }

juce::ValueTree DarkMPEProcessor::history()
{
    auto h = apvts.state.getOrCreateChildWithName ("History", nullptr);
    if (h.getNumChildren() == 0)
    {
        h.appendChild (entry ("Entry", getSeed(), getVariation()), nullptr);
        h.setProperty ("index", 0, nullptr);
    }
    return h;
}

void DarkMPEProcessor::setSeed (int seed, int variation)
{
    // Like an undo stack: going somewhere new drops the "forward" part.
    auto h = history();
    const int index = (int) h.getProperty ("index", 0);
    while (h.getNumChildren() > index + 1)
        h.removeChild (h.getNumChildren() - 1, nullptr);
    h.appendChild (entry ("Entry", seed, variation), nullptr);
    while (h.getNumChildren() > maxHistory)
        h.removeChild (0, nullptr);
    h.setProperty ("index", h.getNumChildren() - 1, nullptr);
    applySeed (seed, variation);
}

void DarkMPEProcessor::applySeed (int seed, int variation)
{
    apvts.state.setProperty ("seed", seed, nullptr);
    apvts.state.setProperty ("variation", variation, nullptr);
    rebuild();
}

bool DarkMPEProcessor::canGoBack() { return (int) history().getProperty ("index", 0) > 0; }
bool DarkMPEProcessor::canGoForward() { auto h = history(); return (int) h.getProperty ("index", 0) < h.getNumChildren() - 1; }

void DarkMPEProcessor::historyStep (int delta)
{
    auto h = history();
    const int index = juce::jlimit (0, h.getNumChildren() - 1, (int) h.getProperty ("index", 0) + delta);
    h.setProperty ("index", index, nullptr);
    const auto e = h.getChild (index);
    applySeed ((int) e.getProperty ("seed"), (int) e.getProperty ("variation"));
}

bool DarkMPEProcessor::isFavourite() const
{
    for (const auto& f : apvts.state.getChildWithName ("Favourites"))
        if (sameEntry (f, getSeed(), getVariation()))
            return true;
    return false;
}

void DarkMPEProcessor::toggleFavourite()
{
    auto favs = apvts.state.getOrCreateChildWithName ("Favourites", nullptr);
    for (int i = 0; i < favs.getNumChildren(); ++i)
        if (sameEntry (favs.getChild (i), getSeed(), getVariation()))
        {
            favs.removeChild (i, nullptr);
            return;
        }
    favs.appendChild (entry ("Fav", getSeed(), getVariation()), nullptr);
}

std::vector<std::pair<int, int>> DarkMPEProcessor::getFavourites() const
{
    std::vector<std::pair<int, int>> out;
    for (const auto& f : apvts.state.getChildWithName ("Favourites"))
        out.push_back ({ (int) f.getProperty ("seed"), (int) f.getProperty ("variation") });
    return out;
}

bool DarkMPEProcessor::loadMidi (const juce::File& file, juce::String& error)
{
    Phrase p;
    LoadInfo info;
    if (! loadMidiFile (file, p, error, &info))
        return false;

    {
        const juce::ScopedLock sl (rebuildLock);
        source = std::move (p);
        sourceInfo = info;
        sourceName = file.getFileNameWithoutExtension();
    }
    if (getMode() == Mode::generate)
        setMode (Mode::transform);
    rebuild();
    return true;
}

juce::String DarkMPEProcessor::describeSource() const
{
    if (source.empty())
        return {};
    juce::String s = sourceInfo.mpe ? "MPE (" + juce::String (sourceInfo.channels) + " ch)" : "MIDI";
    s << ", " << (int) source.notes.size() << " notes";
    if (sourceInfo.notesWithExpression > 0)
        s << ", " << sourceInfo.notesWithExpression << " with expression";
    s << (isMonophonic (source) ? " - melody" : " - chords");
    return s;
}

// ------------------------------------------------------------------ capture
void DarkMPEProcessor::setCapturing (bool shouldCapture)
{
    if (shouldCapture == capturing.load())
        return;

    if (shouldCapture)
    {
        captureCount.store (0);
        capturing.store (true);
    }
    else
    {
        capturing.store (false);
        finishCapture();
    }
}

void DarkMPEProcessor::finishCapture()
{
    const int count = std::min (captureCount.load (std::memory_order_acquire), (int) captureBuffer.size());

    double firstOn = -1.0;
    for (int i = 0; i < count && firstOn < 0.0; ++i)
    {
        const auto& e = captureBuffer[(size_t) i];
        if ((e.bytes[0] & 0xf0) == 0x90 && e.bytes[2] > 0)
            firstOn = e.beat;
    }
    if (firstOn < 0.0)
        return;

    // Everything (including MPE expression) is decoded by the same code as file import.
    const double origin = std::floor (firstOn / 4.0) * 4.0;
    juce::MidiMessageSequence seq;
    for (int i = 0; i < count; ++i)
    {
        const auto& e = captureBuffer[(size_t) i];
        seq.addEvent (juce::MidiMessage (e.bytes, e.size, 0.0), std::max (0.0, e.beat - origin));
    }

    LoadInfo info;
    auto p = phraseFromBeatSequence (seq, &info);
    if (p.empty())
        return;

    {
        const juce::ScopedLock sl (rebuildLock);
        source = std::move (p);
        sourceInfo = info;
        sourceName = "Captured";
    }
    if (getMode() == Mode::generate)
        setMode (Mode::transform);
    rebuild();
}

// ------------------------------------------------------------------ export
juce::String DarkMPEProcessor::suggestedFileName() const
{
    if (getMode() == Mode::kit)
        return "DarkMPE Kit " + juce::String (styleNames[pi (ids::style)]) + " " + scales::keyNames[pi (ids::key)]
             + " " + juce::String (getSeed());

    if (getMode() == Mode::cinematic)
        return "DarkMPE Cinematic "
             + (source.empty() ? juce::String (scales::keyNames[pi (ids::key)]) + " " + progressionNames[pi (ids::cProg)] : sourceName)
             + " " + motionNames[pi (ids::cMotion)];

    if (getMode() == Mode::transform && ! source.empty())
        return "DarkMPE " + sourceName + " " + juce::String (voicingNames[pi (ids::vMode)]);

    return "DarkMPE " + juce::String (styleNames[pi (ids::style)]) + " " + scales::keyNames[pi (ids::key)]
         + " " + juce::String ((int) apvts.state.getProperty ("seed", 0));
}

juce::MidiFile DarkMPEProcessor::streamFile (const Stream& stream) const
{
    RenderOptions ro;
    ro.pitchBendRange = pi (ids::pbRange);
    ro.includeZoneConfig = true;
    const auto seq = stream.mono ? renderMono (stream.phrase, pi (ids::monoBend), true) : renderMpe (stream.phrase, ro);
    const auto name = getMode() == Mode::kit ? suggestedFileName() + " " + layerNames[stream.layer] : suggestedFileName();
    return makeMidiFile (seq, lastBpm.load(), name);
}

juce::File DarkMPEProcessor::writeMidiFile (const juce::File& target) const
{
    auto r = getRendered();
    if (r == nullptr)
        return {};
    return dmpe::writeMidiFile (streamFile (r->focused()), target) ? target : juce::File();
}

juce::Array<juce::File> DarkMPEProcessor::writeAllStreams (const juce::File& target) const
{
    juce::Array<juce::File> written;
    auto r = getRendered();
    if (r == nullptr)
        return written;

    if (r->streams.size() == 1)
    {
        if (dmpe::writeMidiFile (streamFile (r->streams.front()), target))
            written.add (target);
        return written;
    }

    for (const auto& s : r->streams)
    {
        const auto file = target.getSiblingFile (target.getFileNameWithoutExtension() + " - " + layerNames[s.layer] + ".mid");
        if (dmpe::writeMidiFile (streamFile (s), file))
            written.add (file);
    }
    return written;
}

juce::StringArray DarkMPEProcessor::writeTempMidiForDrag() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("DarkMPE");
    dir.createDirectory();
    juce::StringArray paths;
    for (const auto& f : writeAllStreams (dir.getChildFile (juce::File::createLegalFileName (suggestedFileName()) + ".mid")))
        paths.add (f.getFullPathName());
    return paths;
}

// ------------------------------------------------------------------ audio
namespace
{
constexpr juce::uint8 noteOffStatus = 0x80;

void pitchBendCentre (juce::MidiBuffer& out, int ch, int sampleOffset)
{
    const juce::uint8 centre[3] = { (juce::uint8) (0xe0 | (ch - 1)), 0x00, 0x40 };
    out.addEvent (centre, 3, sampleOffset);
}

void addNoteOffs (std::array<std::array<juce::int8, 128>, 17>& table, juce::MidiBuffer& out, int sampleOffset)
{
    for (int ch = 1; ch <= 16; ++ch)
    {
        bool any = false;
        for (auto& emitted : table[(size_t) ch])
            if (emitted >= 0)
            {
                const juce::uint8 off[3] = { (juce::uint8) (noteOffStatus | (ch - 1)), (juce::uint8) emitted, 0 };
                out.addEvent (off, 3, sampleOffset);
                emitted = -1;
                any = true;
            }
        if (any)
            pitchBendCentre (out, ch, sampleOffset);
    }
}

void addNoteOffs (std::array<std::array<bool, 128>, 17>& table, juce::MidiBuffer& out, int sampleOffset)
{
    for (int ch = 1; ch <= 16; ++ch)
    {
        bool any = false;
        for (int n = 0; n < 128; ++n)
            if (table[(size_t) ch][(size_t) n])
            {
                const juce::uint8 off[3] = { (juce::uint8) (noteOffStatus | (ch - 1)), (juce::uint8) n, 0 };
                out.addEvent (off, 3, sampleOffset);
                table[(size_t) ch][(size_t) n] = false;
                any = true;
            }
        if (any)
            pitchBendCentre (out, ch, sampleOffset);
    }
}

bool isNoteOn (const juce::uint8* d) { return (d[0] & 0xf0) == 0x90 && d[2] > 0; }
bool isNoteOff (const juce::uint8* d) { return (d[0] & 0xf0) == 0x80 || ((d[0] & 0xf0) == 0x90 && d[2] == 0); }
} // namespace

void DarkMPEProcessor::allNotesOff (int sampleOffset)
{
    for (auto& l : layers)
        addNoteOffs (l.active, l.buffer, sampleOffset);
    addNoteOffs (hostActive, hostBuffer, sampleOffset);
    for (int ch = 1; ch <= 16; ++ch)
        mon[(size_t) ch].note.store (-1, std::memory_order_relaxed);
}

void DarkMPEProcessor::playStream (const Stream& s, double startPpq, double blockBeats, double ppqPerSample, int numSamples)
{
    auto& layer = layers[(size_t) s.layer];
    const double L = audioRendered->lengthBeats;
    if (startPpq + blockBeats <= 0.0 || L <= 0.0 || s.events.empty())
        return;

    // Walk the loop, splitting the block at the wrap point.
    double beatsDone = startPpq < 0.0 ? -startPpq : 0.0;
    double pos = startPpq >= 0.0 ? std::fmod (startPpq, L) : 0.0;

    while (beatsDone < blockBeats - 1.0e-12)
    {
        const double segEnd = std::min (L, pos + (blockBeats - beatsDone));
        auto it = std::lower_bound (s.events.begin(), s.events.end(), pos,
                                    [] (const PlayEvent& e, double t) { return e.beat < t; });

        for (; it != s.events.end() && it->beat < segEnd; ++it)
        {
            const auto* d = it->data;
            const int ch = (d[0] & 0x0f) + 1;
            const int sample = juce::jlimit (0, numSamples - 1, (int) ((beatsDone + (it->beat - pos)) / ppqPerSample));
            auto& emitted = layer.active[(size_t) ch][d[1]];

            if (isNoteOn (d) || isNoteOff (d))
            {
                juce::uint8 msg[3] = { d[0], d[1], d[2] };
                if (isNoteOn (d))
                {
                    if (emitted >= 0) // same source note again on this channel: end the previous one first
                    {
                        const juce::uint8 off[3] = { (juce::uint8) (noteOffStatus | (ch - 1)), (juce::uint8) emitted, 0 };
                        layer.buffer.addEvent (off, 3, sample);
                    }
                    msg[1] = (juce::uint8) juce::jlimit (0, 127, d[1] + transpose); // Key Trigger
                    emitted = (juce::int8) msg[1];
                }
                else
                {
                    if (emitted < 0)
                        continue; // its note-on was skipped by a jump
                    msg[1] = (juce::uint8) emitted; // the pitch it started on, whatever the transpose is now
                    emitted = -1;
                }
                layer.buffer.addEvent (msg, 3, sample);
                continue;
            }

            layer.buffer.addEvent (d, it->size, sample);
        }

        beatsDone += segEnd - pos;
        pos = segEnd >= L - 1.0e-12 ? 0.0 : segEnd;
    }
}

void DarkMPEProcessor::finishBlock (juce::MidiBuffer& hostMidi, int numSamples)
{
    // Host output: the focused layer (plus note-offs owed to the host from an earlier focus).
    hostBuffer.addEvents (layers[(size_t) hostLayer].buffer, 0, numSamples, 0);

    // MPE monitor: what each member channel is doing right now.
    const float range = pbRangeParam->load (std::memory_order_relaxed);
    for (const auto meta : hostBuffer)
    {
        const auto* d = meta.data;
        if (meta.numBytes < 2)
            continue;
        const int ch = (d[0] & 0x0f) + 1;
        auto& c = mon[(size_t) ch];
        const int type = d[0] & 0xf0;
        if (meta.numBytes == 3 && isNoteOn (d))
        {
            hostActive[(size_t) ch][d[1]] = true;
            c.note.store (d[1], std::memory_order_relaxed);
        }
        else if (meta.numBytes == 3 && isNoteOff (d))
        {
            hostActive[(size_t) ch][d[1]] = false;
            if (c.note.load (std::memory_order_relaxed) == d[1])
                c.note.store (-1, std::memory_order_relaxed);
        }
        else if (type == 0xe0 && meta.numBytes == 3)
            c.bend.store ((float) ((d[1] | (d[2] << 7)) - 8192) / 8192.0f * range, std::memory_order_relaxed);
        else if (type == 0xb0 && meta.numBytes == 3 && d[1] == 74)
            c.slide.store ((float) d[2] / 127.0f, std::memory_order_relaxed);
        else if (type == 0xd0)
            c.pressure.store ((float) d[1] / 127.0f, std::memory_order_relaxed);
    }

    // Each layer to its virtual port: Live receives true MPE from an MPE-enabled input.
    const double nowMs = juce::Time::getMillisecondCounterHiRes() + 2.0;
    for (int i = 0; i < PortHub::numPorts; ++i)
        ports.push (i, layers[(size_t) i].buffer, nowMs, sampleRate);

    hostMidi.clear();
    hostMidi.addEvents (hostBuffer, 0, numSamples, 0);
}

void DarkMPEProcessor::readKeys (const juce::MidiBuffer& midi, int trigger, double blockClock, double ppqPerSample)
{
    if (trigger == 0)
    {
        held.fill (false);
        heldCount = 0;
        lastKey = -1;
        transpose = 0;
        transposeShown.store (0, std::memory_order_relaxed);
        return;
    }

    for (const auto meta : midi)
    {
        if (meta.numBytes != 3)
            continue;
        const auto* d = meta.data;
        const int note = d[1];
        if (isNoteOn (d))
        {
            if (heldCount == 0)
                gateOrigin = blockClock + meta.samplePosition * ppqPerSample; // sample-accurate restart
            if (! held[(size_t) note])
            {
                held[(size_t) note] = true;
                ++heldCount;
            }
            keyOrder[(size_t) note] = ++keyCounter;
            lastKey = note;
        }
        else if (isNoteOff (d) && held[(size_t) note])
        {
            held[(size_t) note] = false;
            --heldCount;
            if (note == lastKey && heldCount > 0)
            {
                // Back to the most recent key still held.
                juce::uint32 newest = 0;
                for (int k = 0; k < 128; ++k)
                    if (held[(size_t) k] && keyOrder[(size_t) k] >= newest)
                    {
                        newest = keyOrder[(size_t) k];
                        lastKey = k;
                    }
            }
        }
    }

    // Transpose relative to the Key, folded to -5..+6 so the register stays put; it latches after release.
    if (lastKey >= 0)
    {
        int t = scales::mod (lastKey - (int) std::lround (keyParam->load (std::memory_order_relaxed)), 12);
        transpose = t > 6 ? t - 12 : t;
    }
    transposeShown.store (transpose, std::memory_order_relaxed);
}

void DarkMPEProcessor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    audio.clear();

    const int numSamples = audio.getNumSamples();
    for (auto& l : layers)
        l.buffer.clear();
    hostBuffer.clear();

    {
        const juce::SpinLock::ScopedTryLockType sl (renderLock);
        if (sl.isLocked() && audioRendered != rendered)
        {
            audioRendered = rendered;
            allNotesOff (0); // new phrase: silence what is sounding so nothing hangs
            hostLayer = audioRendered->focused().layer;
            hostMono.store (audioRendered->focused().mono, std::memory_order_relaxed);
        }
    }

    double bpm = 120.0;
    bool hostPlaying = false;
    juce::Optional<double> hostPpq;

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())
                bpm = *b;
            hostPlaying = pos->getIsPlaying();
            hostPpq = pos->getPpqPosition();
        }
    lastBpm.store (bpm);

    const double ppqPerSample = bpm / 60.0 / sampleRate;
    const double blockBeats = numSamples * ppqPerSample;

    // ---- incoming MIDI is only used for capture; it is not passed through
    if (capturing.load())
    {
        const double inputClock = (hostPlaying && hostPpq) ? *hostPpq : captureClock;
        for (const auto meta : midi)
        {
            if (meta.numBytes < 1 || meta.numBytes > 3 || meta.data[0] < 0x80 || meta.data[0] >= 0xf0)
                continue; // channel voice messages only (notes + MPE expression)

            const int idx = captureCount.load (std::memory_order_relaxed);
            if (idx < (int) captureBuffer.size())
            {
                auto& e = captureBuffer[(size_t) idx];
                e.beat = inputClock + meta.samplePosition * ppqPerSample;
                e.size = meta.numBytes;
                for (int b = 0; b < 3; ++b)
                    e.bytes[b] = b < meta.numBytes ? meta.data[b] : 0;
                captureCount.store (idx + 1, std::memory_order_release);
            }
        }
    }
    captureClock += blockBeats;

    // ---- key trigger (not while capturing: then the input is being recorded)
    const int trigger = capturing.load() ? 0 : (int) std::lround (trigModeParam->load (std::memory_order_relaxed));
    const double blockClock = (hostPlaying && hostPpq) ? *hostPpq : freeClock;
    freeClock += blockBeats;
    readKeys (midi, trigger, blockClock, ppqPerSample);

    // ---- transport
    bool playing = false;
    double startPpq = 0.0;
    if (trigger == 2)
    {
        // Gate: the loop plays while a key is held, from its start at the moment of the first key.
        playing = heldCount > 0;
        startPpq = blockClock - gateOrigin;
    }
    else if (hostPlaying && hostPpq)
    {
        playing = true;
        startPpq = *hostPpq;
        previewPpq = startPpq;
    }
    else if (previewParam->load (std::memory_order_relaxed) > 0.5f)
    {
        playing = true;
        startPpq = previewPpq;
        previewPpq += blockBeats;
    }

    playingBack.store (playing);
    if (playing && ! playedOnce.load (std::memory_order_relaxed))
        playedOnce.store (true, std::memory_order_relaxed);

    if (! playing || audioRendered == nullptr)
    {
        if (wasPlaying)
            allNotesOff (0);
        wasPlaying = false;
        finishBlock (midi, numSamples);
        return;
    }

    if (! wasPlaying || std::abs (startPpq - expectedPpq) > 1.0e-3)
    {
        allNotesOff (0);
        for (const auto& s : audioRendered->streams)
            for (const auto& e : s.startup)
                layers[(size_t) s.layer].buffer.addEvent (e.data, e.size, 0);
    }
    wasPlaying = true;
    expectedPpq = startPpq + blockBeats;

    const double L = audioRendered->lengthBeats;
    playheadBeats.store (startPpq >= 0.0 && L > 0.0 ? std::fmod (startPpq, L) : 0.0);

    for (const auto& s : audioRendered->streams)
        playStream (s, startPpq, blockBeats, ppqPerSample, numSamples);

    finishBlock (midi, numSamples);
}

// ------------------------------------------------------------------ state
void DarkMPEProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const juce::ScopedLock sl (rebuildLock);
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("Source"), nullptr);
    state.setProperty ("program", currentProgram.load(), nullptr);

    if (! source.empty())
    {
        RenderOptions ro;
        ro.includeZoneConfig = true;
        const auto mf = makeMidiFile (renderMpe (source, ro), 120.0, sourceName);
        juce::MemoryOutputStream mos;
        mf.writeTo (mos, 1);
        juce::ValueTree src ("Source");
        src.setProperty ("name", sourceName, nullptr);
        src.setProperty ("midi", mos.getMemoryBlock().toBase64Encoding(), nullptr);
        state.appendChild (src, nullptr);
    }

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void DarkMPEProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    const juce::ScopedLock sl (rebuildLock);
    auto state = juce::ValueTree::fromXml (*xml);
    const auto src = state.getChildWithName ("Source");
    if (src.isValid())
    {
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (src.getProperty ("midi").toString()))
        {
            juce::MemoryInputStream in (mb, false);
            Phrase p;
            juce::String err;
            LoadInfo info;
            if (loadMidiStream (in, p, err, &info))
            {
                source = std::move (p);
                sourceInfo = info;
                sourceName = src.getProperty ("name").toString();
            }
        }
        state.removeChild (src, nullptr);
    }

    apvts.replaceState (state);

    // replaceState skips parameters whose value "looks" unchanged (a bool at 0.87 is already "on"): set each one
    // exactly to the saved value.
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto saved = state.getChildWithProperty ("id", rp->paramID);
            if (! saved.isValid() || ! saved.hasProperty ("value"))
                continue;
            const float v = rp->convertTo0to1 ((float) saved.getProperty ("value"));
            if (std::abs (rp->getValue() - v) > 1.0e-6f)
                rp->setValueNotifyingHost (v);
        }

    currentProgram.store (juce::jlimit (0, getNumPrograms() - 1, (int) state.getProperty ("program", 0)));
    pendingProgram.store (-1);

    // A project is being loaded: this instance is in use, its ports can appear.
    portAllowed.store (true);
    rebuild();
}

// ------------------------------------------------------------------ presets
int DarkMPEProcessor::getNumPrograms() { return (int) presets::factory().size(); }

int DarkMPEProcessor::getCurrentProgram() { return currentProgram.load(); }

const juce::String DarkMPEProcessor::getProgramName (int index)
{
    const auto& f = presets::factory();
    return juce::isPositiveAndBelow (index, (int) f.size()) ? juce::String (f[(size_t) index].name) : juce::String();
}

void DarkMPEProcessor::setCurrentProgram (int index)
{
    // Hosts call this from any thread, and with the current index too (e.g. after restoring a project): only a
    // change is recorded, and the preset is applied on the message thread.
    if (! juce::isPositiveAndBelow (index, getNumPrograms()) || index == currentProgram.load())
        return;
    currentProgram.store (index);
    pendingProgram.store (index);
    triggerAsyncUpdate();
}

void DarkMPEProcessor::applyPendingProgram()
{
    const int index = pendingProgram.exchange (-1);
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;
    presets::apply (apvts, presets::factory()[(size_t) index]);
    apvts.state.setProperty ("program", index, nullptr);
    apvts.state.setProperty ("presetName", getProgramName (index), nullptr);
}

juce::String DarkMPEProcessor::getPresetName() const
{
    return apvts.state.getProperty ("presetName", "Init").toString();
}

bool DarkMPEProcessor::saveUserPreset (const juce::File& file)
{
    if (! presets::save (apvts, getSeed(), getVariation(), file))
        return false;
    apvts.state.setProperty ("presetName", file.getFileNameWithoutExtension(), nullptr);
    return true;
}

bool DarkMPEProcessor::loadUserPreset (const juce::File& file)
{
    int seed = getSeed(), variation = 0;
    if (! presets::load (apvts, file, seed, variation))
        return false;
    apvts.state.setProperty ("presetName", file.getFileNameWithoutExtension(), nullptr);
    setSeed (seed, variation); // rebuilds
    return true;
}

juce::AudioProcessorEditor* DarkMPEProcessor::createEditor()
{
    return new DarkMPEEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DarkMPEProcessor();
}
