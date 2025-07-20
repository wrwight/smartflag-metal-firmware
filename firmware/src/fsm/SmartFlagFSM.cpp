#include "SmartFlagFSM.h"
#include "sensors/Sensor.h"

// External references

extern HalyardManager halMgr1;
extern BuzzerManager buzzer;

// extern EventManager eventManager;
// extern Diagnostics diagnostics;

extern Sensor lidSensor;        // Lid sensor 
extern Sensor halfSensor;       // Half marker 
extern Sensor fullSensor;       // Full marker 

void defineStartupState(FSMController& fsm) {
    static FSMStateID fsmNextState = STATE_NONE;

    FSMState state;
    state.onEnter = []() {
        fsmNextState = STATE_NONE;
        // buzzer.playEventWait(BUZZ_STARTUP);

        // Load configuration from EEPROM
        // Serial.println("Startup: Initializing...");
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
        // buzzer.playEventWait(BUZZ_ON_STATION);
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

        // buzzer.playEventWait(BUZZ_CALIB); // Play "Connected" sound

        // Ensure a valid Ordered station is set, or default to FULL

        if ( halMgr1.getOrderedStation() != FLAG_FULL && halMgr1.getOrderedStation() != FLAG_HALF ) {
            halMgr1.setOrderedStation(FLAG_FULL);  // Default to FULL station
        }

        // If only one sensor is present, set the station accordingly
        
        if ( fullSensor.isPresent() && !halfSensor.isPresent() ) {
            buzzer.playEvent(BUZZ_FULL);
            halMgr1.setActualStation(FLAG_FULL);  // Set to FULL station
        } else if ( halfSensor.isPresent() && !fullSensor.isPresent() ) {
            buzzer.playEvent(BUZZ_HALF);
            halMgr1.setActualStation(FLAG_HALF);  // Set to HALF station
        } else {
            halMgr1.invalidateStation();  // Reset position
        }

        // Now we're either on station or need to move to it
        fsmNextState = ( halMgr1.getOrderedStation() == halMgr1.getActualStation() ) ? STATE_ON_STATION : STATE_MOVING_TO_STATION;    // Stick or move to station
    };    

    state.onUpdate = []() {};    
    state.onExit = []() {};
    state.shouldAdvance = []() -> FSMStateID {
        return fsmNextState;
    };    
    fsm.addState(STATE_CALIBRATION, state);
}    

void defineMovingToStationState(FSMController& fsm) {
    static FSMStateID fsmNextState = STATE_NONE;

    FSMState state;
    state.onEnter = []() {
        fsmNextState = STATE_NONE;
        // Move motor UP or DOWN to reach the station
        if ( halMgr1.getActualStation() == halMgr1.getOrderedStation() ) {
            fsmNextState = STATE_ON_STATION;  // Already on station, no need to move
            return;
        }
        FlagStation ordered = halMgr1.getOrderedStation();
        if (ordered == FLAG_HALF) {
            halMgr1.runMotor(CW, 120000, 255, 1500); // Move down to HALF
        } else if (ordered == FLAG_FULL) {
            halMgr1.runMotor(CCW, 120000, 255, 1500); // Move up to FULL
        }
    };    

    state.onUpdate = []() {
        if (!halMgr1.isRunning()) {             // Continue moving until the motor stops
            FlagMoveStatus status = halMgr1.getMoveStatus();
            if ( status == FLAG_ON_STATION ) {
                halMgr1.setActualStation(halMgr1.getOrderedStation());  // Set actual station to ordered
                fsmNextState = STATE_ON_STATION;            // Go to the "on station" state
            } else if (status == FLAG_MOVE_CANCELLED) {     // Probably LID_OPEN
                halMgr1.invalidateStation();                // Reset position
                fsmNextState = STATE_LID_OPEN;              // Go to the "fix it" state
            } else if (status == FLAG_MOVE_TIMEOUT) {
                // buzzer.playEventWait(BUZZ_TIMEOUT);      // Play timeout sound
                halMgr1.invalidateStation();                // Reset position
                fsmNextState = STATE_FAULT_RECOVERY;        // Go to the "fix it" state
            } else if (status == FLAG_MOVE_STALL) {
                // buzzer.playEventWait(BUZZ_STALL);        // Play stall sound
                fsmNextState = STATE_FAULT_RECOVERY;        // Go to the "fix it" state
            }
        }
        if ( ( halMgr1.getOrderedStation() == FLAG_FULL && halMgr1.getMoveStatus() == FLAG_MOVING_DOWN ) ||
             ( halMgr1.getOrderedStation() == FLAG_HALF && halMgr1.getMoveStatus() == FLAG_MOVING_UP )      ) {  // If the ordered station has changed
                halMgr1.stopMotor(FLAG_MOVE_CANCELLED);  // Stop the motor
            halMgr1.invalidateStation();  // Reset position
            fsmNextState = STATE_CALIBRATION;  // Go back to calibration
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
        // buzzer.playEventWait(BUZZ_LID_OPEN);

        if ( halMgr1.isRunning() ) halMgr1.stopMotor( FLAG_MOVE_CANCELLED );  // Stop the motor if it is running
        halMgr1.invalidateStation();  // reset position
        // report lid opening to host
        buzzer.playEvent(BUZZ_STOP);  // Play a sound to indicate lid is open
    };
    state.onUpdate = []() {
        if (lidSensor.isPresent()) {
            fsmNextState = STATE_CALIBRATION;
        }
    };
    state.onExit = []() {
        // report lid closing to host
        // Serial.println("Lid closed – resuming");
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
        // buzzer.playEventWait(BUZZ_FAULT_RECOVERY);

        // Serial.println("ERROR: Entering FIX_ME");
        buzzer.playEvent(BUZZ_FAULT_RECOVERY);  // Play fault recovery sound
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
