/******************************************************/
//       THIS IS A GENERATED FILE - DO NOT EDIT       //
/******************************************************/

#include "Particle.h"
#line 1 "c:/SmartFlagRepos/smartflag-metal-firmware/firmware/src/main.ino"
#include "fsm/FSMController.h"
#include "fsm/FSMState.h"
#include "HalyardManager.h"
#include "sensors/SensorManager.h"
#include "BuzzerManager.h"


// Let Device OS manage the connection to the Particle Cloud
void setup();
void loop();
#line 10 "c:/SmartFlagRepos/smartflag-metal-firmware/firmware/src/main.ino"
SYSTEM_MODE(AUTOMATIC);

// Run the application and system concurrently in separate threads
SYSTEM_THREAD(ENABLED);

// Show system, cloud connectivity, and application logs over USB
// View logs with CLI using 'particle serial monitor --follow'
// SerialLogHandler logHandler(LOG_LEVEL_INFO);


BuzzerManager buzzer;
HalyardManager halyardManager(D5, D6, D7, A4, false); // Initialize without encoder support
SensorManager sensorManager(D8, D2, D3); // Halyard sensor pin D3, Lid sensor pin D2
FSMController halyardFSM;

void setup() {

  buzzer.begin();
  buzzer.playPattern("111");

  // Wait for boot tones to finish (non-blocking wait)

  while (!buzzer.isFinished()) {
      buzzer.update();
      Particle.process();  // Ensure cloud background tasks
  }

  // Begin Particle connection
  Particle.connect();

  while (!Particle.connected()) {
    Particle.connect();
    Particle.process();
    delay(100);
  }

  Particle.publish("smartflag/info", "Halyard system starting up", PRIVATE);
  
  HalyardManager::initialize(D5, D6);

  halyardFSM.start("START");
}

void loop() {
  SensorManager::update();

  // Lid interrupt
  if (SensorManager::lidJustOpened()) {
    HalyardManager::stopMotor();
    halyardFSM.stop();
  } else if (SensorManager::lidJustClosed()) {
    halyardFSM.start("START");
  }

  bool playedCloudTone = false;

  buzzer.update();
  if (Particle.connected() && !playedCloudTone) {
      buzzer.playPattern("222");
      playedCloudTone = true;
  }

  if (!SensorManager::lidOpen()) {
    halyardFSM.loop();
    HalyardManager::update(); // if duration was 0, we think this will stop the motor immediately
  }
}
