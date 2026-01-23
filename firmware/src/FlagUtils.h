#pragma once

#include "Particle.h"
#include "HalyardManager.h"
#include "fsm/SmartFlagFSM.h"
#include "Dbg.h"

String flagStationToString(FlagStation station);
FlagStation charToFlagStation(char code);
String stateToString(FSMStateID state);
void checkAndReportStatus(bool forceReport , const char* reason );
String getStatus( String reason );
int dbgToggle(String arg);