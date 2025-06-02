#include "fsm/FSMController.h"
#include "fsm/FSMState.h"
#include "HalyardManager.h"
#include "HalyardSequences.h"

FSMController halyardFSM;

void setup() {
  HalyardManager::initialize(D5, D6);

  // Choose a sequence to run:
  HalyardSequences::buildLoweringSequence(halyardFSM);
  // HalyardSequences::buildTestSequence(halyardFSM);

  halyardFSM.start("START");
}

void loop() {
  SensorManager::update();

  // Lid interrupt
  if (SensorManager::lidJustOpened()) {
    HalyardManager::stopMotor();
    halyardFSM.stop();
  } else if (SensorManager::lidJustClosed()) {
    HalyardSequences::buildCalibrateSequence(halyardFSM);
    halyardFSM.start("START");
  }

  if (!SensorManager::lidOpen()) {
    halyardFSM.loop();
    HalyardManager::update(); // if duration was 0, we think this will stop the motor immediately
  }
}
