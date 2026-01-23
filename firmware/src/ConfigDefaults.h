#pragma once

#include "Particle.h"
#include "EEPROMManager.h"   // for ConfigData

namespace ConfigDefaults {

ConfigData makeDefaultConfig();

// Applies defaults to fields that appear "unset" (mostly empty strings / zeroed ints).
// Returns true if it modified cfg.
bool applyDefaults(ConfigData &cfg);

// Basic sanity checks / clamping.
// Returns true if it modified cfg.
bool validateAndClamp(ConfigData &cfg);

} // namespace ConfigDefaults
