#ifndef FSM_CONTROLLER_H
#define FSM_CONTROLLER_H

#include "Particle.h"
#include "FSMState.h"
#include <map>

class FSMController {
public:
  void addState(const FSMState& state);
  void start(const char* initialStateName);
  void loop();  // Call from main loop
  bool isRunning() const;
  void stop();
  unsigned long getStateStartTime() const;

private:
  std::map<String, FSMState> _states;
  FSMState* _current = nullptr;
  unsigned long _stateStartTime = 0;
  bool _active = false;

  void transitionTo(const char* stateName);
};

#endif
