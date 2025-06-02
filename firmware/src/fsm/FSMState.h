#ifndef FSM_STATE_H
#define FSM_STATE_H

#include "Particle.h"
#include <functional>

struct FSMState {
  const char* name;
  std::function<void()> onEnter;
  std::function<void()> onUpdate;
  std::function<bool()> shouldAdvance;
  const char* nextState;
};

#endif
