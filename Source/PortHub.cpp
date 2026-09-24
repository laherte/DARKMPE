#include "PortHub.h"

#include <cstring>

PortHub::PortHub() : juce::Thread ("DarkMPE port sender") {}

PortHub::~PortHub()
{
    stopThread (1000);
    for (auto& s : slots)
    {
        const juce::ScopedLock sl (s.lock);
        if (s.out != nullptr)
            for (int ch = 1; ch <= 16; ++ch)
                s.out->sendMessageNow (juce::MidiMessage::allNotesOff (ch));
        s.out.reset();
        s.open.store (false);
    }
}

juce::String PortHub::getName (int index) const
{
    return slots[(size_t) index].name;
}

void PortHub::setOpen (int index, const juce::String& name, bool shouldBeOpen)
{
    auto& s = slots[(size_t) index];
    const bool isOpenNow = s.out != nullptr;
    if (shouldBeOpen == isOpenNow && (! shouldBeOpen || name == s.name))
        return;

    std::unique_ptr<juce::MidiOutput> fresh;
    if (shouldBeOpen)
        fresh = juce::MidiOutput::createNewDevice (name);

    std::unique_ptr<juce::MidiOutput> old;
    {
        const juce::ScopedLock sl (s.lock);
        old = std::move (s.out);
        s.out = std::move (fresh);
        s.name = name;
        s.open.store (s.out != nullptr);
    }

    if (old != nullptr)
        for (int ch = 1; ch <= 16; ++ch)
            old->sendMessageNow (juce::MidiMessage::allNotesOff (ch));

    if (s.out != nullptr && ! isThreadRunning())
        startThread (juce::Thread::Priority::highest);
}

void PortHub::push (int index, const juce::MidiBuffer& block, double blockStartMs, double sampleRate)
{
    auto& s = slots[(size_t) index];
    if (! s.open.load (std::memory_order_relaxed) || block.isEmpty())
        return;

    const double msPerSample = 1000.0 / sampleRate;
    for (const auto meta : block)
    {
        if (meta.numBytes < 1 || meta.numBytes > 3)
            continue;

        int start1, size1, start2, size2;
        s.fifo.prepareToWrite (1, start1, size1, start2, size2);
        if (size1 + size2 == 0)
            return; // sender stalled: drop rather than block the audio thread

        auto& p = s.buffer[(size_t) (size1 > 0 ? start1 : start2)];
        p.timeMs = blockStartMs + meta.samplePosition * msPerSample;
        p.size = meta.numBytes;
        std::memcpy (p.data, meta.data, (size_t) meta.numBytes);
        s.fifo.finishedWrite (1);
    }
}

void PortHub::run()
{
    while (! threadShouldExit())
    {
        const double now = juce::Time::getMillisecondCounterHiRes();

        for (auto& s : slots)
        {
            if (s.fifo.getNumReady() == 0)
                continue;

            const juce::ScopedLock sl (s.lock);
            for (;;)
            {
                int start1, size1, start2, size2;
                s.fifo.prepareToRead (1, start1, size1, start2, size2);
                if (size1 + size2 == 0)
                    break;

                const auto& p = s.buffer[(size_t) (size1 > 0 ? start1 : start2)];
                if (p.timeMs > now + 0.25)
                    break; // not due yet (events are queued in time order)

                if (s.out != nullptr)
                    s.out->sendMessageNow (juce::MidiMessage (p.data, p.size));
                s.fifo.finishedRead (1);
            }
        }

        wait (1);
    }
}
