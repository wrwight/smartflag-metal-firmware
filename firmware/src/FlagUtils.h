#pragma once

#include "Particle.h"
#include "HalyardManager.h"
#include "SmartFlagFSM.h"
#include "Dbg.h"

String flagStationToString(FlagStation station);
FlagStation charToFlagStation(char code);
String stateToString(FSMStateID state);

// Primary status publish entry point.
// forceReport=true  → event-driven (move start/stop, faults, lid events)
// forceReport=false → periodic heartbeat (called every loop())
void checkAndReportStatus(bool forceReport, const char* reason);

// Called by HalyardManager when a move starts.
// Captures ASTA at moment of departure and OSTA (destination).
void reportMoveStart(FlagStation fromStation, FlagStation toStation);

// Called by HalyardManager when a move ends for any reason.
void reportMoveEnd(FlagMoveStatus moveResult, FlagStation actualStation);

// Called by HalyardManager::update() each tick while motor is running.
// Feeds move current stats (avg/peak) for inclusion in status reports.
void updateMoveCurrentStats(float amps);

// Builds and returns the current status JSON string.
String getStatus(String reason);

// Particle.function handler to toggle debug publishing.
int dbgToggle(String arg);
