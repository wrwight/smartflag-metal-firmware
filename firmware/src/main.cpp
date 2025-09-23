/******************************************************/
//       THIS IS A GENERATED FILE - DO NOT EDIT       //
/******************************************************/

#line 1 "c:/SmartFlagRepos/smartflag-metal-firmware/firmware/src/main.ino"

// Include necessary libraries
#include "Particle.h"
#include "HalyardManager.h"
#include "sensors/Sensor.h"
#include "BuzzerManager.h"
#include "fsm/SmartFlagFSM.h"

// Define product ID and version for Particle Cloud
// PRODUCT_ID(38638) // Unique product ID for SmartFlag-Gen3 (discontinued after 4.0.0)
void setup();
void loop();
int setHALF(String duration);
int setFULL(String duration);
unsigned int validateDuration(const String& duration);
int playIDTones();
int handleRunMotor(String json);
String getStatus();
#line 11 "c:/SmartFlagRepos/smartflag-metal-firmware/firmware/src/main.ino"
PRODUCT_VERSION(1)

// // Thermistor coefs                                 These values may need to be tuned
// #define NTC_NOMINAL_RESISTANCE                      10000
// #define NTC_BETA                                    3425    // 3401 to 3450 with +- 1% tol
// #define NTC_SERRIES_RESISTANCE                      10000

// ADC pins
#define SET_PIN(x)                                  ((pin_t)(x))
#define PD15                                    SET_PIN(15)
#define PD16                                    SET_PIN(16)
#define PA3                                     PD16
#define PA4                                     PD15
#define INPUT_VOLT_SENSE_PIN                        PD16
#define MOTOR_CURRENT_PIN                           PD15

// ADC coefs
#define ADC_MAX                                     4095
#define ADC_REF_VOLT                                3.3
#define ADC_TO_VOLT(x)                              ((double)((x) * ADC_REF_VOLT / ADC_MAX))
#define INPUT_VOLT_SCALE_FAC                        8       // Voltage sensing coefs
#define MOTOR_VOLT_TO_AMP_FAC                       0.661   // Motor current sensing coefs


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
    Particle.function("GotoHALF", setHALF);
    Particle.function("GotoFULL", setFULL);
    Particle.function("PlayID", playIDTones); // Play ID tones for debugging
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

int setHALF(String duration) {
    halMgr1.setForcedStation(FLAG_HALF, validateDuration(duration)); // Set forced station with provided duration
    return 0; // Success
}

int setFULL(String duration) {
    halMgr1.setForcedStation(FLAG_FULL, validateDuration(duration)); // Set forced station with provided duration
    return 0; // Success
}

unsigned int validateDuration(const String& duration) {

    if ( duration.length() == 0) return 3 * 60 * 1000;      // Handle null or blank input → default to 3 minutes

    if (duration.equalsIgnoreCase("X")) return 0;   // Return 0 if input is "X" (case-insensitive)

    // Try to convert to integer
    char* endPtr;
    long minutes = strtol(duration.c_str(), &endPtr, 10);

    // Check for conversion failure or extra characters
    if (endPtr == duration.c_str() || *endPtr != '\0') {
        return -1;  // Invalid input
    }

    // Return 0 if negative
    if (minutes < 0) {
        return 0;
    }

    return minutes * 60 * 1000;  // Convert to milliseconds
}

int playIDTones() {
    buzzer.playEventWait(BUZZ_ID_TONES); // Play identifier sound
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


