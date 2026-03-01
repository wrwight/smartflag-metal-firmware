// main.ino  –  SmartFlag Gen3  PRODUCT_VERSION(7)
/* ────────────────────────────────────────────────────────────────────────────────
 * 2024-06-17  Marked as Gen3.0, v7.  
    Implemented status reporting changes to support new cloud dashboard.  
    Added event registration and processing for new SJR (sub-jurisdiction) field, 
        which is a Gen3-only addition to the event JSON schema.  
    No changes to core halyard control logic or safety features in this update.
──────────────────────────────────────────────────────────────────────────────── */

#include "Particle.h"
#include "HalyardManager.h"
#include "FaultManager.h"
#include "Sensor.h"
#include "BuzzerManager.h"
#include "SmartFlagFSM.h"
#include "EEPROMManager.h"
#include "FlagUtils.h"
#include "EventManager.h"

PRODUCT_VERSION(7)          // firmware version, for OTA update tracking

// ─────────────────────────────────────────────────────────────────────────────
//  Pin assignments
// ─────────────────────────────────────────────────────────────────────────────
const int DIR_PIN          = D5;
const int PWM_PIN          = D6;
const int MOTOR_ENABLE_PIN = D7;
const int CURRENT_SENSE_PIN= A4;
const int SENSOR_ENABLE_PIN= D8;
const int HALF_SENSOR_PIN  = D10;
const int FULL_SENSOR_PIN  = D11;
const int LID_SENSOR_PIN   = D12;

// ─────────────────────────────────────────────────────────────────────────────
//  System mode
// ─────────────────────────────────────────────────────────────────────────────
SYSTEM_MODE(AUTOMATIC);

// ─────────────────────────────────────────────────────────────────────────────
//  Object instances
// ─────────────────────────────────────────────────────────────────────────────
BuzzerManager buzzer;
HalyardManager halMgr1(DIR_PIN, PWM_PIN, MOTOR_ENABLE_PIN, CURRENT_SENSE_PIN, false);
FSMController  fsm;
FaultManager   faultMgr;
Sensor halfSensor(HALF_SENSOR_PIN);
Sensor fullSensor(FULL_SENSOR_PIN);
Sensor lidSensor (LID_SENSOR_PIN);

// Amp-smoothing timer — fires every 100 ms, independent of loop()
Timer halMgr1AmpsTimer(100, [](){ halMgr1.updateSmoothedAmps(); });

// ─────────────────────────────────────────────────────────────────────────────
//  Forward declarations
// ─────────────────────────────────────────────────────────────────────────────
int  remoteClearFault(String arg);
void serviceRemoteRequests();

// ─────────────────────────────────────────────────────────────────────────────
//  Remote-request latches  (set in cloud function, consumed in loop)
// ─────────────────────────────────────────────────────────────────────────────
static volatile bool g_clearFaultRequested = false;
static String        g_clearFaultArg       = "";

// ─────────────────────────────────────────────────────────────────────────────
//  System event handler: time-sync / DST change
//  Registered via System.on(time_changed, onTimeChanged) in setup().
//  Re-evaluates stored event time marks (sunrise, local times) whenever
//  Device OS corrects the clock.
// ─────────────────────────────────────────────────────────────────────────────
void onTimeChanged(system_event_t event, int param) {
    (void)event; (void)param;
    if (Time.year() >= 2020) {      // guard against pre-sync garbage time
        evMgr.reprocessEvents();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  setup()
// ─────────────────────────────────────────────────────────────────────────────
void setup() {

    // ── Buzzer & sensors ──────────────────────────────────────────────────────
    buzzer.begin();
    buzzer.playEventWait(BUZZ_POWER_ON);    // "Off we go..."

    halfSensor.begin();
    fullSensor.begin();

    pinMode(SENSOR_ENABLE_PIN, OUTPUT);
    digitalWrite(SENSOR_ENABLE_PIN, LOW);   // enable lid sensor
    lidSensor.begin();

    // ── Halyard ───────────────────────────────────────────────────────────────
    halMgr1.begin();
    halMgr1AmpsTimer.start();

    // ── Connect to Particle cloud ─────────────────────────────────────────────
    //  Particle.function() / .variable() / .subscribe() registrations must
    //  happen BEFORE Particle.connect() so Device OS includes them in the
    //  cloud handshake.  Register everything below, then connect.

    // ── Cloud functions ───────────────────────────────────────────────────────
    Particle.function("TempHALF",   setHALF);
    Particle.function("TempFULL",   setFULL);
    Particle.function("OrderHFS",   setStation);
    Particle.function("PlayID",     playIDTones);
    Particle.function("clearFault", remoteClearFault);
    Particle.function("dbg",        dbgToggle);
    Particle.function("s_Config",   static_cast<int(*)(String)>([](String s) -> int {
        return evMgr.configScheduler(s);    // event scheduler configuration
    }));
    Particle.function("s_InjectEv", static_cast<int(*)(String)>([](String s) -> int {
        return evMgr.receiveEvent(s);       // direct event injection (catch-up for offline units)
    }));

    // ── Cloud variables ───────────────────────────────────────────────────────
    Particle.variable("Config", configToJSON);
    Particle.variable("Status", []() -> String { return getStatus("QRY"); });
    // s_EventLIST and s_ShowConfig are registered inside evMgr.setup() below

    // ── System event hooks ────────────────────────────────────────────────────
    System.on(time_changed, onTimeChanged);

    // ── Connect ───────────────────────────────────────────────────────────────
    while (!Particle.connected()) {
        Particle.connect();
        Particle.process();
        delay(100);
    }
    buzzer.playEventWait(BUZZ_CONNECT);

    // ── EEPROM ────────────────────────────────────────────────────────────────
    //  Must come after cloud connection (validateOrMigrateEEPROM may publish).
    if (!validateOrMigrateEEPROM()) {
        Log.error("EEPROM validation failed — running with defaults.");
        initEEPROM();
    }
    validateOrInitConfigExt();
    halMgr1.applyConfigExtToRuntime();
    bumpRebootCount();

    // ── EventManager ─────────────────────────────────────────────────────────
    //  Must come after EEPROM is valid (reads ConfigData / ConfigExt) and
    //  after Particle is connected (subscribes to configured topics immediately).
    //
    //  evMgr.setup() internally:
    //    - copies config fields from EEPROM (lat/lng, jurisdiction, TZ, etc.)
    //    - restores stored event list from EEPROM
    //    - reprocesses all events against current time and config
    //    - registers Particle variables: s_EventLIST, s_ShowConfig, s_Event
    //    - registers Particle function: s_EvIdx
    //    - calls resetSubscriptions() → subscribes to configured FED/STA topics
    //
    //  The lambda routes EventManager's ordered-station output to halMgr1.
    //  Note: this will set the ordered station based on any active/pending
    //  events found in EEPROM, overriding the OSTA value loaded below.
    //  If no events are active, orderedSta defaults to FLAG_FULL (no change).
    evMgr.setup([](FlagStation sta) {
        halMgr1.setOrderedStation(sta);
    });

    // ── Restore ordered station from last saved status ────────────────────────
    //  Read the EEPROM-persisted ordered station and apply it, but only if
    //  EventManager did not already assert a different station.  EventManager
    //  takes priority when an active or pending event exists.
    if (evMgr.orderedStation() == FLAG_FULL) {
        // No event is currently asserting a station — restore saved OSTA
        StatusData status;
        readStatus(status);
        halMgr1.setOrderedStation(status.OSTA);
    }
    // If evMgr set HALF (event in progress) or is waiting for a future event,
    // leave that station in place and let the event lifecycle manage it.

    // ── FSM ───────────────────────────────────────────────────────────────────
    setupFSM(fsm);
    fsm.begin(STATE_STARTUP);
}

// ─────────────────────────────────────────────────────────────────────────────
//  loop()
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
    buzzer.update();

    if (!lidSensor.isPresent() && fsm.currentState() != STATE_LID_OPEN) {
        fsm.enqueueEvent(EVENT_LID_OPEN);
    }

    halMgr1.update();
    fsm.update();
    checkAndReportStatus(false, "RPT");
    serviceRemoteRequests();
    evMgr.loop();       // software-timer event checking → checkForChange()
}

// ─────────────────────────────────────────────────────────────────────────────
//  Cloud function handlers
// ─────────────────────────────────────────────────────────────────────────────

int setHALF(String duration) {
    halMgr1.setForcedStation(FLAG_HALF, validateDuration(duration));
    return 0;
}

int setFULL(String duration) {
    halMgr1.setForcedStation(FLAG_FULL, validateDuration(duration));
    return 0;
}

int remoteClearFault(String arg) {
    g_clearFaultArg       = arg;
    g_clearFaultRequested = true;
    return 1;
}

int setStation(String station) {
    char c = station.toUpperCase().charAt(0);
    if      (c == 'H') { halMgr1.setOrderedStation(FLAG_HALF); }
    else if (c == 'F') { halMgr1.setOrderedStation(FLAG_FULL); }
    else if (c == 'S') { halMgr1.setOrderedStation(FLAG_STOP);
                         halMgr1.setActualStation  (FLAG_STOP); }
    else               { return -1; }
    return 0;
}

int playIDTones(String dummyArg) {
    buzzer.playEvent(BUZZ_ID_TONES);
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Remote-request service  (called every loop)
// ─────────────────────────────────────────────────────────────────────────────

static bool isSafeToClearFault() {
    // Known unsafe conditions — extend here as fault recovery matures.
    if (!lidSensor.isPresent()) return false;   // lid open
    if (halMgr1.isRunning())    return false;   // motor in motion
    return true;
}

void serviceRemoteRequests() {
    if (!g_clearFaultRequested) return;

    if (!isSafeToClearFault()) {
        Particle.publish("ClearFault", "Rejected:not-safe", PRIVATE);
        g_clearFaultRequested = false;
        g_clearFaultArg       = "";
        return;
    }

    fsm.enqueueEvent(EVENT_CLEAR_FAULT);
    g_clearFaultRequested = false;
    g_clearFaultArg       = "";
}

// ─────────────────────────────────────────────────────────────────────────────
//  Utility
// ─────────────────────────────────────────────────────────────────────────────

unsigned long validateDuration(const String& duration) {
    if (duration.length() == 0)         return 3 * 60 * 1000;
    if (duration.equalsIgnoreCase("X")) return 0;

    char* endPtr;
    long minutes = strtol(duration.c_str(), &endPtr, 10);
    if (endPtr == duration.c_str() || *endPtr != '\0') return -1;
    if (minutes < 0) return 0;
    return minutes * 60 * 1000;
}
