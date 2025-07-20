#ifndef SMARTFLAG_FSM_H
#define SMARTFLAG_FSM_H

#include "Particle.h"
#include <functional>
#include "HalyardManager.h"
#include "BuzzerManager.h"

enum FSMStateID {
    STATE_NONE = 0,
    STATE_STARTUP,
    STATE_ON_STATION,
    STATE_CALIBRATION,
    STATE_MOVING_TO_STATION,
    STATE_LID_OPEN,
    STATE_FAULT_RECOVERY,
    STATE_MAX
};

class FSMState {
public:
    std::function<void()> onEnter;
    std::function<void()> onUpdate;
    std::function<void()> onExit;
    std::function<FSMStateID()> shouldAdvance;
};

class FSMController {
    private:
    FSMState _states[STATE_MAX];
    FSMStateID _current = STATE_NONE;
    
    public:
    void addState(FSMStateID id, const FSMState& state);
    void begin(FSMStateID initial);
    void update();
    FSMStateID currentState() const;
};

// State registration
void setupFSM(FSMController& fsm);
// unsigned long _fixMeStartTime = 0;

#endif
