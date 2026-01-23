#pragma once
#include "Particle.h"

// Compile-time master switch. Leave ON for development.
// For production, set to 0 or leave on but runtime-disabled.
#ifndef SFDBG_COMPILED
#define SFDBG_COMPILED 1
#endif

namespace SFDBG {

// Runtime enable (we'll later set this from ConfigData; for now, toggle via Particle.function).
extern bool enabled;

// Throttle settings (ms). Tune as needed.
static const uint32_t MIN_GAP_MS = 30000;   // 30s between debug publishes by default

// Publish one debug event (throttled unless force=true).
inline void pub(const char* tag, const String& msg, bool force = false) {
#if SFDBG_COMPILED
    if (!enabled && !force) return;

    static uint32_t lastMs = 0;
    uint32_t now = millis();
    if (!force && (now - lastMs) < MIN_GAP_MS) return;
    lastMs = now;

    // Keep payload small. Avoid long strings.
    // Note: Particle event data limit exists; stay conservative.
    String payload = String::format("{\"T\":\"%s\",\"M\":\"%s\"}", tag, msg.c_str());
    Particle.publish("SFDBG", payload, PRIVATE);
#endif
}

} // namespace SFDBG
