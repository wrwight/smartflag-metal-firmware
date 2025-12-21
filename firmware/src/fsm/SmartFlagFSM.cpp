#include "SmartFlagFSM.h"
#include "FaultManager.h"
#include "sensors/Sensor.h"
#include "FlagUtils.h"

// External references

extern HalyardManager halMgr1;
extern BuzzerManager buzzer;

// extern EventManager eventManager;
// extern Diagnostics diagnostics;

extern Sensor lidSensor;        // Lid sensor 
extern Sensor halfSensor;       // Half marker 
extern Sensor fullSensor;       // Full marker 

// ==== Current Study globals for FSM ====
static float currentSamples[200];
static int currentSampleIndex = 0;
static Timer* currentStudyTimer = nullptr;

// Configurable flag (assume you load this from EEPROM/config system)
extern bool CRS;  

// Helper: sample smoothed amps
void recordCurrentSample() {
    if (currentSampleIndex < 200) {
        currentSamples[currentSampleIndex++] = halMgr1.getSmoothedAmps();
    }
}

// Helper: initiate current study
void startCurrentStudy() {
    // Reset study array
    for (int i = 0; i < 200; i++) {
        currentSamples[i] = 0.0f;
    }
    currentSampleIndex = 0;

    // Create timer if not already
    if (!currentStudyTimer) {
        int timerDuration = 250; // change to 1000 for production
        currentStudyTimer = new Timer(timerDuration, recordCurrentSample); // 1 second
    }
    currentStudyTimer->start();
}

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
        checkAndReportStatus( true , "ONS");   // Periodic status report

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

/**
 * Calibration state: Determine current position using sensors
 * If sensors are ambiguous, set position to UNKNOWN
 * 
 * Initiated when:
 * - System startup
 * - Lid is opened and then closed
 * - Ordered station changes while moving
 * - Fault recovery timeout
 */
void defineCalibrationState(FSMController& fsm) {
    static FSMStateID fsmNextState = STATE_NONE;
    
    FSMState state;
    state.onEnter = []() {

        // Ensure a valid Ordered station is set, or default to FULL
        checkAndReportStatus( true , "CAL");   // Periodic status report


        if ( halMgr1.getOrderedStation() == FLAG_STOP ) {
            halMgr1.setActualStation(FLAG_STOP); // Immediate effect
            if ( halMgr1.isRunning() ) halMgr1.stopMotor( FLAG_MOVE_CANCELLED );  // Stop the motor if it is running
            fsmNextState = STATE_ON_STATION;  // Go to ON_STATION state
            return;
        } else {
            if ( halMgr1.getOrderedStation() != FLAG_FULL && halMgr1.getOrderedStation() != FLAG_HALF ) {

                halMgr1.setOrderedStation(FLAG_FULL);  // Default to FULL station
            }
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
            checkAndReportStatus( true , "MOV");   // Periodic status report
            startCurrentStudy();  // Start current study if CRS is enabled
            halMgr1.runMotor(CW, 120000, 255, 1500); // Move down to HALF
            
        } else if (ordered == FLAG_FULL) {
            checkAndReportStatus( true , "MOV");   // Periodic status report
            startCurrentStudy();  // Start current study if CRS is enabled
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

    // state.onExit = []() {};
 
    state.onExit = []() {
        if (currentStudyTimer) {
            currentStudyTimer->stop();
        }

        int duration = currentSampleIndex; // seconds == # of samples
        if (duration == 0) return;

        // Calculate stats
        int count = 0;
        double sum = 0;
        float maxVal = 0;
        int maxIndex = -1;

        for (int i = 0; i < duration; i++) {
            float v = currentSamples[i];
            if (v > 0.0f) {
                count++;
                sum += v;
            }
            if (v > maxVal) {
                maxVal = v;
                maxIndex = i;
            }
        }

        double avg = (count > 0) ? sum / count : 0;

        // Collect up to 4 preceding values before max
        String preceding = "";
        if (maxIndex >= 0) {
            int start = max(0, maxIndex - 3);
            for (int i = start; i <= maxIndex; i++) {
                preceding += String(currentSamples[i], 2);
                if (i < maxIndex) preceding += ",";
            }
        }

        // Publish if enabled
        // if (CRS) {
            Particle.publish("CurrentStudy",
                String::format("Dur:%d Avg:%.2f Cnt:%d Max:%.2f Seq:%s",
                            duration, avg, count, maxVal, preceding.c_str()),
                PRIVATE);
        // }
    };

    state.shouldAdvance = []() -> FSMStateID {
        return fsmNextState;
    };    
    fsm.addState(STATE_MOVING_TO_STATION, state);
}

void defineLidOpenState(FSMController& fsm) {
    static FSMStateID fsmNextState = STATE_NONE;
    static unsigned long lidClosedStart = 0;
    static unsigned long lidClosedBeep = 0;

    FSMState state;
    state.onEnter = []() {
        fsmNextState = STATE_NONE;
        // buzzer.playEventWait(BUZZ_LID_OPEN);

        if ( halMgr1.isRunning() ) halMgr1.stopMotor( FLAG_MOVE_CANCELLED );  // Stop the motor if it is running
        halMgr1.invalidateStation();  // reset position
        // report lid opening to host
        buzzer.playEvent(BUZZ_STOP);  // Play a sound to indicate lid is open
        checkAndReportStatus( true , "LID");   // Periodic status report

    };

    state.onUpdate = []() {

    /*      Implemented 10s of beeps before activating unit after lid closeure.
     *      If lid re-opens, reset the timer.
     */
    if (lidSensor.isPresent()) {
        if (lidClosedStart == 0) {
            lidClosedStart = millis();  // start timing
            lidClosedBeep = millis(); // 1s beep
            buzzer.playEvent(BUZZ_LID_START);  // Play a sound to indicate lid is closed
        } else if ( (millis() - lidClosedStart) >= 10000) {
            lidClosedStart = 0;  // reset if lid opens again
            fsmNextState = STATE_CALIBRATION;  // 10s elapsed
        } else if ( (millis() - lidClosedStart) >= 9000) {
            buzzer.playEvent(BUZZ_LID_END);  // Play a sound to indicate unit is activating
        } else if ( (millis() - lidClosedBeep) >= 1000) {
            lidClosedBeep += 1000; // increment for next beep time
            if ( (millis() - lidClosedStart) < 7000) {
                buzzer.playEvent(BUZZ_HIGHTICK);  // Play tick - count off 10 seconds
            } else {
                buzzer.playEvent(BUZZ_HIGHTICK2);  // Play double-tick - getting close
            }
        }
    } else {
        lidClosedStart = 0;  // reset if lid opens again
        lidClosedBeep = 0;  // reset if lid opens again
    }
};

    state.onExit = []() {};
    state.shouldAdvance = []() -> FSMStateID { return fsmNextState; };
    fsm.addState(STATE_LID_OPEN, state);
}

void defineFaultRecoveryState(FSMController& fsm) {
    static FSMStateID fsmNextState = STATE_NONE;
    static unsigned long _fixMeStartTime = 0; // Start time for fault recovery

    FSMState state;
    state.onEnter = []() {
        fsmNextState = STATE_NONE;
        buzzer.playEvent(BUZZ_FAULT_RECOVERY);  // Play fault recovery sound
        _fixMeStartTime = millis();  // Record the start time for the fault recovery    
        checkAndReportStatus( true , "FLT");   // Periodic status report

    };
    
    state.onUpdate = []() {
        if ( ( millis() - _fixMeStartTime ) > 900000UL ) { // 15 min timeout
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

    // Process queued events
    FSMEvent evt = nextEvent();
    while (evt != EVENT_NONE) {
        // Option 1: direct override for urgent events
        if (evt == EVENT_LID_OPEN) {
            if (state.onExit) state.onExit();
            _current = STATE_LID_OPEN;
            if (_states[_current].onEnter) _states[_current].onEnter();
            return;
        } else if ( evt == EVENT_CLEAR_FAULT ) {
            if (state.onExit) state.onExit();
            _current = STATE_CALIBRATION;
            if (_states[_current].onEnter) _states[_current].onEnter();
            return;
        }
        // // Option 2: let state handle events
        // if (state.onEvent) state.onEvent(evt);  // you can add onEvent handler to FSMState
        evt = nextEvent();
    }

    

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

void FSMController::enqueueEvent(FSMEvent evt) {
    int nextHead = (head + 1) % MAX_FSM_EVENTS;
    if (nextHead != tail) {   // queue not full
        eventQueue[head] = evt;
        head = nextHead;
    } else {
        buzzer.playEvent(BUZZ_HIGHTICK2); // Queue full, play error sound
    }
}

FSMEvent FSMController::nextEvent() {
    if (head == tail) {
        return EVENT_NONE;  // queue empty
    }
    FSMEvent evt = eventQueue[tail];
    tail = (tail + 1) % MAX_FSM_EVENTS;
    return evt;
}
