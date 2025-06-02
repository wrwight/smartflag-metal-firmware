#ifndef FSM_STATES_H
#define FSM_STATES_H

#include "FSMController.h"
#include "HalyardManager.h"
#include "SensorManager.h"

class FSMStates {
public:
  static void motorToSensorStop(FSMController& fsm,
                                const char* stateName,
                                Direction dir,
                                const char* targetPosition,
                                unsigned long timeoutMs,
                                const char* nextState);

  static void buildErrorState(FSMController& fsm, const char* stateName = "HALYARD_ERROR");
};

#endif
