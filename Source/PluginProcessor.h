#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "PortHub.h"
#include "engine/CinematicEngine.h"
#include "engine/ExpressionShaper.h"
#include "engine/KitGenerator.h"
#include "engine/MidiFileIO.h"
#include "engine/MidiLearn.h"
#include "engine/MelodyGenerator.h"
#include "engine/MpeRenderer.h"
#include "engine/VoiceGenerator.h"
#include "engine/VoicingEngine.h"
#include "model/Phrase.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

// One output stream of a render: the main line, or one layer of a KIT. Each stream has its own
// MPE zone, its own virtual port and its own note bookkeeping on the audio thread.
struct Stream
{
    int layer = 0;                         // 0 = main output (also the KIT lead), 1.. = other KIT layers
    dmpe::Phrase phrase;
    std::vector<dmpe::PlayEvent> events;   // the loop, beat-timed and sorted
    std::vector<dmpe::PlayEvent> startup;  // sent when playback (re)starts: zone configuration / bend range
    bool mono = false;
};

// Result of a rebuild, shared read-only between UI and audio thread.
struct Rendered
{
    std::vector<Stream> streams;
    int focus = 0;           // index into streams: drawn in the UI, sent to the host MIDI out
    double lengthBeats = 4.0;
    std::vector<std::pair<double, juce::String>> sections; // phrase form: where A, B, C... start (beats)
    std::vector<std::pair<double, juce::String>> chords;   // GENERATE / KIT: the chords the lines follow (beats)
    dmpe::Phrase ghost;                                    // MIDI Learn: your lead, drawn under the output

    const Stream& focused() const { return streams[(size_t) focus]; }
};

class DarkMPEProcessor : public juce::AudioProcessor,
                         private juce::AudioProcessorValueTreeState::Listener,
                         private juce::AsyncUpdater,
                         private juce::Timer
{
public:
    enum class Mode { generate, transform, cinematic, kit };

    DarkMPEProcessor();
    ~DarkMPEProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "DarkMPE MK2"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Factory presets are the host's programs.
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- message-thread API used by the editor
    juce::AudioProcessorValueTreeState apvts;

    std::shared_ptr<const Rendered> getRendered() const;
    Mode getMode() const;
    void setMode (Mode m);

    void generateNew();          // new random seed
    void mutate();               // keep the motif, vary the later bars

    // Seed history (NEW / MUTATE / typed seeds, last 32, saved with the project) and favourites.
    int getSeed() const;
    int getVariation() const;
    void setSeed (int seed, int variation);  // goes to a seed and records it in the history
    bool canGoBack();
    bool canGoForward();
    void historyBack() { historyStep (-1); }
    void historyForward() { historyStep (1); }
    bool isFavourite() const;
    void toggleFavourite();
    std::vector<std::pair<int, int>> getFavourites() const; // {seed, variation}
    bool loadMidi (const juce::File& file, juce::String& error);
    juce::String describeSource() const; // e.g. "MPE - 14 notes, 12 with expression"
    bool hasSource() const { return ! source.empty(); }
    juce::String getSourceName() const { return sourceName; }
    juce::String getHarmonyText() const { return harmonyText; } // chord symbols of the cinematic output / the lead's chords
    bool isVoiceEngine() const; // GENERATE's lead engine is Voice

    // Presets: factory (also the host's programs) and user files (~/Music/DarkMPE/Presets/*.dmpreset).
    juce::String getPresetName() const;
    bool saveUserPreset (const juce::File& file);
    bool loadUserPreset (const juce::File& file);

    void setCapturing (bool shouldCapture);
    bool isCapturing() const { return capturing.load() && ! learning.load(); }

    // MIDI Learn: listens to 4 bars of what plays on the plugin's track (from the first note, on the host's bars),
    // understands what `kind` says it is and applies it (Harmony = MIDI In, Motif = Your Lead / Your Rhythm,
    // Key / Scale / Rate / Bars). What was learned is saved with the project.
    void startLearn (dmpe::LearnKind kind);
    void cancelLearn();
    bool isLearning() const { return learning.load(); }
    dmpe::LearnKind getLearnKind() const { return learnKind; }
    juce::String getLearnStatus() const { return learnStatus; } // progress while listening, then the result
    bool hasLearned (dmpe::LearnKind kind) const;
    void forgetLearned (dmpe::LearnKind kind);
    void updateLearn(); // message thread: progress, and the analysis once 4 bars were heard (the timer calls it)

    juce::File writeMidiFile (const juce::File& target) const; // the focused stream; returns the written file or {}
    juce::Array<juce::File> writeAllStreams (const juce::File& target) const; // KIT: "<name> - <Layer>.mid" per layer
    juce::StringArray writeTempMidiForDrag() const;             // one file, or one per KIT layer
    juce::String suggestedFileName() const;

    std::function<void()> onRebuilt; // called on the message thread after every rebuild

    // ---- MPE monitor (written by the audio thread, read by the UI)
    struct ChannelMonitor
    {
        std::atomic<int> note { -1 };
        std::atomic<float> bend { 0.0f }; // semitones
        std::atomic<float> slide { 0.0f };
        std::atomic<float> pressure { 0.0f };
    };
    const ChannelMonitor& monitor (int channel) const { return mon[(size_t) juce::jlimit (1, 16, channel)]; }

    juce::String getPortName() const { return portName; }
    juce::String getLayerPortName (int layer) const; // "DarkMPE MK2 Out" for the lead / main output
    bool isLayerPortOpen (int layer) const { return ports.isOpen (layer); }
    int getFocusLayer() const;
    void setFocusLayer (int layer);
    int getTranspose() const { return transposeShown.load (std::memory_order_relaxed); } // Key Trigger, semitones
    bool isHostMono() const { return hostMono.load (std::memory_order_relaxed); } // host out is the mono (channel 1) line
    bool isPortOpen() const { return ports.isOpen (0); }

    void refreshNow() { applyPendingProgram(); rebuild(); } // synchronous (message thread: UI actions, tests)

    // The virtual ports open only once the plugin is really in use (editor opened, project loaded, loop played),
    // never while a host is just scanning or validating it.
    void allowPorts();

    double getPlayheadBeats() const { return playheadBeats.load(); }
    bool isPlayingBack() const { return playingBack.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static std::array<std::array<juce::int8, 128>, 17> makeEmittedTable()
    {
        std::array<std::array<juce::int8, 128>, 17> t;
        for (auto& row : t)
            row.fill (-1);
        return t;
    }
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { applyPendingProgram(); rebuild(); }
    void timerCallback() override;
    void applyPendingProgram(); // message thread: a program change requested by the host (from any thread)
    void rebuild();
    void finishCapture();
    struct CaptureWindow { int count = 0; double origin = 0.0; double phase = 0.0; bool found = false; };
    CaptureWindow captureWindow() const;                             // from the first note-on, on the host's bars
    dmpe::Phrase capturedPhrase (const CaptureWindow& w, double length, bool loop, dmpe::LoadInfo* info = nullptr) const;
    void finishLearn();
    void setLearned (std::shared_ptr<const dmpe::Learned> l, bool toState); // + the state tree
    dmpe::Learned currentLearned() const;
    void loadLearnedFromState();
    juce::ValueTree history();
    void historyStep (int delta);
    void applySeed (int seed, int variation);

    dmpe::GenParams readGenParams() const;
    dmpe::VoicingParams readVoicingParams() const;
    dmpe::ExprParams readExprParams() const;
    dmpe::CineParams readCineParams() const;
    dmpe::HarmonyParams readHarmonyParams() const;
    dmpe::KitParams readKitParams() const;
    void buildKit (Rendered& r);
    void describeChords (Rendered& r, const std::vector<dmpe::Region>& chords); // chord symbols: status text + piano roll
    juce::MidiFile streamFile (const Stream& s) const;
    float pf (const char* id) const;
    int pi (const char* id) const;
    bool pb (const char* id) const;

    Stream makeStream (int layer, dmpe::Phrase phrase, const dmpe::ExprParams& expr, bool mono) const;

    // ---- audio thread
    using NoteTable = std::array<std::array<bool, 128>, 17>;             // sounding (channel, pitch)
    using EmittedTable = std::array<std::array<juce::int8, 128>, 17>;   // pitch sent per (channel, source pitch), -1 = off
    void allNotesOff (int sampleOffset);  // every layer (to its port) and the host output
    void playStream (const Stream& s, double startPpq, double blockBeats, double ppqPerSample, int numSamples);
    void finishBlock (juce::MidiBuffer& hostMidi, int numSamples); // host out + monitor + virtual ports
    void updatePorts();                                            // message thread

    // ---- virtual MIDI ports ("DarkMPE MK2 Out" + KIT layers: MK2 ports never clash with DarkMPE 1.x ones)
    PortHub ports;
    juce::String portName;
    int instanceNumber = 1;
    std::array<bool, PortHub::numPorts> layerPortWanted {}; // a KIT layer's port stays once it was used
    std::atomic<bool> portAllowed { false }; // see allowPorts(): never during a plugin scan
    std::atomic<bool> playedOnce { false };  // set by the audio thread the first time the loop plays

    // Programs can be changed by the host from any thread: the request is applied on the message thread.
    std::atomic<int> currentProgram { 0 }, pendingProgram { -1 };

    // One rebuild at a time, and the loaded source is only touched under this lock (hosts may restore state
    // from a background thread).
    juce::CriticalSection rebuildLock;
    std::array<ChannelMonitor, 17> mon;

    // ---- shared state
    mutable juce::SpinLock renderLock;
    std::shared_ptr<const Rendered> rendered;
    std::vector<std::shared_ptr<const Rendered>> keepAlive; // old renders, freed on the message thread only

    dmpe::Phrase source;
    juce::String sourceName;
    juce::String harmonyText;
    dmpe::LoadInfo sourceInfo;

    // ---- audio thread
    struct LayerState
    {
        EmittedTable active = makeEmittedTable(); // transposed notes are turned off at the pitch they started on
        juce::MidiBuffer buffer; // preallocated in prepareToPlay
    };
    std::array<LayerState, PortHub::numPorts> layers;
    NoteTable hostActive {};
    juce::MidiBuffer hostBuffer;
    int hostLayer = 0;

    // parameters read by the audio thread, looked up once
    std::atomic<float>* pbRangeParam = nullptr;
    std::atomic<float>* previewParam = nullptr;
    std::atomic<float>* trigModeParam = nullptr;
    std::atomic<float>* keyParam = nullptr;

    // ---- key trigger (audio thread)
    void readKeys (const juce::MidiBuffer& midi, int trigger, double blockClock, double ppqPerSample);
    std::array<bool, 128> held {};
    std::array<juce::uint32, 128> keyOrder {};
    juce::uint32 keyCounter = 0;
    int heldCount = 0, lastKey = -1, transpose = 0;
    double gateOrigin = 0.0, freeClock = 0.0;
    std::atomic<int> transposeShown { 0 };

    std::shared_ptr<const Rendered> audioRendered;
    double sampleRate = 44100.0;
    double expectedPpq = 0.0;
    double previewPpq = 0.0;
    bool wasPlaying = false;
    std::atomic<double> playheadBeats { 0.0 };
    std::atomic<bool> playingBack { false };
    std::atomic<bool> hostMono { false };
    std::atomic<double> lastBpm { 120.0 };

    // ---- capture (audio thread writes, message thread reads after stop)
    // `beat` runs on without jumps (the host's loop may jump back); `host` is the host position, -1 when stopped.
    struct CapturedEvent { double beat; double host; juce::uint8 bytes[3]; int size; };
    std::array<CapturedEvent, 16384> captureBuffer {};
    std::atomic<int> captureCount { 0 };
    std::atomic<bool> capturing { false };
    double captureClock = 0.0;
    std::atomic<double> captureClockNow { 0.0 }; // captureClock at the end of the last block

    // ---- MIDI Learn (message thread; `learned` is replaced under rebuildLock)
    std::atomic<bool> learning { false };
    dmpe::LearnKind learnKind = dmpe::LearnKind::chords;
    juce::String learnStatus;
    std::shared_ptr<const dmpe::Learned> learned;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DarkMPEProcessor)
};
