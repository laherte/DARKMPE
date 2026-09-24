// Loads the built VST3 the way a host does (scan + instantiate + play + destroy).
#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::cout << "usage: HostLoadTest <plugin.vst3>" << std::endl; return 2; }

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    format.findAllTypesForFile (found, argv[1]);
    std::cout << "scan: " << found.size() << " type(s) in " << (juce::Time::getMillisecondCounterHiRes() - t0) << " ms" << std::endl;
    if (found.isEmpty()) { std::cout << "FAIL: no plugin found" << std::endl; return 1; }

    for (auto* d : found)
        std::cout << "  " << d->name << " | " << d->manufacturerName << " | instrument=" << d->isInstrument
                  << " | ins=" << d->numInputChannels << " outs=" << d->numOutputChannels << std::endl;

    juce::String err;
    const auto t1 = juce::Time::getMillisecondCounterHiRes();
    auto inst = format.createInstanceFromDescription (*found[0], 48000.0, 512, err);
    std::cout << "instantiate: " << (inst ? "ok" : ("FAIL " + err)) << " in " << (juce::Time::getMillisecondCounterHiRes() - t1) << " ms" << std::endl;
    if (! inst) return 1;

    inst->prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> audio (2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 200; ++i) { midi.clear(); inst->processBlock (audio, midi); }
    std::cout << "processed 200 blocks" << std::endl;

    if (inst->hasEditor())
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditor());
        std::cout << "editor: " << (ed ? "ok" : "null") << std::endl;
    }
    inst->releaseResources();
    inst.reset();
    std::cout << "PASS" << std::endl;
    return 0;
}
