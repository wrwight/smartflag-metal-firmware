#include "Particle.h"
#include "HalyardManager.h"
#include "sensors/Sensor.h"
#include "BuzzerManager.h"
#include "fsm/SmartFlagFSM.h"
// Include necessary libraries

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
SYSTEM_THREAD(ENABLED);

// Show system, cloud connectivity, and application logs over USB
// View logs with CLI using 'particle serial monitor --follow'
// SerialLogHandler logHandler(LOG_LEVEL_INFO);

BuzzerManager buzzer;   // Future enhancement, initialize buzzer in constructor
HalyardManager halMgr1 (DIR_PIN, PWM_PIN, MOTOR_ENABLE_PIN, CURRENT_SENSE_PIN, false); // Initialize without encoder support
FSMController fsm; // FSM controller instance
Sensor halfSensor(HALF_SENSOR_PIN);       // Half marker active (near) when pin reads LOW (default)
Sensor fullSensor(FULL_SENSOR_PIN);       // Full marker active (near) when pin reads LOW (default)
Sensor lidSensor(LID_SENSOR_PIN);         // Lid sensor active (closed) when pin reads LOW (default)

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

// Initialize the halyard manager
    halMgr1.begin(); // Set up motor pins and initial state

// Initiate Particle connection
    while (!Particle.connected()) {
        Particle.connect();
        Particle.process();
        delay(100);
    }

// Connected!
    buzzer.playEventWait(BUZZ_CONNECT); // T-mobile jingle approximation

    // Debugging functions
    Particle.function("RunMotor", handleRunMotor);
    Particle.function("SetStation", setStation);
    Particle.variable("Status", getStatus);

    setupFSM (fsm);
    fsm.begin(STATE_STARTUP); // Start with the startup state
}

void loop() {

    buzzer.update();

    
    if ( !lidSensor.isPresent() && fsm.currentState() != STATE_LID_OPEN ) { // If the lid just opened
        fsm.begin(STATE_LID_OPEN); // Interrupt current state and go to LID_OPEN
    } else if (lidSensor.isPresent() && fsm.currentState() == STATE_LID_OPEN) {
        fsm.begin(STATE_CALIBRATION); // Must calibrate again
    }
    
    halMgr1.update();       // Update the halyard manager state
    fsm.update();           // Update the FSM state machine
}

int setStation(String json) {
    JSONValue root = JSONValue::parseCopy(json);
    if (!root.isObject()) return -1; // Invalid JSON format

    JSONObjectIterator iter(root);

    while (iter.next()) {
        String upName = String(iter.name()).toUpperCase();
        if (upName == "HALF") {
            halMgr1.setOrderedStation(FLAG_HALF);
        } else if (upName == "FULL") {
            halMgr1.setOrderedStation(FLAG_FULL);
        } else if (upName == "UNKNOWN") {
            halMgr1.setOrderedStation(FLAG_UNKNOWN);
        } else {
            return -2; // Unknown parameter
        }
    }

    return 0; // Success
}

int handleRunMotor(String json) {
    JSONValue root = JSONValue::parseCopy(json);
    if (!root.isObject()) return -1; // Invalid JSON format

    JSONObjectIterator iter(root);

    Direction dir = CCW; // Default direction
    unsigned int dur = 5000;  // Default duration is 5000 ms (5 seconds)
    unsigned int spd = 255; // Default speed is 255 (full speed)
    unsigned int rmp = 1500; // Default ramp time is 50 ms

    while (iter.next()) {
      String upName = String(iter.name()).toUpperCase();
        if (upName == "DIR") {
            String dirStr = String(iter.value().toString()).toUpperCase();
            if (dirStr == "CW" || dirStr == "DN" || dirStr == "DOWN") {
                dir = CW; // Clockwise
            } else if (dirStr == "CCW" || dirStr == "UP" ) {
                dir = CCW; // Counter-clockwise
            } else {
                return -2; // Invalid direction value
            }
            // dir = (String(iter.value().toString()).toUpperCase() == "CW") ? CW : CCW;
        } else if (upName == "DUR") {
            dur = iter.value().toUInt();
            if ( dur < 100 ) dur = dur * 1000; // Convert seconds to milliseconds
            if ( dur > 60000) {
                return -3; // Invalid duration value
            }
        } else if (upName == "SPD") {
            spd = iter.value().toUInt();
        } else if (upName == "RMP") {
            rmp = iter.value().toUInt();
        } else if (upName == "OSTA") {
            String value = String(iter.value().toString()).toUpperCase();
            halMgr1.setOrderedStation(value == "HALF" ? FLAG_HALF : FLAG_FULL);
        } else {
            return -4; // Unknown parameter
        }
    }
    if ( rmp > dur ) rmp = dur; // Ensure ramp time does not exceed duration

    halMgr1.runMotor(dir, dur, spd, rmp);
    return dur;
}


String getStatus() {
    String statusReport;
    String stateStr;
    String stationNames[] = {"UNKNOWN", "FULL", "HALF"};

    switch (fsm.currentState()) {
        case STATE_NONE: stateStr = "NONE"; break;
        case STATE_STARTUP: stateStr = "STARTUP"; break;
        case STATE_ON_STATION: stateStr = "ON_STATION"; break;
        case STATE_CALIBRATION: stateStr = "CALIBRATION"; break;
        case STATE_MOVING_TO_STATION: stateStr = "MOVING_TO_STATION"; break;
        case STATE_LID_OPEN: stateStr = "LID_OPEN"; break;
        case STATE_FAULT_RECOVERY: stateStr = "FAULT_RECOVERY"; break;
        default: stateStr = "UNKNOWN"; break;
    }

    String orderedStr  = stationNames[halMgr1.getOrderedStation()];
    String actualStr   = stationNames[halMgr1.getActualStation()];
    String motorStr    = halMgr1.isRunning() ? "RUNNING" : "STOPPED";

    statusReport = String::format(
        "FSM:%s OSTA:%s ASTA:%s MTR:%s",
        stateStr.c_str(),
        orderedStr.c_str(),
        actualStr.c_str(),
        motorStr.c_str() );
    return statusReport;
}
