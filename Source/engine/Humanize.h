#pragma once

#include "model/Phrase.h"

namespace dmpe
{

// Loosens timing (up to +-1/64 beat) and velocity (up to +-15%), deterministically for a seed.
// Notes tied into / out of a glide keep their timing, and engine-authored (locked) notes are left alone.
void humanize (Phrase& phrase, float amount, int seed);

} // namespace dmpe
