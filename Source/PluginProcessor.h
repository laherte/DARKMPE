#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "PortHub.h"
#include "engine/CinematicEngine.h"
#include "engine/ExpressionShaper.h"
#include "engine/MidiFileIO.h"
#include "engine/MelodyGenerator.h"
#include "engine/MpeRenderer.h"
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

    const Stream& focused() const { return streams[(size_t) focus]; }
};

class DarkMPEProcessor : public juce::AudioProcessor,
                         private juce::AudioProcessorValueTreeState::Listener,
                         private juce::AsyncUpdater
{
public:
    enum class Mode { generate, transform, cinematic };

    DarkMPEProcessor();
    ~DarkMPEProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "DarkMPE"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
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
    juce::String getHarmonyText() const { return harmonyText; } // chord symbols of the cinematic output

    void setCapturing (bool shouldCapture);
    bool isCapturing() const { return capturing.load(); }

    juce::File writeMidiFile (const juce::File& target) const; // returns the written file or {}
    juce::File writeTempMidiForDrag() const;
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
    bool isPortOpen() const { return ports.isOpen (0); }

    void refreshNow() { rebuild(); } // synchronous rebuild (tests / immediate UI actions)

    double getPlayheadBeats() const { return playheadBeats.load(); }
    bool isPlayingBack() const { return playingBack.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { rebuild(); }
    void rebuild();
    void finishCapture();
    juce::ValueTree history();
    void historyStep (int delta);
    void applySeed (int seed, int variation);

    dmpe::GenParams readGenParams() const;
    dmpe::VoicingParams readVoicingParams() const;
    dmpe::ExprParams readExprParams() const;
    dmpe::CineParams readCineParams() const;
    dmpe::HarmonyParams readHarmonyParams() const;
    float pf (const char* id) const;
    int pi (const char* id) const;
    bool pb (const char* id) const;

    Stream makeStream (int layer, dmpe::Phrase phrase, const dmpe::ExprParams& expr) const;

    // ---- audio thread
    using NoteTable = std::array<std::array<bool, 128>, 17>;
    void allNotesOff (int sampleOffset);  // every layer (to its port) and the host output
    void playStream (const Stream& s, double startPpq, double blockBeats, double ppqPerSample, int numSamples);
    void finishBlock (juce::MidiBuffer& hostMidi, int numSamples); // host out + monitor + virtual ports
    void updatePorts();                                            // message thread

    // ---- virtual MIDI ports ("DarkMPE Out" + KIT layers)
    PortHub ports;
    juce::String portName;
    std::atomic<bool> portAllowed { false }; // only after prepareToPlay: never open ports during plugin scans
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
        NoteTable active {};
        juce::MidiBuffer buffer; // preallocated in prepareToPlay
    };
    std::array<LayerState, PortHub::numPorts> layers;
    NoteTable hostActive {};
    juce::MidiBuffer hostBuffer;
    int hostLayer = 0;

    // parameters read by the audio thread, looked up once
    std::atomic<float>* pbRangeParam = nullptr;
    std::atomic<float>* previewParam = nullptr;

    std::shared_ptr<const Rendered> audioRendered;
    double sampleRate = 44100.0;
    double expectedPpq = 0.0;
    double previewPpq = 0.0;
    bool wasPlaying = false;
    std::atomic<double> playheadBeats { 0.0 };
    std::atomic<bool> playingBack { false };
    std::atomic<double> lastBpm { 120.0 };

    // ---- capture (audio thread writes, message thread reads after stop)
    struct CapturedEvent { double beat; juce::uint8 bytes[3]; int size; };
    std::array<CapturedEvent, 16384> captureBuffer {};
    std::atomic<int> captureCount { 0 };
    std::atomic<bool> capturing { false };
    double captureClock = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DarkMPEProcessor)
};
