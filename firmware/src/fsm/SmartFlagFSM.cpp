#include "SmartFlagFSM.h"
#include "FaultManager.h"
#include "sensors/Sensor.h"
#include "FlagUtils.h"

extern HalyardManager halMgr1;
extern BuzzerManager  buzzer;
extern Sensor lidSensor;
extern Sensor halfSensor;
extern Sensor fullSensor;

// -----------------------------------------------------------------------
// NOTE: Status reporting has been moved out of the FSM entirely.
// Move-start and move-end reports are triggered directly by
// HalyardManager::runMotor() and HalyardManager::stopMotor().
// Periodic heartbeat reports are driven by checkAndReportStatus(false,...)
// called in loop(). FSM states no longer call checkAndReportStatus().
// -----------------------------------------------------------------------

void defineStartupState(FSMController& fsm) {
    static FSMStateID fsmNextState = STATE_NONE;

    FSMState state;
    state.onEnter = []() {
        fsmNextState = STATE_NONE;
    };
    state.onUpdate = []() {
        fsmNextState = STATE_CALIBRATION;
    };
    state.onExit        = []() {};
    state.shouldAdvance = []() -> FSMStateID { return fsmNextState; };
    fsm.addState(STATE_STARTUP, state);
}

void defineOnStationState(FSMController& fsm) {
    static FSMStateID fsmNextState = STATE_NONE;

    FSMState state;
    state.onEnter = []() {
        fsmNextState = STATE_NONE;
        // No status publish here — move-end report from stopMotor() covers
        // the arrival event. Periodic heartbeat covers ongoing health.
    };
    state.onUpdate = []() {
        FlagStation ordered = halMgr1.getOrderedStation();
        FlagStation actual  = halMgr1.getActualStation();
        if (actual != ordered) {
            fsmNextState = STATE_MOVING_TO_STATION;
        }
    };
    state.onExit        = []() {};
    state.shouldAdvance = []() -> FSMStateID { return fsmNextState; };
    fsm.addState(STATE_ON_STATION, state);
}

void defineCalibrationState(FSMController& fsm) {
    static FSMStateID fsmNextState = STATE_NONE;

    FSMState state;
    state.onEnter = []() {
        fsmNextState = STATE_NONE;

        if (halMgr1.getOrderedStation() == FLAG_STOP) {
            halMgr1.setActualStation(FLAG_STOP);
            if (halMgr1.isRunning()) halMgr1.stopMotor(FLAG_MOVE_CANCELLED);
            fsmNextState = STATE_ON_STATION;
            return;
        } else {
            if (halMgr1.getOrderedStation() != FLAG_FULL &&
                halMgr1.getOrderedStation() != FLAG_HALF) {
                halMgr1.setOrderedStation(FLAG_FULL);
            }
        }

        if (fullSensor.isPresent() && !halfSensor.isPresent()) {
            buzzer.playEvent(BUZZ_FULL);
            halMgr1.setActualStation(FLAG_FULL);
        } else if (halfSensor.isPresent() && !fullSensor.isPresent()) {
            buzzer.playEvent(BUZZ_HALF);
            halMgr1.setActualStation(FLAG_HALF);
        } else {
            halMgr1.invalidateStation();
        }

        fsmNextState = (halMgr1.getOrderedStation() == halMgr1.getActualStation())
                       ? STATE_ON_STATION
                       : STATE_MOVING_TO_STATION;
    };
    state.onUpdate      = []() {};
    state.onExit        = []() {};
    state.shouldAdvance = []() -> FSMStateID { return fsmNextState; };
    fsm.addState(STATE_CALIBRATION, state);
}

void defineMovingToStationState(FSMController& fsm) {
    static FSMStateID fsmNextState = STATE_NONE;

    FSMState state;

    state.onEnter = []() {
        fsmNextState = STATE_NONE;

        if (halMgr1.getActualStation() == halMgr1.getOrderedStation()) {
            fsmNextState = STATE_ON_STATION;
            return;
        }

        FlagStation departure = halMgr1.getActualStation();  // capture BEFORE motor starts
        FlagStation ordered = halMgr1.getOrderedStation();

        if (ordered == FLAG_HALF) {
            halMgr1.runMotor(CW, halMgr1.getMoveTimeoutSec() * 1000, 255, 1500, departure);
        } else if (ordered == FLAG_FULL) {
            halMgr1.runMotor(CCW, halMgr1.getMoveTimeoutSec() * 1000, 255, 1500, departure);
        }
    };


    state.onUpdate = []() {
        if (!halMgr1.isRunning()) {
            FlagMoveStatus status = halMgr1.getMoveStatus();
            // stopMotor() already called reportMoveEnd() — just advance FSM
            if (status == FLAG_ON_STATION) {
                halMgr1.setActualStation(halMgr1.getOrderedStation());
                fsmNextState = STATE_ON_STATION;
            } else if (status == FLAG_MOVE_CANCELLED) {
                halMgr1.invalidateStation();
                fsmNextState = STATE_LID_OPEN;
            } else if (status == FLAG_MOVE_TIMEOUT) {
                halMgr1.invalidateStation();
                fsmNextState = STATE_FAULT_RECOVERY;
            } else if (status == FLAG_MOVE_STALL) {
                fsmNextState = STATE_FAULT_RECOVERY;
            }
        }

        // Ordered station reversed mid-move
        if ((halMgr1.getOrderedStation() == FLAG_FULL && halMgr1.getMoveStatus() == FLAG_MOVING_DOWN) ||
            (halMgr1.getOrderedStation() == FLAG_HALF && halMgr1.getMoveStatus() == FLAG_MOVING_UP)) {
            halMgr1.stopMotor(FLAG_MOVE_CANCELLED);
            halMgr1.invalidateStation();
            fsmNextState = STATE_CALIBRATION;
        }
    };

    // CurrentStudy removed — onExit has nothing to do
    state.onExit        = []() {};
    state.shouldAdvance = []() -> FSMStateID { return fsmNextState; };
    fsm.addState(STATE_MOVING_TO_STATION, state);
}

void defineLidOpenState(FSMController& fsm) {
    static FSMStateID    fsmNextState   = STATE_NONE;
    static unsigned long lidClosedStart = 0;
    static unsigned long lidClosedBeep  = 0;

    FSMState state;
    state.onEnter = []() {
        fsmNextState   = STATE_NONE;
        lidClosedStart = 0;
        lidClosedBeep  = 0;

        if (halMgr1.isRunning()) halMgr1.stopMotor(FLAG_MOVE_CANCELLED);
        halMgr1.invalidateStation();
        buzzer.playEvent(BUZZ_STOP);
        // stopMotor() above fires reportMoveEnd() if a move was in progress.
        // No additional publish needed here.
    };

    state.onUpdate = []() {
        if (lidSensor.isPresent()) {
            if (lidClosedStart == 0) {
                lidClosedStart = millis();
                lidClosedBeep  = millis();
                buzzer.playEvent(BUZZ_LID_START);
            } else if ((millis() - lidClosedStart) >= 10000) {
                lidClosedStart = 0;
                fsmNextState   = STATE_CALIBRATION;
            } else if ((millis() - lidClosedStart) >= 9000) {
                buzzer.playEvent(BUZZ_LID_END);
            } else if ((millis() - lidClosedBeep) >= 1000) {
                lidClosedBeep += 1000;
                if ((millis() - lidClosedStart) < 7000) {
                    buzzer.playEvent(BUZZ_HIGHTICK);
                } else {
                    buzzer.playEvent(BUZZ_HIGHTICK2);
                }
            }
        } else {
            lidClosedStart = 0;
            lidClosedBeep  = 0;
        }
    };

    state.onExit        = []() {};
    state.shouldAdvance = []() -> FSMStateID { return fsmNextState; };
    fsm.addState(STATE_LID_OPEN, state);
}

void defineFaultRecoveryState(FSMController& fsm) {
    static FSMStateID    fsmNextState   = STATE_NONE;
    static unsigned long fixMeStartTime = 0;

    FSMState state;
    state.onEnter = []() {
        fsmNextState   = STATE_NONE;
        fixMeStartTime = millis();
        buzzer.playEvent(BUZZ_FAULT_RECOVERY);
        // Move-end report already fired from stopMotor() — no extra publish needed.
    };
    state.onUpdate = []() {
        if ((millis() - fixMeStartTime) > 900000UL) {  // 15-minute timeout
            fsmNextState = STATE_CALIBRATION;
        }
    };
    state.onExit        = []() {};
    state.shouldAdvance = []() -> FSMStateID { return fsmNextState; };
    fsm.addState(STATE_FAULT_RECOVERY, state);
}

// Unified FSM setup
void setupFSM(FSMController& fsm) {
    defineStartupState(fsm);
    defineOnStationState(fsm);
    defineCalibrationState(fsm);
    defineMovingToStationState(fsm);
    defineLidOpenState(fsm);
    defineFaultRecoveryState(fsm);
}

// ---------------------------------------------------------------------------
// FSMController implementation
// ---------------------------------------------------------------------------

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

    FSMEvent evt = nextEvent();
    while (evt != EVENT_NONE) {
        if (evt == EVENT_LID_OPEN) {
            if (state.onExit) state.onExit();
            _current = STATE_LID_OPEN;
            if (_states[_current].onEnter) _states[_current].onEnter();
            return;
        } else if (evt == EVENT_CLEAR_FAULT) {
            if (state.onExit) state.onExit();
            _current = STATE_CALIBRATION;
            if (_states[_current].onEnter) _states[_current].onEnter();
            return;
        }
        evt = nextEvent();
    }

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

void FSMController::enqueueEvent(FSMEvent evt) {
    int nextHead = (head + 1) % MAX_FSM_EVENTS;
    if (nextHead != tail) {
        eventQueue[head] = evt;
        head = nextHead;
    } else {
        buzzer.playEvent(BUZZ_HIGHTICK2);  // Queue full
    }
}

FSMEvent FSMController::nextEvent() {
    if (head == tail) return EVENT_NONE;
    FSMEvent evt = eventQueue[tail];
    tail = (tail + 1) % MAX_FSM_EVENTS;
    return evt;
}
