#include "FlagUtils.h"
#include "HalyardManager.h"
#include "fsm/SmartFlagFSM.h"

extern HalyardManager halMgr1;
extern FSMController fsm;

String flagStationToString(FlagStation station) {
    switch (station) {
        case FlagStation::FLAG_UNKNOWN: return "U";
        case FlagStation::FLAG_FULL:    return "F";
        case FlagStation::FLAG_HALF:    return "H";
        case FlagStation::FLAG_STOP:    return "S";
    }
    return "U";
}

FlagStation charToFlagStation(char code) {
    switch (toupper(code)) {
        case 'F': return FlagStation::FLAG_FULL;
        case 'H': return FlagStation::FLAG_HALF;
        case 'S': return FlagStation::FLAG_STOP;
        case 'U': default: return FlagStation::FLAG_UNKNOWN;
    }
}

String stateToString(FSMStateID state) {
    switch (fsm.currentState()) {
        case STATE_NONE:                return "NON";
        case STATE_STARTUP:             return "SUP";
        case STATE_ON_STATION:          return "ONS";
        case STATE_CALIBRATION:         return "CAL";
        case STATE_MOVING_TO_STATION:   return "MOV";
        case STATE_LID_OPEN:            return "LID";
        case STATE_FAULT_RECOVERY:      return "FLT";
    }
    return "UNK";
}

void checkAndReportStatus( bool forceReport , const char* reason ) {
    static unsigned long lastReportTime = 0;
    unsigned long currentTime = millis();
    const unsigned long reportInterval = 3600000; // 1 hour in ms

    if (!forceReport && (currentTime - lastReportTime < reportInterval)) {
        return; // Not time yet
    }

    if (!forceReport) {
        lastReportTime = currentTime;
    }

    Particle.publish("statusReport", getStatus( reason ), PRIVATE);
}

String getStatus( String reason ) {

    char statusReport[256];  // Output buffer
    JSONBufferWriter writer(statusReport, sizeof(statusReport)-1);

    writer.beginObject();
    writer.name("RSN").value(reason);                                               // Reason for report
    writer.name("VLT").value(halMgr1.getInputVoltage() ,1);                         // Volts - 1 digit
    writer.name("AMP").value(halMgr1.getSmoothedAmps() ,3);                           // Amps - 3 digits
    writer.name("FSM").value(stateToString(fsm.currentState()));                    // Current FSM state
    writer.name("OSTA").value(flagStationToString(halMgr1.getOrderedStation()));    // Ordered station
    writer.name("ASTA").value(flagStationToString(halMgr1.getActualStation() ));    // Actual station
/** [[ IMPLEMENT NEXT STATION REPORTING - time should be spelled out in UTC ]] */
    // writer.name("NSTA").value(flagStationToString(halMgr1.getNextStation()));       // Next station (pending event)
    // writer.name("NXT").value(halMgr1.getNextTime());                               // Time of next move (UTC)
    writer.name("MTR").value(halMgr1.isRunning() ? "RUN" : "STP" );                 // Motor status
    writer.endObject();

    return String(statusReport);
}

