#include "HalyardSequences.h"
#include "HalyardManager.h"

void HalyardSequences::buildLoweringSequence(FSMController& fsm) {
  fsm.addState({
    "START",
    []() { HalyardManager::runMotor(CW, 0, 255); },
    []() {},
    []() { return millis() - fsm.getStateStartTime() > 3000; },
    "STOP"
  });

  fsm.addState({
    "STOP",
    []() { HalyardManager::stopMotor(); },
    []() {},
    []() { return false; },  // Final state
    nullptr
  });
}

void HalyardSequences::buildTestSequence(FSMController& fsm) {
  fsm.addState({
    "CW",
    []() { HalyardManager::runMotor(CW, 0, 200); },
    []() {},
    []() { return millis() - fsm.getStateStartTime() > 1000; },
    "PAUSE"
  });

  fsm.addState({
    "PAUSE",
    []() { HalyardManager::stopMotor(); },
    []() {},
    []() { return millis() - fsm.getStateStartTime() > 500; },
    "CCW"
  });

  fsm.addState({
    "CCW",
    []() { HalyardManager::runMotor(CCW, 0, 200); },
    []() {},
    []() { return millis() - fsm.getStateStartTime() > 1000; },
    "DONE"
  });

  fsm.addState({
    "DONE",
    []() { HalyardManager::stopMotor(); },
    []() {},
    []() { return false; },
    nullptr
  });
}
