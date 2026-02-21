// Include necessary libraries
// #include "JsonParserGeneratorRK.h"
// using namespace JsonWriterGeneratorRK;  
#include "Particle.h"
#include "HalyardManager.h"
#include "FaultManager.h"
#include "sensors/Sensor.h"
#include "BuzzerManager.h"
#include "fsm/SmartFlagFSM.h"
#include "EEPROMManager.h"
#include "FlagUtils.h"

/**************/
// Define product ID and version for Particle Cloud
// PRODUCT_ID(38638) // Unique product ID for SmartFlag-Gen3 (discontinued after 4.0.0)
PRODUCT_VERSION(6) // Incremented for version 6 (status reporting refactor)

// // Thermistor coefs                                 These values may need to be tuned
// #define NTC_NOMINAL_RESISTANCE                      10000
// #define NTC_BETA                                    3425    // 3401 to 3450 with +- 1% tol
// #define NTC_SERRIES_RESISTANCE                      10000

// Forward declarations
int remoteClearFault(String arg);
void serviceRemoteRequests();

// Remote request latch
static volatile bool g_clearFaultRequested = false;
static String g_clearFaultArg = "";

const int DIR_PIN = D5;  // Direction pin for motor
const int PWM_PIN = D6;  // PWM pin for motor speed control
const int MOTOR_ENABLE_PIN = D7;  // Enable pin for motor control
const int CURRENT_SENSE_PIN = A4;  // Pin for current sensing (if needed)
// Sensor pins
const int SENSOR_ENABLE_PIN = D8;  // Powers LID sensor
const int HALF_SENSOR_PIN = D10;  // Half marker active when pin reads LOW (default)
const int FULL_SENSOR_PIN = D11;  // Half marker active when pin reads LOW (default)
const int LID_SENSOR_PIN = D12;  // Lid sensor pin - LOW when closed, HIGH when open

// Let Device OS manage the connection to the Particle Cloud
SYSTEM_MODE(AUTOMATIC);

// Run the application and system concurrently in separate threads
// SYSTEM_THREAD(ENABLED);

// Show system, cloud connectivity, and application logs over USB
// View logs with CLI using 'particle serial monitor --follow'
// SerialLogHandler logHandler(LOG_LEVEL_INFO);

BuzzerManager buzzer;   // Future enhancement, initialize buzzer in constructor
HalyardManager halMgr1 (DIR_PIN, PWM_PIN, MOTOR_ENABLE_PIN, CURRENT_SENSE_PIN, false); // Initialize without encoder support
FSMController fsm; // FSM controller instance
FaultManager faultMgr; // Fault manager instance
Sensor halfSensor(HALF_SENSOR_PIN);       // Half marker active (near) when pin reads LOW (default)
Sensor fullSensor(FULL_SENSOR_PIN);       // Full marker active (near) when pin reads LOW (default)
Sensor lidSensor(LID_SENSOR_PIN);         // Lid sensor active (closed) when pin reads LOW (default)

// Timer to update smoothed amps every 100 ms
Timer halMgr1AmpsTimer(100, [](){ halMgr1.updateSmoothedAmps(); });

void setup() {
          
    buzzer.begin();
    buzzer.playEventWait(BUZZ_POWER_ON);  // Play power-on sound ("Off we go...")
    
    // Initialize sensors
    halfSensor.begin(); // Initialize half sensor
    fullSensor.begin(); // Initialize full sensor
    
    // Turn the LID sensor on
    pinMode( SENSOR_ENABLE_PIN, OUTPUT); // Lid sensor enable pin
    digitalWrite( SENSOR_ENABLE_PIN, LOW); // Enable lid sensor by default
    lidSensor.begin(); // Initialize lid sensor
    
    // Initialize the halyard manager and its amp smoothing timer
    halMgr1.begin(); // Set up motor pins and initial state
    halMgr1AmpsTimer.start();

// Initiate Particle connection
    while (!Particle.connected()) {
        Particle.connect();
        Particle.process();
        delay(100);
    }

// Connected!
    buzzer.playEventWait(BUZZ_CONNECT); // Connection jingle

// Validate or initialize EEPROM
    if (!validateOrMigrateEEPROM()) {
        Log.error("EEPROM validation failed — running with defaults.");
        initEEPROM();
    }
    validateOrInitConfigExt();          // Configuration extension
    halMgr1.applyConfigExtToRuntime();      // Apply extended config to runtime
    bumpRebootCount();

    // Load Ordered Station from EEPROM
    StatusData status;
    readStatus(status);
    halMgr1.setOrderedStation(status.OSTA);  // Set ordered station from EEPROM without saving again

    // Particle functions
    // Particle.function("RunMotor", handleRunMotor);
    Particle.function("TempHALF", setHALF);
    Particle.function("TempFULL", setFULL);
    Particle.function("OrderHFS", setStation); // Set ordered station
    Particle.function("PlayID", playIDTones); // Play ID tones for debugging
    Particle.function("SetConfig", setConfigHandler);
    Particle.function("clearFault", remoteClearFault);
    Particle.function("dbg", dbgToggle);

    Particle.variable("Config", configToJSON);
    Particle.variable("Status", []() -> String { return getStatus("QRY"); });

    setupFSM(fsm);
    fsm.begin(STATE_STARTUP); // Start with the startup state
}

void loop() {

    buzzer.update();
    
    if (!lidSensor.isPresent() && fsm.currentState() != STATE_LID_OPEN) {
        fsm.enqueueEvent(EVENT_LID_OPEN);
    }
    
    halMgr1.update();       // Update the halyard manager state
    fsm.update();           // Update the FSM state machine
    checkAndReportStatus(false, "RPT");   // Periodic status report
    serviceRemoteRequests();
}

int setHALF(String duration) {
    halMgr1.setForcedStation(FLAG_HALF, validateDuration(duration));
    return 0;
}

int setFULL(String duration) {
    halMgr1.setForcedStation(FLAG_FULL, validateDuration(duration));
    return 0;
}

int remoteClearFault(String arg) {
    g_clearFaultArg = arg;
    g_clearFaultRequested = true;
    return 1;
}

static bool isSafeToClearFault() {
    bool lidClosed    = lidSensor.isPresent();
    bool motorInactive = !halMgr1.isRunning();
    bool fsmIdle      = true;
    // fsmIdle = fsm.isInIdleState(); // placeholder
    return lidClosed && motorInactive && fsmIdle;
}

void serviceRemoteRequests() {
    if (!g_clearFaultRequested) return;

    if (!isSafeToClearFault()) {
        Particle.publish("ClearFault", "Rejected:not-safe", PRIVATE);
        g_clearFaultRequested = false;
        g_clearFaultArg = "";
        return;
    }

    fsm.enqueueEvent(EVENT_CLEAR_FAULT);
    g_clearFaultRequested = false;
    g_clearFaultArg = "";
}

unsigned long validateDuration(const String& duration) {
    if (duration.length() == 0)          return 3 * 60 * 1000;
    if (duration.equalsIgnoreCase("X"))  return 0;

    char* endPtr;
    long minutes = strtol(duration.c_str(), &endPtr, 10);
    if (endPtr == duration.c_str() || *endPtr != '\0') return -1;
    if (minutes < 0) return 0;
    return minutes * 60 * 1000;
}

int setStation(String station) {
    if (String(station.toUpperCase().charAt(0)) == "H") {
        halMgr1.setOrderedStation(FLAG_HALF);
    } else if (String(station.toUpperCase().charAt(0)) == "F") {
        halMgr1.setOrderedStation(FLAG_FULL);
    } else if (String(station.toUpperCase().charAt(0)) == "S") {
        halMgr1.setOrderedStation(FLAG_STOP);
        halMgr1.setActualStation(FLAG_STOP);
    } else {
        return -1;
    }
    return 0;
}

int playIDTones(String dummyArg) {
    buzzer.playEvent(BUZZ_ID_TONES);
    return 0;
}
