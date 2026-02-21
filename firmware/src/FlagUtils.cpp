#include "FlagUtils.h"
#include "HalyardManager.h"
#include "fsm/SmartFlagFSM.h"
#include "EEPROMManager.h"

extern HalyardManager halMgr1;
extern FSMController fsm;

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static uint32_t s_lastPeriodicPublishSec = 0;  // tracks ONLY periodic heartbeats — never reset by forced publishes
static uint32_t s_statusSeq = 0;

// Last move context — populated by reportMoveStart(), read by getStatus()
static FlagStation    s_moveFromStation = FLAG_UNKNOWN;
static FlagStation    s_moveToStation   = FLAG_UNKNOWN;
static FlagMoveStatus s_lastMoveResult  = FLAG_MOVE_NONE;
static bool           s_moveInProgress  = false;

// Move current stats — updated while motor is running via updateMoveCurrentStats()
static float s_movePeakAmps = 0.0f;
static float s_moveAmpSum   = 0.0f;
static int   s_moveAmpCount = 0;

// Cached cellular signal — sampled lazily at most every 5 minutes
static float    s_cachedRSSI         = 0.0f;
static float    s_cachedQual         = 0.0f;
static uint32_t s_lastSignalSampleMs = 0;

// ---------------------------------------------------------------------------
// Burst limiter
// Allows up to BURST_MAX publishes in any BURST_WINDOW_SEC window.
// Protects against runaway publishing while still allowing rapid event sequences.
// ---------------------------------------------------------------------------
static const uint8_t  BURST_MAX        = 10;
static const uint32_t BURST_WINDOW_SEC = 60;

static bool burstAllowed() {
    static uint32_t timestamps[BURST_MAX] = {0};
    static uint8_t  idx = 0;

    uint32_t now = Time.isValid() ? (uint32_t)Time.now() : (millis() / 1000);
    uint32_t oldest = timestamps[idx];

    if (oldest != 0 && (now - oldest) < BURST_WINDOW_SEC) {
        SFDBG::pub("RPT", String::format("burst limit hit: %u msgs/%us",
                   (unsigned)BURST_MAX, (unsigned)BURST_WINDOW_SEC));
        return false;
    }
    timestamps[idx] = now;
    idx = (idx + 1) % BURST_MAX;
    return true;
}

// ---------------------------------------------------------------------------
// String helpers
// ---------------------------------------------------------------------------

String flagStationToString(FlagStation station) {
    switch (station) {
        case FLAG_UNKNOWN: return "U";
        case FLAG_FULL:    return "F";
        case FLAG_HALF:    return "H";
        case FLAG_STOP:    return "S";
    }
    return "U";
}

FlagStation charToFlagStation(char code) {
    switch (toupper(code)) {
        case 'F': return FLAG_FULL;
        case 'H': return FLAG_HALF;
        case 'S': return FLAG_STOP;
        case 'U': default: return FLAG_UNKNOWN;
    }
}

String stateToString(FSMStateID state) {
    switch (state) {
        case STATE_NONE:                return "NON";
        case STATE_STARTUP:             return "SUP";
        case STATE_ON_STATION:          return "ONS";
        case STATE_CALIBRATION:         return "CAL";
        case STATE_MOVING_TO_STATION:   return "MOV";
        case STATE_LID_OPEN:            return "LID";
        case STATE_FAULT_RECOVERY:      return "FLT";
        default:                        return "UNK";
    }
}

static const char* moveResultToString(FlagMoveStatus s) {
    switch (s) {
        case FLAG_MOVE_NONE:      return "NON";
        case FLAG_MOVING_UP:      return "MUP";
        case FLAG_MOVING_DOWN:    return "MDN";
        case FLAG_ON_STATION:     return "ONS";
        case FLAG_MOVE_CANCELLED: return "CAN";
        case FLAG_MOVE_TIMEOUT:   return "TMO";
        case FLAG_MOVE_STALL:     return "STL";
        default:                  return "UNK";
    }
}

// ---------------------------------------------------------------------------
// Move current stats — called from HalyardManager::update() while running
// ---------------------------------------------------------------------------

void updateMoveCurrentStats(float amps) {
    if (amps > s_movePeakAmps) s_movePeakAmps = amps;
    s_moveAmpSum   += amps;
    s_moveAmpCount += 1;
}

static void resetMoveCurrentStats() {
    s_movePeakAmps = 0.0f;
    s_moveAmpSum   = 0.0f;
    s_moveAmpCount = 0;
}

// ---------------------------------------------------------------------------
// Move event reporters — called directly by HalyardManager
// ---------------------------------------------------------------------------

void reportMoveStart(FlagStation fromStation, FlagStation toStation) {
    s_moveFromStation = fromStation;
    s_moveToStation   = toStation;
    s_moveInProgress  = true;
    resetMoveCurrentStats();
    checkAndReportStatus(true, "MVS");   // Move Start
}

void reportMoveEnd(FlagMoveStatus moveResult, FlagStation actualStation) {
    s_lastMoveResult = moveResult;
    s_moveInProgress = false;
    checkAndReportStatus(true, "MVE");   // Move End
}

// ---------------------------------------------------------------------------
// Core publish gate
// ---------------------------------------------------------------------------

void checkAndReportStatus(bool forceReport, const char* reason) {

    if (forceReport) {
        // Event-driven: only burst limiter applies — no per-message gap throttle
        if (!burstAllowed()) return;

    } else {
        // Periodic heartbeat: independent timer, never reset by forced publishes
        // status_period_sec == 0 means periodic reports are disabled
        ConfigData cfg;
        readConfig(cfg);

        if (cfg.status_period_sec == 0) return;

        uint32_t nowSec = Time.isValid() ? (uint32_t)Time.now() : (millis() / 1000);
        uint32_t gap    = nowSec - s_lastPeriodicPublishSec;

        if (s_lastPeriodicPublishSec != 0 && gap < cfg.status_period_sec) return;

        if (!burstAllowed()) return;

        s_lastPeriodicPublishSec = nowSec;   // ONLY periodic timer updates this
    }

    s_statusSeq++;
    Particle.publish("statusReport", getStatus(reason), PRIVATE);
}

// ---------------------------------------------------------------------------
// Status payload builder
// ---------------------------------------------------------------------------

String getStatus(String reason) {

    StatusData st;
    readStatus(st);
    uint32_t rebootCount = st.reboot_count;
    uint32_t uptimeSec   = millis() / 1000;

    // FSM state duration tracking
    static FSMStateID lastTrackedState = STATE_NONE;
    static uint32_t   stateEnteredMs   = 0;
    FSMStateID curState = fsm.currentState();
    if (curState != lastTrackedState) {
        lastTrackedState = curState;
        stateEnteredMs   = millis();
    }
    uint32_t stateDurSec = (millis() - stateEnteredMs) / 1000;

    // Cellular signal — sampled lazily, cached to avoid blocking loop()
    uint32_t nowMs = millis();
    if (s_lastSignalSampleMs == 0 || (nowMs - s_lastSignalSampleMs) > 300000UL) {
        CellularSignal sig = Cellular.RSSI();
        s_cachedRSSI         = sig.getStrengthValue();
        s_cachedQual         = sig.getQualityValue();
        s_lastSignalSampleMs = nowMs;
    }

    // Move current averages
    float movAvgAmps  = (s_moveAmpCount > 0) ? (s_moveAmpSum / s_moveAmpCount) : 0.0f;
    float movPeakAmps = s_movePeakAmps;

    char buf[512];
    memset(buf, 0, sizeof(buf));
    JSONBufferWriter writer(buf, sizeof(buf));

    writer.beginObject();
    writer.name("RSN").value(reason);                                               // Reason for report
    writer.name("SEQ").value(s_statusSeq);                                          // Sequence number
    writer.name("OSTA").value(flagStationToString(halMgr1.getOrderedStation()));    // Ordered station
    writer.name("ASTA").value(flagStationToString(halMgr1.getActualStation()));     // Actual station
    writer.name("FSM").value(stateToString(curState));                              // FSM state
    writer.name("FSD").value(stateDurSec);                                          // FSM state duration (sec)
    writer.name("MTR").value(halMgr1.isRunning() ? "RUN" : "STP");                 // Motor running?
    writer.name("LMR").value(moveResultToString(s_lastMoveResult));                 // Last move result
    writer.name("VLT").value(halMgr1.getInputVoltage(), 1);                         // Input voltage
    writer.name("AMP").value(halMgr1.getSmoothedAmps(), 3);                         // Smoothed amps (live)
    writer.name("MCA").value(movAvgAmps, 3);                                        // Move current avg
    writer.name("MCP").value(movPeakAmps, 3);                                       // Move current peak
    writer.name("UPT").value(uptimeSec);                                            // Uptime (sec)
    writer.name("RBT").value(rebootCount);                                          // Reboot count
    writer.name("RSS").value(s_cachedRSSI, 1);                                      // Cellular signal strength
    writer.name("QUL").value(s_cachedQual, 1);                                      // Cellular signal quality
    // NSTA and NXT intentionally omitted until event-handling port is complete
    writer.endObject();

    size_t n = writer.bufferSize();
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    buf[n] = '\0';
    return String(buf);
}

// ---------------------------------------------------------------------------
// Particle.function: dbgToggle
// ---------------------------------------------------------------------------

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
    if (arg == "status") {
        SFDBG::pub("DBG", String::format("enabled=%d", SFDBG::enabled ? 1 : 0), true);
        return SFDBG::enabled ? 1 : 0;
    }
    return -1;
}
