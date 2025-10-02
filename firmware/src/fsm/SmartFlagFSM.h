#ifndef SMARTFLAG_FSM_H
#define SMARTFLAG_FSM_H

#include "Particle.h"
#include <functional>
#include "HalyardManager.h"
#include "BuzzerManager.h"

enum FSMEvent {
    EVENT_NONE = 0,
    EVENT_LID_OPEN,
    EVENT_LID_CLOSED,
    EVENT_FAULT,
    EVENT_FLAG_AT_FULL,
    EVENT_FLAG_AT_HALF
    // … add more as needed
};

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

#define MAX_FSM_EVENTS 10
class FSMController {
private:
    FSMState _states[STATE_MAX];
    FSMStateID _current = STATE_NONE;
    FSMEvent eventQueue[MAX_FSM_EVENTS];
    int head = 0;
    int tail = 0;
     
public:
    void addState(FSMStateID id, const FSMState& state);
    void begin(FSMStateID initial);
    void enqueueEvent(FSMEvent evt);
    FSMEvent nextEvent();
    void update();
    FSMStateID currentState() const;
    
     
};

// State registration
void setupFSM(FSMController& fsm);
// unsigned long _fixMeStartTime = 0;

#endif
