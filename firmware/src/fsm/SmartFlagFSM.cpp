#include "SmartFlagFSM.h"

// External references

extern HalyardManager halMgr1;
extern BuzzerManager buzzer;
// extern EventManager eventManager;
extern SensorManager sensorMgr;
// extern Diagnostics diagnostics;

void defineIdleState(FSMController& fsm) {
    static FSMStateID fsmNextState = STATE_NONE;

    FSMState state;
    state.onEnter = []() {
        fsmNextState = STATE_NONE;
        Serial.println("Idle: Monitoring station state");
    };

    state.onUpdate = []() {
        OrderedStation ordered = halMgr1.getOrderedStation();
        ActualStation actual = halMgr1.getActualStation();

        if (actual == STATION_FULL && ordered == ORDERED_HALF) {
            fsmNextState = STATE_PENDING_FROM_FULL;
        } else if (actual == STATION_HALF && ordered == ORDERED_FULL) {
            fsmNextState = STATE_PENDING_FROM_HALF;
        // } else if (actual == STATION_HALF && ordered == ORDERED_HALF && eventManager.isIndefinite()) {
        //     fsmNextState = STATE_PENDING_INDEFINITE;
        }
    };
    state.onExit = []() {};
    state.shouldAdvance = []() -> FSMStateID {
        return fsmNextState;
    };
    fsm.addState(STATE_IDLE_FULL, state);  // Reused ID for now
}

void defineStartupState(FSMController& fsm) {
    static FSMStateID fsmNextState = STATE_NONE;

    FSMState state;
    state.onEnter = []() {
        fsmNextState = STATE_NONE;
        Serial.println("Startup: Initializing...");
    };
    state.onUpdate = []() {
        fsmNextState = STATE_IDLE_FULL;  // Placeholder: assume flag at full initially
    };
    state.onExit = []() {};
    state.shouldAdvance = []() -> FSMStateID { return fsmNextState; };
    fsm.addState(STATE_STARTUP, state);
}

void defineLidOpenState(FSMController& fsm) {
    static FSMStateID fsmNextState = STATE_NONE;

    FSMState state;
    state.onEnter = []() {
        fsmNextState = STATE_NONE;
        halMgr1.invalidateStation();  // reset position
        Serial.println("Lid open – motion disabled");
    };
    state.onUpdate = []() {
        if (sensorMgr.lidClosed()) {
            fsmNextState = STATE_STARTUP;
        }
    };
    state.onExit = []() {
        Serial.println("Lid closed – resuming");
    };
    state.shouldAdvance = []() -> FSMStateID { return fsmNextState; };
    fsm.addState(STATE_LID_OPEN, state);
}

void defineFixMeState(FSMController& fsm) {
    static FSMStateID fsmNextState = STATE_NONE;
    FSMState state;
    state.onEnter = []() {
        fsmNextState = STATE_NONE;
        Serial.println("ERROR: Entering FIX_ME");
        buzzer.playPattern("4444", "____");
    };
    
    state.onUpdate = []() {
        // if (diagnostics.resetRequested()) {
        //     fsmNextState = STATE_STARTUP;
        // }
    };

    state.onExit = []() {
    };

    state.shouldAdvance = []() -> FSMStateID { return fsmNextState; };
    fsm.addState(STATE_FIX_ME, state);
}

// Unified FSM setup function
void setupFSM(FSMController& fsm) {
    defineStartupState(fsm);
    defineIdleState(fsm);            // Unified idle state
    defineLidOpenState(fsm);
    defineFixMeState(fsm);
}

// FSMController implementation
void FSMController::addState(FSMStateID id, const FSMState& state) {
    if (id > STATE_NONE && id < STATE_MAX) {
        _states[id] = state;
    }
}

void FSMController::begin(FSMStateID initial) {
    _current = initial;
    if (_states[_current].onEnter) _states[_current].onEnter();
}

void FSMController::update() {
    if (_current == STATE_NONE || _current >= STATE_MAX) return;
    FSMState& state = _states[_current];
    if (state.onUpdate) state.onUpdate();
    if (state.shouldAdvance) {
        FSMStateID next = state.shouldAdvance();
        if (next != STATE_NONE && next != _current) {
            if (state.onExit) state.onExit();
            if (_states[next].onEnter) _states[next].onEnter();
            _current = next;
        }
    }
}

FSMStateID FSMController::currentState() const {
    return _current;
}
