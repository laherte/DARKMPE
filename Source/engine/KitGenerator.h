#pragma once

#include "model/Phrase.h"
#include "engine/CinematicEngine.h"
#include "engine/MelodyGenerator.h"

#include <array>
#include <vector>

namespace dmpe
{

// A KIT is a whole track idea: several parts built on the same key, scale, chord progression (the lead style's)
// and seed, so they lock together.
enum class Layer { lead, bass, arp, siren, stab, pad, count };
inline const char* const layerNames[] = { "Lead", "Bass", "Arp", "Siren", "Stab", "Pad" };
constexpr int numLayers = (int) Layer::count;

enum class BassPattern { rolling, offbeat, pedalOctave, arpDown, count };
inline const char* const bassPatternNames[] = { "Rolling", "Offbeat", "Pedal + Oct", "Arp Down" };

enum class ArpPattern { up, down, upDown, random, count };
inline const char* const arpPatternNames[] = { "Up", "Down", "Up Down", "Random" };

enum class SirenPattern { rise, wail, fall, alarm, count };
inline const char* const sirenPatternNames[] = { "Rise", "Wail", "Fall", "Alarm" };

enum class StabPattern { accents, offbeat, syncopated, downbeat, count };
inline const char* const stabPatternNames[] = { "Accents", "Offbeat", "Syncopated", "Downbeat" };

struct LayerParams
{
    bool on = true;
    int pattern = 0;       // the layer's pattern enum
    float density = 0.6f;  // 0..1
    int octave = 0;        // -2..+2 around the layer's home register
};

struct KitParams
{
    GenParams gen; // key, scale, style (its progression), bars, seed, variation, slide, and the lead itself
    std::array<LayerParams, (size_t) numLayers> layers;
    CineParams pad; // the Pad layer is the cinematic engine over the kit's chords
};

struct KitPart
{
    Layer layer;
    Phrase phrase;
};

// The enabled layers, in Layer order, all with the same length. Deterministic for a given KitParams.
std::vector<KitPart> generateKit (const KitParams& p);

// The kit's chords (one region per bar), e.g. for chord symbols.
std::vector<Region> kitChords (const GenParams& gen);

} // namespace dmpe
