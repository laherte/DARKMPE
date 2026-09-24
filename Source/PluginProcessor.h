#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "engine/CinematicEngine.h"
#include "engine/ExpressionShaper.h"
#include "engine/MidiFileIO.h"
#include "engine/MelodyGenerator.h"
#include "engine/VoicingEngine.h"
#include "model/Phrase.h"

#include <array>
#include <atomic>
#include <deque>
#include <memory>

// Result of a rebuild, shared read-only between UI and audio thread.
struct Rendered
{
    dmpe::Phrase phrase;
    juce::MidiMessageSequence sequence; // timestamps in beats
    double lengthBeats = 4.0;
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
    bool loadMidi (const juce::File& file, juce::String& error);
    juce::String describeSource() const; // e.g. "MPE - 14 notes, 12 with expression"
    bool hasSource() const { return ! source.empty(); }
    juce::String getSourceName() const { return sourceName; }

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
    bool isPortOpen() const { return portOpen.load(); }

    void refreshNow() { rebuild(); } // synchronous rebuild (tests / immediate UI actions)

    double getPlayheadBeats() const { return playheadBeats.load(); }
    bool isPlayingBack() const { return playingBack.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { rebuild(); }
    void rebuild();
    void finishCapture();

    dmpe::GenParams readGenParams() const;
    dmpe::VoicingParams readVoicingParams() const;
    dmpe::ExprParams readExprParams() const;
    dmpe::CineParams readCineParams() const;
    float pf (const char* id) const;
    int pi (const char* id) const;
    bool pb (const char* id) const;

    void sendAllNotesOff (juce::MidiBuffer& out, int sampleOffset);
    void finishBlock (juce::MidiBuffer& hostMidi, juce::MidiBuffer& out); // monitor + virtual port + host
    void updatePort();                                                   // message thread

    // ---- virtual MIDI port "DarkMPE Out"
    juce::SpinLock portLock;
    std::unique_ptr<juce::MidiOutput> port;
    juce::String portName;
    std::atomic<bool> portOpen { false };
    std::atomic<bool> portAllowed { false }; // only after prepareToPlay: never open ports during plugin scans
    std::array<ChannelMonitor, 17> mon;

    // ---- shared state
    mutable juce::SpinLock renderLock;
    std::shared_ptr<const Rendered> rendered;
    std::deque<std::shared_ptr<const Rendered>> keepAlive; // old renders released on the message thread

    dmpe::Phrase source;
    juce::String sourceName;
    dmpe::LoadInfo sourceInfo;

    // ---- audio thread
    std::shared_ptr<const Rendered> audioRendered;
    double sampleRate = 44100.0;
    double expectedPpq = 0.0;
    double previewPpq = 0.0;
    bool wasPlaying = false;
    std::array<std::array<bool, 128>, 17> active {};
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
