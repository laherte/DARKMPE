#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "engine/MidiFileIO.h"
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
constexpr const char* pbRange = "pbRange";
constexpr const char* preview = "preview";
constexpr const char* virtualOut = "virtualOut";
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

    choice (ids::mode, "Mode", { "Generate", "Transform", "Cinematic" }, 0);

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
    integer (ids::cVoices, "Cine Voices", 2, 6, 6);
    integer (ids::cLow, "Cine Low Note", 24, 60, 33);
    integer (ids::cHigh, "Cine High Note", 60, 108, 88);
    flt (ids::cGlide, "Cine Glide", 0.05f, 1.0f, 0.5f);
    flt (ids::cAnticip, "Cine Anticipation", 0.0f, 1.0f, 0.5f);
    flt (ids::cStagger, "Cine Stagger", 0.0f, 1.0f, 0.4f);
    flt (ids::cSwell, "Cine Swell", 0.0f, 1.0f, 0.7f);

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
    integer (ids::pbRange, "Bend Range", 1, 96, 48);
    boolean (ids::preview, "Preview", false);
    boolean (ids::virtualOut, "Virtual MIDI Out", true);

    return l;
}

static std::atomic<int> instanceCounter { 0 };

DarkMPEProcessor::DarkMPEProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DarkMPE", createLayout())
{
    const int instance = ++instanceCounter;
    portName = instance == 1 ? juce::String ("DarkMPE Out") : "DarkMPE Out " + juce::String (instance);

    apvts.state.setProperty ("seed", juce::Random::getSystemRandom().nextInt (100000), nullptr);
    apvts.state.setProperty ("variation", 0, nullptr);

    for (auto* p : getParameters())
        if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            if (wp->paramID != ids::preview)
                apvts.addParameterListener (wp->paramID, this);

    rebuild();
}

DarkMPEProcessor::~DarkMPEProcessor()
{
    cancelPendingUpdate();
    std::unique_ptr<juce::MidiOutput> old;
    {
        const juce::SpinLock::ScopedLockType sl (portLock);
        old = std::move (port);
        portOpen.store (false);
    }
    if (old != nullptr)
    {
        old->stopBackgroundThread();
        for (int ch = 1; ch <= 16; ++ch)
            old->sendMessageNow (juce::MidiMessage::allNotesOff (ch));
    }
}

void DarkMPEProcessor::updatePort()
{
    const bool want = portAllowed.load() && pb (ids::virtualOut);
    if (want == (port != nullptr))
        return;

    std::unique_ptr<juce::MidiOutput> fresh;
    if (want)
    {
        fresh = juce::MidiOutput::createNewDevice (portName);
        if (fresh != nullptr)
            fresh->startBackgroundThread();
    }

    std::unique_ptr<juce::MidiOutput> old;
    {
        const juce::SpinLock::ScopedLockType sl (portLock);
        old = std::move (port);
        port = std::move (fresh);
        portOpen.store (port != nullptr);
    }

    if (old != nullptr)
    {
        old->stopBackgroundThread();
        for (int ch = 1; ch <= 16; ++ch)
            old->sendMessageNow (juce::MidiMessage::allNotesOff (ch));
    }
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

    // The port is created on the message thread once the host actually runs us.
    if (! portAllowed.exchange (true))
        triggerAsyncUpdate();
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
    c.seed = (int) apvts.state.getProperty ("seed", 1);
    return c;
}

DarkMPEProcessor::Mode DarkMPEProcessor::getMode() const
{
    const int m = pi (ids::mode);
    return m == 2 ? Mode::cinematic : (m == 1 ? Mode::transform : Mode::generate);
}

void DarkMPEProcessor::setMode (Mode m)
{
    if (auto* p = apvts.getParameter (ids::mode))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) (int) m));
}

// ------------------------------------------------------------------ rebuild
void DarkMPEProcessor::rebuild()
{
    updatePort();

    auto r = std::make_shared<Rendered>();

    if (getMode() == Mode::cinematic)
        r->phrase = cinematic (source.empty() ? demoProgression (pi (ids::key), (scales::Scale) pi (ids::scale),
                                                                 barChoices[std::clamp (pi (ids::bars), 0, 3)])
                                              : source,
                               readCineParams());
    else if (getMode() == Mode::transform && ! source.empty())
        r->phrase = applyVoicing (source, readVoicingParams());
    else
        r->phrase = generateMelody (readGenParams());

    // Keep every note-off strictly inside the loop so wrap-around never leaves hanging notes.
    const double L = r->phrase.lengthBeats;
    for (auto& n : r->phrase.notes)
        n.length = std::max (1.0 / 64.0, std::min (n.length, L - n.start - 1.0e-3));

    shapeExpression (r->phrase, readExprParams());

    RenderOptions ro;
    ro.pitchBendRange = pi (ids::pbRange);
    ro.includeZoneConfig = false; // the audio thread sends it at playback start
    r->sequence = renderMpe (r->phrase, ro);
    r->lengthBeats = L;

    {
        const juce::SpinLock::ScopedLockType sl (renderLock);
        if (rendered != nullptr)
            keepAlive.push_back (rendered);
        rendered = r;
    }
    while (keepAlive.size() > 8)
        keepAlive.pop_front();

    if (onRebuilt)
        onRebuilt();
}

std::shared_ptr<const Rendered> DarkMPEProcessor::getRendered() const
{
    const juce::SpinLock::ScopedLockType sl (renderLock);
    return rendered;
}

void DarkMPEProcessor::generateNew()
{
    apvts.state.setProperty ("seed", juce::Random::getSystemRandom().nextInt (100000), nullptr);
    apvts.state.setProperty ("variation", 0, nullptr);
    if (getMode() != Mode::generate)
        setMode (Mode::generate);
    rebuild();
}

void DarkMPEProcessor::mutate()
{
    apvts.state.setProperty ("variation", (int) apvts.state.getProperty ("variation", 0) + 1, nullptr);
    rebuild();
}

bool DarkMPEProcessor::loadMidi (const juce::File& file, juce::String& error)
{
    Phrase p;
    LoadInfo info;
    if (! loadMidiFile (file, p, error, &info))
        return false;

    source = std::move (p);
    sourceInfo = info;
    sourceName = file.getFileNameWithoutExtension();
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

    source = std::move (p);
    sourceInfo = info;
    sourceName = "Captured";
    if (getMode() == Mode::generate)
        setMode (Mode::transform);
    rebuild();
}

// ------------------------------------------------------------------ export
juce::String DarkMPEProcessor::suggestedFileName() const
{
    if (getMode() == Mode::cinematic)
        return "DarkMPE Cinematic " + (source.empty() ? juce::String (scales::keyNames[pi (ids::key)]) : sourceName)
             + " " + motionNames[pi (ids::cMotion)];

    if (getMode() == Mode::transform && ! source.empty())
        return "DarkMPE " + sourceName + " " + juce::String (voicingNames[pi (ids::vMode)]);

    return "DarkMPE " + juce::String (styleNames[pi (ids::style)]) + " " + scales::keyNames[pi (ids::key)]
         + " " + juce::String ((int) apvts.state.getProperty ("seed", 0));
}

juce::File DarkMPEProcessor::writeMidiFile (const juce::File& target) const
{
    auto r = getRendered();
    if (r == nullptr)
        return {};

    RenderOptions ro;
    ro.pitchBendRange = pi (ids::pbRange);
    ro.includeZoneConfig = true;
    const auto seq = renderMpe (r->phrase, ro);
    const auto mf = makeMidiFile (seq, lastBpm.load(), suggestedFileName());
    return dmpe::writeMidiFile (mf, target) ? target : juce::File();
}

juce::File DarkMPEProcessor::writeTempMidiForDrag() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("DarkMPE");
    dir.createDirectory();
    const auto name = juce::File::createLegalFileName (suggestedFileName()) + ".mid";
    return writeMidiFile (dir.getChildFile (name));
}

// ------------------------------------------------------------------ audio
void DarkMPEProcessor::sendAllNotesOff (juce::MidiBuffer& out, int sampleOffset)
{
    for (int ch = 1; ch <= 16; ++ch)
    {
        bool any = false;
        for (int n = 0; n < 128; ++n)
            if (active[(size_t) ch][(size_t) n])
            {
                out.addEvent (juce::MidiMessage::noteOff (ch, n), sampleOffset);
                active[(size_t) ch][(size_t) n] = false;
                any = true;
            }
        if (any)
            out.addEvent (juce::MidiMessage::pitchWheel (ch, 8192), sampleOffset);
        mon[(size_t) ch].note.store (-1, std::memory_order_relaxed);
    }
}

void DarkMPEProcessor::finishBlock (juce::MidiBuffer& hostMidi, juce::MidiBuffer& out)
{
    // MPE monitor: what each member channel is doing right now.
    const float range = (float) pi (ids::pbRange);
    for (const auto meta : out)
    {
        const auto& m = meta.getMessage();
        const int ch = m.getChannel();
        if (ch < 1 || ch > 16)
            continue;
        auto& c = mon[(size_t) ch];
        if (m.isNoteOn())
            c.note.store (m.getNoteNumber(), std::memory_order_relaxed);
        else if (m.isNoteOff() && c.note.load (std::memory_order_relaxed) == m.getNoteNumber())
            c.note.store (-1, std::memory_order_relaxed);
        else if (m.isPitchWheel())
            c.bend.store ((float) (m.getPitchWheelValue() - 8192) / 8192.0f * range, std::memory_order_relaxed);
        else if (m.isController() && m.getControllerNumber() == 74)
            c.slide.store ((float) m.getControllerValue() / 127.0f, std::memory_order_relaxed);
        else if (m.isChannelPressure())
            c.pressure.store ((float) m.getChannelPressureValue() / 127.0f, std::memory_order_relaxed);
    }

    // Same stream to the virtual port: Live receives true MPE from an MPE-enabled input.
    if (! out.isEmpty())
    {
        const juce::SpinLock::ScopedTryLockType sl (portLock);
        if (sl.isLocked() && port != nullptr)
            port->sendBlockOfMessages (out, juce::Time::getMillisecondCounterHiRes() + 1.0, sampleRate);
    }

    hostMidi.swapWith (out);
}

void DarkMPEProcessor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    audio.clear();

    const int numSamples = audio.getNumSamples();
    juce::MidiBuffer out;

    {
        const juce::SpinLock::ScopedTryLockType sl (renderLock);
        if (sl.isLocked() && audioRendered != rendered)
        {
            audioRendered = rendered;
            sendAllNotesOff (out, 0); // new phrase: silence what is sounding so nothing hangs
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
    midi.clear();

    // ---- transport
    bool playing = false;
    double startPpq = 0.0;
    if (hostPlaying && hostPpq)
    {
        playing = true;
        startPpq = *hostPpq;
        previewPpq = startPpq;
    }
    else if (pb (ids::preview))
    {
        playing = true;
        startPpq = previewPpq;
        previewPpq += blockBeats;
    }

    playingBack.store (playing);

    if (! playing || audioRendered == nullptr)
    {
        if (wasPlaying)
            sendAllNotesOff (out, 0);
        wasPlaying = false;
        finishBlock (midi, out);
        return;
    }

    if (! wasPlaying || std::abs (startPpq - expectedPpq) > 1.0e-3)
    {
        sendAllNotesOff (out, 0);
        for (const auto meta : juce::MPEMessages::setLowerZone (15, pi (ids::pbRange), 2))
            out.addEvent (meta.getMessage(), 0);
    }
    wasPlaying = true;
    expectedPpq = startPpq + blockBeats;

    const auto& seq = audioRendered->sequence;
    const double L = audioRendered->lengthBeats;

    if (startPpq + blockBeats <= 0.0 || L <= 0.0)
    {
        finishBlock (midi, out);
        return;
    }

    // Walk the loop, splitting the block at the wrap point.
    double beatsDone = 0.0;
    double pos = startPpq >= 0.0 ? std::fmod (startPpq, L) : 0.0;
    if (startPpq < 0.0)
        beatsDone = -startPpq;

    playheadBeats.store (pos);

    while (beatsDone < blockBeats - 1.0e-12)
    {
        const double segEnd = std::min (L, pos + (blockBeats - beatsDone));
        for (int i = seq.getNextIndexAtTime (pos); i < seq.getNumEvents(); ++i)
        {
            const auto& m = seq.getEventPointer (i)->message;
            const double t = m.getTimeStamp();
            if (t >= segEnd)
                break;

            const int sample = juce::jlimit (0, numSamples - 1, (int) ((beatsDone + (t - pos)) / ppqPerSample));
            if (m.isNoteOn())
                active[(size_t) m.getChannel()][(size_t) m.getNoteNumber()] = true;
            else if (m.isNoteOff())
            {
                if (! active[(size_t) m.getChannel()][(size_t) m.getNoteNumber()])
                    continue; // its note-on was skipped by a jump
                active[(size_t) m.getChannel()][(size_t) m.getNoteNumber()] = false;
            }
            out.addEvent (m, sample);
        }

        beatsDone += segEnd - pos;
        pos = segEnd >= L - 1.0e-12 ? 0.0 : segEnd;
    }

    finishBlock (midi, out);
}

// ------------------------------------------------------------------ state
void DarkMPEProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("Source"), nullptr);

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
    rebuild();
}

juce::AudioProcessorEditor* DarkMPEProcessor::createEditor()
{
    return new DarkMPEEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DarkMPEProcessor();
}
