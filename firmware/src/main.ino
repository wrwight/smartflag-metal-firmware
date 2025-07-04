#include "Particle.h"
#include "HalyardManager.h"
#include "sensors/SensorManager.h"
#include "BuzzerManager.h"
#include "fsm/SmartFlagFSM.h"
// Include necessary libraries

const int DIR_PIN = D5;  // Direction pin for motor
const int PWM_PIN = D6;  // PWM pin for motor speed control
const int MOTOR_ENABLE_PIN = D7;  // Enable pin for motor control
const int CURRENT_SENSE_PIN = A4;  // Pin for current sensing (if needed)
// Sensor pins
const int SENSOR_ENABLE_PIN = D8;  // Powers D2 & D3 sensors
const int LID_SENSOR_PIN = D10;  // Lid sensor pin - HIGH when closed, LOW when open
const int HALYARD_SENSOR_PIN = D11;  // Halyard sensor pin - HIGH when marker present, LOW when absent

// Let Device OS manage the connection to the Particle Cloud
SYSTEM_MODE(AUTOMATIC);

// Run the application and system concurrently in separate threads
SYSTEM_THREAD(ENABLED);

// Show system, cloud connectivity, and application logs over USB
// View logs with CLI using 'particle serial monitor --follow'
// SerialLogHandler logHandler(LOG_LEVEL_INFO);


BuzzerManager buzzer;   // Future enhancement, initialize buzzer in constructor
HalyardManager halMgr1 (DIR_PIN, PWM_PIN, MOTOR_ENABLE_PIN, CURRENT_SENSE_PIN, false); // Initialize without encoder support
SensorManager sensorMgr( SENSOR_ENABLE_PIN , LID_SENSOR_PIN , HALYARD_SENSOR_PIN ); // Halyard sensor pin A0, Lid sensor pin A1
FSMController fsm; // FSM controller instance

void setup() {
  
  buzzer.begin();
  
  buzzer.playEventWait(BUZZ_POWER_ON);
  
  // Initiate Particle connection
  while (!Particle.connected()) {
    Particle.connect();
    Particle.process();
    delay(100);
  }
  
  buzzer.playEventWait(BUZZ_CONNECT);
  
          pinMode(D10, INPUT);
          pinMode(D11, INPUT);

  Particle.function("RunMotor", handleRunMotor);
  
  
  setupFSM (fsm);
  // fsm.begin(STATE_STARTUP); // Start with the startup state
}

void loop() {
  
  fsm.update(); // Update the FSM state machine
  sensorMgr.update(); // Update sensor states
  buzzer.update();
  halMgr1.update(); // I think maybe the fsm is taking care of this?
  // sensorsString = showSensors();  // Keep updating the status

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
    return 1;
}
