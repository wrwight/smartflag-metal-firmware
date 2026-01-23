#include "FlagUtils.h"
#include "HalyardManager.h"
#include "fsm/SmartFlagFSM.h"
#include "EEPROMManager.h"

extern HalyardManager halMgr1;
extern FSMController fsm;
static uint32_t s_lastStatusPublishSec = 0;
static uint32_t s_lastForcedPublishSec = 0;
static uint32_t s_statusSeq = 0;

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
    unsigned long currentTime = millis();
    const unsigned long reportInterval = 3600000; // 1 hour in ms
    ConfigData cfg;
    readConfig(cfg);

    uint32_t nowSec = Time.now();
    if (nowSec == 0) {
        nowSec = millis() / 1000;
    }

    bool shouldPublish = false;

    if (forceReport) {
        uint32_t gap = nowSec - s_lastForcedPublishSec;

        if (s_lastForcedPublishSec == 0 || gap >= cfg.force_report_min_gap_sec) {
            shouldPublish = true;
        } else {
            // Optional debug (throttled by SFDBG itself)
            SFDBG::pub("RPT", String::format("skip forced gap=%lu < %u (%s)",
                        (unsigned long)gap,
                        (unsigned int)cfg.force_report_min_gap_sec,
                        reason ));
        }
    } else {
        uint32_t gap = nowSec - s_lastStatusPublishSec;

        if (s_lastStatusPublishSec == 0 || gap >= cfg.status_period_sec) {
            shouldPublish = true;
        }
    }

    if (!shouldPublish) {
        return;
    }

    s_statusSeq++;
    Particle.publish("statusReport", getStatus( reason ), PRIVATE);

    s_lastStatusPublishSec = nowSec;
    if (forceReport) {
        s_lastForcedPublishSec = nowSec;
    }
}

String getStatus( String reason ) {

    StatusData st;
    readStatus(st);  // you already have readStatus(StatusData&) in EEPROMManager.cpp/.h
    uint32_t rebootCount = st.reboot_count;
    uint32_t uptimeSec = millis() / 1000;

    char statusReport[256];  // Output buffer
    memset(statusReport, 0, sizeof(statusReport));
    JSONBufferWriter writer(statusReport, sizeof(statusReport));

    writer.beginObject();
    writer.name("RSN").value(reason);                                               // Reason for report
    writer.name("OSTA").value(flagStationToString(halMgr1.getOrderedStation()));    // Ordered station
    writer.name("ASTA").value(flagStationToString(halMgr1.getActualStation() ));    // Actual station
    writer.name("SEQ").value(s_statusSeq);                                          // Status sequence number
    writer.name("UPT").value(uptimeSec);                                            // Uptime in seconds
    writer.name("RBT").value(rebootCount);                                          // Reboot count
    writer.name("VLT").value(halMgr1.getInputVoltage() ,1);                         // Volts - 1 digit
    writer.name("AMP").value(halMgr1.getSmoothedAmps() ,3);                           // Amps - 3 digits
    writer.name("FSM").value(stateToString(fsm.currentState()));                    // Current FSM state
/** [[ IMPLEMENT NEXT STATION REPORTING - time should be spelled out in UTC ]] */
    // writer.name("NSTA").value(flagStationToString(halMgr1.getNextStation()));       // Next station (pending event)
    // writer.name("NXT").value(halMgr1.getNextTime());                               // Time of next move (UTC)
    writer.name("MTR").value(halMgr1.isRunning() ? "RUN" : "STP" );                 // Motor status
    writer.endObject();

    // Ensure accurate string length based on bytes written (without overflowing the buffer)
    size_t n = writer.bufferSize();
    if (n >= sizeof(statusReport)) n = sizeof(statusReport);
    return String(statusReport, n);
}

int dbgToggle(String arg) {
    arg.trim();
    arg.toLowerCase();
    if (arg == "1" || arg == "on" || arg == "true") {
        SFDBG::enabled = true;
        SFDBG::pub("DBG", "enabled", true);
        return 1;
    }
    if (arg == "0" || arg == "off" || arg == "false") {
        SFDBG::pub("DBG", "disabled", true);
        SFDBG::enabled = false;
        return 0;
    }
    // "status" returns current state without changing it
    if (arg == "status") {
        SFDBG::pub("DBG", String::format("enabled=%d", SFDBG::enabled ? 1 : 0), true);
        return SFDBG::enabled ? 1 : 0;
    }
    return -1;
}
