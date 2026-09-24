#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

// Virtual MIDI output ports ("DarkMPE Out", one per layer in KIT mode).
// The audio thread only pushes into a lock-free FIFO; a sender thread delivers each message on time.
// (juce::MidiOutput::sendBlockOfMessages allocates and locks for every event, which the audio thread must not do.)
class PortHub : private juce::Thread
{
public:
    static constexpr int numPorts = 6;

    PortHub();
    ~PortHub() override;

    // ---- message thread
    void setOpen (int index, const juce::String& name, bool shouldBeOpen); // creates / destroys the device
    bool isOpen (int index) const { return slots[(size_t) index].open.load (std::memory_order_relaxed); }
    juce::String getName (int index) const;

    // ---- audio thread: never blocks or allocates. Messages go out at blockStartMs + samplePosition / sampleRate.
    void push (int index, const juce::MidiBuffer& block, double blockStartMs, double sampleRate);

private:
    void run() override;

    static constexpr int capacity = 8192;

    struct Pending
    {
        double timeMs = 0.0;
        juce::uint8 data[3] {};
        int size = 0;
    };

    struct Slot
    {
        juce::CriticalSection lock; // sender thread vs. message thread (never taken by the audio thread)
        std::unique_ptr<juce::MidiOutput> out;
        juce::String name;
        std::atomic<bool> open { false };
        juce::AbstractFifo fifo { capacity };
        std::vector<Pending> buffer = std::vector<Pending> ((size_t) capacity);
    };

    std::array<Slot, numPorts> slots;

    JUCE_DECLARE_NON_COPYABLE (PortHub)
};
