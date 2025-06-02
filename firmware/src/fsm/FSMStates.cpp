#include "FSMStates.h"

void FSMStates::motorToSensorStop(FSMController& fsm,
                                   const char* stateName,
                                   Direction dir,
                                   const char* targetPosition,
                                   unsigned long timeoutMs,
                                   const char* nextState) {
  fsm.addState({
    stateName,
    [dir, targetPosition]() {
      HalyardManager::setTargetPosition(targetPosition);
      HalyardManager::runMotor(dir, 0, 255);
    },
    []() {
      if (SensorManager::halyardTriggered()) {
        HalyardManager::handleSensorTriggered();
      }
    },
    [timeoutMs, &fsm]() {
      return SensorManager::halyardTriggered() ||
             millis() - fsm.getStateStartTime() > timeoutMs;
    },
    nextState
  });
}

void FSMStates::buildErrorState(FSMController& fsm, const char* stateName) {
  fsm.addState({
    stateName,
    []() {
      HalyardManager::stopMotor();
      Log.error("Entered HALYARD_ERROR state");
      Particle.publish("smartflag/error", "HALYARD_ERROR", PRIVATE);
    },
    []() {},
    []() { return false; },  // Never advance automatically
    nullptr
  });
}