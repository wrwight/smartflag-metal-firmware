#include "SmartFlagFSM.h"

// External references

extern HalyardManager halMgr1;
extern BuzzerManager buzzer;
// extern EventManager eventManager;
extern SensorManager sensorMgr;
// extern Diagnostics diagnostics;

void defineStartupState(FSMController& fsm) {
    static FSMStateID fsmNextState = STATE_NONE;

    FSMState state;
    state.onEnter = []() {
        fsmNextState = STATE_NONE;
        Serial.println("Startup: Initializing...");
    };
    state.onUpdate = []() {
        fsmNextState = STATE_CALIBRATION;  // On startup, calibration is required
    };
    state.onExit = []() {};
    state.shouldAdvance = []() -> FSMStateID { return fsmNextState; };
    fsm.addState(STATE_STARTUP, state);
}

void defineOnStationState(FSMController& fsm) {
    static FSMStateID fsmNextState = STATE_NONE;

    FSMState state;
    state.onEnter = []() {
        fsmNextState = STATE_NONE;
        // Serial.println("On station: Monitoring station state");
    };    

    state.onUpdate = []() {
        FlagStation ordered = halMgr1.getOrderedStation();
        FlagStation actual = halMgr1.getActualStation();
        
        if (actual != ordered ) {
            fsmNextState = STATE_MOVING_TO_STATION;
        }
    };    
    state.onExit = []() {};
    state.shouldAdvance = []() -> FSMStateID {
        return fsmNextState;
    };    
    fsm.addState(STATE_ON_STATION, state);
}    

void defineCalibrationState(FSMController& fsm) {
    static FSMStateID fsmNextState = STATE_NONE;
    
    FSMState state;
    state.onEnter = []() {

        fsmNextState = STATE_CALIBRATE_DOWN;    // Default to downward calibration
        
        if ( !sensorMgr.markerPresent() ) { fsmNextState = STATE_MOVING_TO_STATION; }

        FlagStation actual = halMgr1.getActualStation();
        if (actual == FLAG_HALF) { fsmNextState = STATE_CALIBRATE_UP; }
    };    

    state.onUpdate = []() {};    
    state.onExit = []() {};
    state.shouldAdvance = []() -> FSMStateID {
        return fsmNextState;
    };    
    fsm.addState(STATE_CALIBRATION, state);
}    

void defineCalibrateDownState(FSMController& fsm) {
    static FSMStateID fsmNextState = STATE_NONE;

    FSMState state;
    state.onEnter = []() {
        fsmNextState = STATE_NONE;
        // Move motor DOWN slowly (ignoring sensor)
    };    

    state.onUpdate = []() {
        // Watch for sensor to go OFF -> MOVING_TO_STATION
    };    

    state.onExit = []() {
        // Stop motor?
    };
    state.shouldAdvance = []() -> FSMStateID {
        return fsmNextState;
    };    
    fsm.addState(STATE_CALIBRATE_DOWN, state);
}

void defineCalibrateUpState(FSMController& fsm) {
    static FSMStateID fsmNextState = STATE_NONE;

    FSMState state;
    state.onEnter = []() {
        fsmNextState = STATE_NONE;
        // Move motor UP slowly (ignoring sensor)
    };    

    state.onUpdate = []() {
        // Watch for sensor to go OFF -> MOVING_TO_STATION
    };    

    state.onExit = []() {
        // Stop motor?
    };
    state.shouldAdvance = []() -> FSMStateID {
        return fsmNextState;
    };    
    fsm.addState(STATE_CALIBRATE_UP, state);
}

void defineMovingToStationState(FSMController& fsm) {
    static FSMStateID fsmNextState = STATE_NONE;

    FSMState state;
    state.onEnter = []() {
        fsmNextState = STATE_NONE;
        // Move motor UP or DOWN to reach the station
        if (halMgr1.getOrderedStation() == FLAG_HALF) {
            buzzer.playEventWait(BUZZ_FLAG_DOWN);
            halMgr1.runMotor(CW, 120000, 255, 1500); // Move down to HALF
        } else {
            buzzer.playEventWait(BUZZ_FLAG_UP);
            halMgr1.runMotor(CCW, 120000, 255, 1500); // Move up to FULL
        }
    };    

    state.onUpdate = []() {
        if (!halMgr1.isRunning()) {             // Continue moving until the motor stops
            // Need some error checking in here for stalls or timeouts
            fsmNextState = STATE_ON_STATION;    // Once the motor stops, we are on station
        }
    };    

    state.onExit = []() {};
    state.shouldAdvance = []() -> FSMStateID {
        return fsmNextState;
    };    
    fsm.addState(STATE_MOVING_TO_STATION, state);
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

void defineFaultRecoveryState(FSMController& fsm) {
    static FSMStateID fsmNextState = STATE_NONE;
    static unsigned long _fixMeStartTime = 0; // Start time for fault recovery

    FSMState state;
    state.onEnter = []() {
        fsmNextState = STATE_NONE;
        Serial.println("ERROR: Entering FIX_ME");
        buzzer.playPattern("4444", "____");
        _fixMeStartTime = millis();  // Record the start time for the fault recovery    
    };
    
    state.onUpdate = []() {
        if ( ( millis() - _fixMeStartTime ) > 30000UL ) { // 30 seconds timeout
            fsmNextState = STATE_CALIBRATION;  // If no resolution, return to calibration
        }
    };

    state.onExit = []() {
    };

    state.shouldAdvance = []() -> FSMStateID { return fsmNextState; };
    fsm.addState(STATE_FAULT_RECOVERY, state);
}

// Unified FSM setup function
void setupFSM(FSMController& fsm) {
    defineStartupState(fsm);
    defineOnStationState(fsm);            // Unified idle state
    defineCalibrationState(fsm);
    defineCalibrateDownState(fsm);
    defineCalibrateUpState(fsm);
    defineMovingToStationState(fsm);
    defineLidOpenState(fsm);
    defineFaultRecoveryState(fsm);
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
        if (next != STATE_NONE && next != _current) {       // remove _current check to allow re-entrance
            if (state.onExit) state.onExit();
            if (_states[next].onEnter) _states[next].onEnter();
            _current = next;
        }
    }
}

FSMStateID FSMController::currentState() const {
    return _current;
}
