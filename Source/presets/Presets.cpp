#include "Presets.h"

namespace presets
{
namespace
{

void setPlain (juce::RangedAudioParameter& p, float plain)
{
    p.setValueNotifyingHost (p.convertTo0to1 (plain));
}

} // namespace

// Mode: 0 Generate, 1 Transform, 2 Cinematic, 3 Kit. Bars: 0..3 = 1, 2, 4, 8 bars.
// Form: 0 Classic, 1 Call & Response, 2 A B A C, 3 A A B A, 4 A A A B, 5 Period, 6 Sentence, 7 Sequence.
// Style: 0 Pursuit, 1 Hate or Glory, 2 Opr, 3 Dark Arp, 4 Acid Slide, 5 Gallop, 6 Rave Stab.
// Scale: 0 Natural Minor, 1 Phrygian, 2 Harmonic Minor, 3 Phrygian Dominant, 4 Dorian, 5 Locrian,
//        6 Hungarian Minor, 7 Double Harmonic, 8 Neapolitan Minor, 9 Aeolian b5, 10 Minor Pentatonic.
// Progression: 0 Epic Minor, 1 Phrygian Dark, 2 Harmonic Dominant, 3 Lament Bass, 4 Mediant Chain,
//              5 Tritone Abyss, 6 Tonic Pedal, 7 Line Cliche, 8 Neapolitan, 9 Andalusian Dark, 10 Auto.
// Motion: 0 Morph, 1 Bloom, 2 Collapse, 3 Breathe, 4 Deep Note, 5 Pulse, 6 Tension Rise.
// Reharm: 0 Off, 1 Sus Resolve, 2 Chromatic Approach, 3 Mediant Shift, 4 Suspensions, 5 Tonic Pedal,
//         6 Planing, 7 Tritone Approach.
// Voicing: 2 Open Spread, 3 Dark Cluster, 8 Epic Spread, 9 Gothic, 10 Hyper Spread. Shape: 1 Ease, 3 Swoop Out, 4 Stepped.
const std::vector<Preset>& factory()
{
    static const std::vector<Preset> list {
        { "Init", {} },

        { "Lead - Pursuit Drive",   { { "mode", 0 }, { "style", 0 }, { "scale", 1 }, { "density", 0.8f }, { "pedal", 0.6f }, { "slide", 0.25f }, { "gate", 0.55f } } },
        { "Lead - Hate Octaves",    { { "mode", 0 }, { "style", 1 }, { "scale", 2 }, { "octave", 0.6f }, { "slide", 0.2f }, { "gate", 0.5f } } },
        { "Lead - Opr Stabs",       { { "mode", 0 }, { "style", 2 }, { "scale", 1 }, { "gate", 0.25f }, { "slide", 0.05f }, { "chroma", 0.2f } } },
        { "Lead - Acid Glide",      { { "mode", 0 }, { "style", 4 }, { "scale", 0 }, { "slide", 0.8f }, { "glideTime", 0.18f }, { "gate", 0.9f }, { "vibDepth", 0.1f } } },
        { "Lead - Gallop",          { { "mode", 0 }, { "style", 5 }, { "scale", 3 }, { "density", 0.9f }, { "slide", 0.1f }, { "gate", 0.5f } } },
        { "Lead - Rave Stab",       { { "mode", 0 }, { "style", 6 }, { "scale", 7 }, { "octave", 0.6f }, { "gate", 0.3f } } },
        { "Lead - Dark Arp 16ths",  { { "mode", 0 }, { "style", 3 }, { "scale", 6 }, { "density", 1.0f }, { "gate", 0.5f }, { "slide", 0.15f }, { "bars", 3 } } },
        { "Lead - ABAC Anthem",     { { "mode", 0 }, { "form", 2 }, { "style", 0 }, { "scale", 1 }, { "density", 0.75f }, { "slide", 0.3f }, { "bars", 3 } } },
        { "Lead - Call & Response Acid", { { "mode", 0 }, { "form", 1 }, { "style", 4 }, { "scale", 0 }, { "slide", 0.6f }, { "gate", 0.8f } } },
        { "Lead - Sequence Climb",  { { "mode", 0 }, { "form", 7 }, { "style", 3 }, { "scale", 2 }, { "density", 0.9f }, { "gate", 0.5f } } },
        { "Lead - Sentence Opr",    { { "mode", 0 }, { "form", 6 }, { "style", 2 }, { "scale", 1 }, { "gate", 0.3f } } },

        { "Cinematic - Epic Minor Morph",    { { "mode", 2 }, { "cProg", 0 }, { "cMotion", 0 }, { "cReharm", 1 }, { "cVoicing", 8 }, { "cShape", 1 }, { "cTension", 0.35f }, { "cDark", 0.4f }, { "cSub", 1 }, { "cArc", 0.5f } } },
        { "Cinematic - Mediant Bloom",       { { "mode", 2 }, { "cProg", 4 }, { "cMotion", 1 }, { "cReharm", 0 }, { "cVoicing", 9 }, { "cShape", 3 }, { "cTension", 0.5f }, { "cDark", 0.8f } } },
        { "Cinematic - Tritone Deep Note",   { { "mode", 2 }, { "cProg", 5 }, { "cMotion", 4 }, { "cReharm", 0 }, { "cVoicing", 10 }, { "cChordLen", 2 }, { "bars", 3 }, { "cTension", 0.3f }, { "cDark", 0.9f }, { "cSub", 1 } } },
        { "Cinematic - Lament Suspensions",  { { "mode", 2 }, { "cProg", 3 }, { "cMotion", 0 }, { "cReharm", 4 }, { "cVoicing", 8 }, { "cShape", 4 }, { "cTension", 0.25f }, { "cDark", 0.6f }, { "cSub", 1 } } },
        { "Cinematic - Phrygian Pulse",      { { "mode", 2 }, { "cProg", 1 }, { "cMotion", 5 }, { "cReharm", 0 }, { "cVoicing", 9 }, { "cShape", 3 }, { "cPulse", 0.6f }, { "cFall", 0.5f }, { "cDark", 0.9f } } },
        { "Cinematic - Tonic Pedal Rise",    { { "mode", 2 }, { "cProg", 6 }, { "cMotion", 6 }, { "cReharm", 5 }, { "cTension", 0.6f }, { "cDark", 0.7f }, { "cSub", 1 } } },
        { "Cinematic - Neapolitan Tritone",  { { "mode", 2 }, { "cProg", 8 }, { "cMotion", 0 }, { "cReharm", 7 }, { "cTension", 0.3f }, { "cDark", 0.7f }, { "cFall", 0.3f } } },
        { "Cinematic - Line Cliche",         { { "mode", 2 }, { "cProg", 7 }, { "cMotion", 0 }, { "cReharm", 0 }, { "cVoicing", 8 }, { "cTension", 0.1f }, { "cDark", 0.3f } } },
        { "Cinematic - ABAC Cadence",        { { "mode", 2 }, { "form", 2 }, { "cProg", 0 }, { "cMotion", 0 }, { "cReharm", 4 }, { "cVoicing", 8 }, { "cShape", 1 }, { "cTension", 0.3f }, { "cSub", 1 }, { "bars", 3 } } },
        { "Cinematic - Rising Sequence",     { { "mode", 2 }, { "form", 7 }, { "cProg", 0 }, { "cMotion", 0 }, { "cReharm", 0 }, { "cVoicing", 9 }, { "cTension", 0.35f }, { "cDark", 0.7f }, { "cArc", 0.8f }, { "bars", 3 } } },
        { "Cinematic - Auto Planing",        { { "mode", 2 }, { "cProg", 10 }, { "cMotion", 0 }, { "cReharm", 6 }, { "cVoicing", 9 }, { "cShape", 3 }, { "cTension", 0.3f }, { "cDark", 0.8f }, { "cSub", 1 } } },

        { "Kit - Full Track",       { { "mode", 3 }, { "style", 0 }, { "scale", 1 }, { "kSiren", 1 }, { "kStabPat", 0 } } },
        { "Kit - Siren Break",      { { "mode", 3 }, { "kLead", 0 }, { "kBass", 0 }, { "kArp", 0 }, { "kStab", 0 }, { "kSiren", 1 }, { "kSirenPat", 1 }, { "kPad", 1 }, { "cMotion", 4 }, { "kFocus", 3 } } },
        { "Kit - Bass + Arp Drive", { { "mode", 3 }, { "style", 5 }, { "scale", 3 }, { "kLead", 0 }, { "kSiren", 0 }, { "kStab", 0 }, { "kPad", 0 }, { "kArpPat", 2 }, { "kArpDen", 0.8f }, { "kFocus", 1 } } },
        { "Kit - ABAC Track",       { { "mode", 3 }, { "form", 2 }, { "style", 0 }, { "scale", 1 }, { "kSiren", 1 }, { "bars", 3 } } },
        { "Kit - Rave Stabs",       { { "mode", 3 }, { "style", 6 }, { "scale", 7 }, { "kStabPat", 2 }, { "kStabDen", 0.7f }, { "kArp", 0 }, { "kSiren", 1 }, { "kSirenPat", 3 }, { "kFocus", 4 } } },

        { "Transform - Epic Spread",  { { "mode", 1 }, { "vMode", 8 }, { "voices", 6 }, { "strum", 0.02f } } },
        { "Transform - Gothic Stack", { { "mode", 1 }, { "vMode", 9 }, { "voices", 6 } } },
    };
    return list;
}

bool isOutputSetting (const juce::String& id)
{
    return id == "preview" || id == "virtualOut" || id == "pbRange" || id == "monoBend" || id == "trigMode" || id == "leadMono";
}

void apply (juce::AudioProcessorValueTreeState& state, const Preset& preset)
{
    for (auto* p : state.processor.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr || isOutputSetting (rp->paramID))
            continue;
        float value = rp->convertFrom0to1 (rp->getDefaultValue());
        for (const auto& [id, v] : preset.values)
            if (rp->paramID == id)
                value = v;
        setPlain (*rp, value);
    }
}

juce::File userFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("DarkMPE").getChildFile ("Presets");
}

bool save (juce::AudioProcessorValueTreeState& state, int seed, int variation, const juce::File& file)
{
    juce::XmlElement xml ("DarkMPEPreset");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("seed", seed);
    xml.setAttribute ("variation", variation);
    for (auto* p : state.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p); rp != nullptr && ! isOutputSetting (rp->paramID))
        {
            auto* e = xml.createNewChildElement ("PARAM");
            e->setAttribute ("id", rp->paramID);
            e->setAttribute ("value", rp->convertFrom0to1 (rp->getValue()));
        }
    file.getParentDirectory().createDirectory();
    return xml.writeTo (file);
}

bool load (juce::AudioProcessorValueTreeState& state, const juce::File& file, int& seed, int& variation)
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("DarkMPEPreset"))
        return false;

    Preset preset { "", {} };
    std::vector<juce::String> ids; // keeps the id strings alive for the Preset's const char*
    ids.reserve ((size_t) xml->getNumChildElements());
    for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
    {
        ids.push_back (e->getStringAttribute ("id"));
        preset.values.push_back ({ ids.back().toRawUTF8(), (float) e->getDoubleAttribute ("value") });
    }
    apply (state, preset);
    seed = xml->getIntAttribute ("seed", seed);
    variation = xml->getIntAttribute ("variation", 0);
    return true;
}

} // namespace presets
