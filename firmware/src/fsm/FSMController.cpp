#include "FSMController.h"

void FSMController::addState(const FSMState& state) {
  _states[state.name] = state;
}

void FSMController::start(const char* initialStateName) {
  _active = true;
  transitionTo(initialStateName);
}

void FSMController::stop() {
  _active = false;
  _current = nullptr;
}

bool FSMController::isRunning() const {
  return _active;
}

unsigned long FSMController::getStateStartTime() const {
  return _stateStartTime;
}

void FSMController::loop() {
  if (!_active || _current == nullptr) return;

  if (_current->onUpdate) {
    _current->onUpdate();
  }

  if (_current->shouldAdvance && _current->shouldAdvance()) {
    if (_current->nextState) {
      transitionTo(_current->nextState);
    } else {
      stop();  // End of state machine
    }
  }
}

void FSMController::transitionTo(const char* stateName) {
  auto it = _states.find(stateName);
  if (it == _states.end()) {
    Log.error("FSM: Unknown state %s", stateName);
    stop();
    return;
  }

  _current = &it->second;
  _stateStartTime = millis();
  if (_current->onEnter) {
    _current->onEnter();
  }
}
