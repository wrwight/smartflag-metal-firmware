#include "HalyardManager.h"
#include "BuzzerManager.h"
#include "Sensor.h"
#include "EEPROMManager.h"
#include "FlagUtils.h"

#define CURRENT_SENSOR_SCALE 2.0

extern BuzzerManager buzzer;
extern Sensor lidSensor;
extern Sensor halfSensor;
extern Sensor fullSensor;
extern HalyardManager halMgr1;

void HalyardManager::begin() {
    pinMode(_dirPin,          OUTPUT);
    pinMode(_pwmPin,          OUTPUT);
    pinMode(_enablePin,       OUTPUT);
    pinMode(_currentSensePin, INPUT);

    digitalWrite(_dirPin,    LOW);
    analogWrite (_pwmPin,    LOW);
    digitalWrite(_enablePin, LOW);

    invalidateStation();
    _isRunning = false;
    _stall     = false;
    _forced    = FLAG_UNKNOWN;
}

void HalyardManager::runMotor(Direction dir, unsigned long durationMs,
                uint8_t targetSpeed, unsigned long rampTimeMs,
                FlagStation departureStation) {

    if (_dirPin == -1 || _pwmPin == -1 || _enablePin == -1) {
        _moveStatus = FLAG_MOVE_NONE;
        return;
    }

    buzzer.playEventWait(dir == CW ? BUZZ_FLAG_DOWN : BUZZ_FLAG_UP);

    // Capture departure station for move-start report
    _departureStation = departureStation;  // use passed value instead of getActualStation()

    digitalWrite(_dirPin, (dir == CW) ? HIGH : LOW);
    _lastDirection = dir;
    _moveStatus    = (dir == CW) ? FLAG_MOVING_DOWN : FLAG_MOVING_UP;

    _targetSpeed   = targetSpeed;
    _rampStartTime = millis();
    _rampDuration  = (rampTimeMs < _minRampStartTime) ? _minRampStartTime : rampTimeMs;
    _rampActive    = true;
    _currentSpeed  = 0;

    analogWrite (_pwmPin,    _currentSpeed);
    digitalWrite(_enablePin, HIGH);

    _stopTime  = (durationMs == 0) ? 0 : millis() + durationMs;
    _isRunning = true;
    _stall     = false;

    // Notify FlagUtils: move is starting
    reportMoveStart(_departureStation, getOrderedStation());
}

void HalyardManager::applyConfigExtToRuntime() {
    ConfigExt x;
    readConfigExt(x);
    if (x.magic != CFGX_MAGIC || x.version != CFGX_VERSION) {
        initConfigExt();
        readConfigExt(x);
    }
    setStallLimitMa(x.stall_limit_ma);
    setMoveTimeoutSec(x.move_timeout_sec);
    SFDBG::pub("CFGX", String::format("applied SLM=%u TMO=%u",
               (unsigned)x.stall_limit_ma, (unsigned)x.move_timeout_sec));
}

void HalyardManager::setForcedStation(FlagStation s, unsigned long expiration) {
    _forced          = s;
    _forcedBeginning = millis();
    _forcedDuration  = expiration;
}

void HalyardManager::update() {

    // Expire forced station override
    if (_forced != FLAG_UNKNOWN &&
        millis() >= _forcedBeginning + _forcedDuration) {
        _forced          = FLAG_UNKNOWN;
        _forcedBeginning = 0;
        _forcedDuration  = 0;
    }

    if (_isRunning) {

        // 1. Stall check first
        if (getSmoothedAmps() >= _stallLimitAmps) {
            _stall = true;
            stopMotor(FLAG_MOVE_STALL);
            buzzer.playEventWait(BUZZ_STALL);
            return;
        }

        // 2. PWM ramp
        if (_rampActive) {
            unsigned long elapsed = millis() - _rampStartTime;
            if (elapsed >= _rampDuration) {
                _currentSpeed = _targetSpeed;
                _rampActive   = false;
            } else {
                float progress = (float)elapsed / (float)_rampDuration;
                _currentSpeed  = (uint8_t)(_targetSpeed * progress);
            }
            analogWrite(_pwmPin, _currentSpeed);
        }

        // 3. Time-based stop
        if (_stopTime > 0 && millis() >= _stopTime) {
            stopMotor(FLAG_MOVE_TIMEOUT);
            buzzer.playEventWait(BUZZ_STOP);
        }

        // 4. Marker arrival check
        if (lowering()) {
            if (halfSensor.isPresent()) {
                setActualStation(FLAG_HALF);
                stopMotor(FLAG_ON_STATION);
                buzzer.playEventWait(BUZZ_HALF);
            }
        } else {
            if (fullSensor.isPresent()) {
                setActualStation(FLAG_FULL);
                stopMotor(FLAG_ON_STATION);
                buzzer.playEventWait(BUZZ_FULL);
            }
        }
    }
}

float HalyardManager::getInputVoltage() {
    return ADC_TO_VOLT(analogRead(INPUT_VOLT_SENSE_PIN)) * INPUT_VOLT_SCALE_FAC;
}

void HalyardManager::stopMotor(FlagMoveStatus status) {
    digitalWrite(_enablePin, LOW);
    analogWrite (_pwmPin,    0);
    _isRunning  = false;
    _moveStatus = status;
    _rampActive = false;

    // Notify FlagUtils: move has ended
    reportMoveEnd(status, getActualStation());
}

void HalyardManager::setOrderedStation(FlagStation s) {
    _ordered = s;
    saveOSTA(_ordered);
}

FlagStation HalyardManager::getActualStation() {
    if (_actual == FLAG_STOP) return _actual;

    if ((_actual == FLAG_HALF && !halfSensor.isPresent()) ||
        (_actual == FLAG_FULL && !fullSensor.isPresent())) {
        setActualStation(FLAG_UNKNOWN);
    }
    return _actual;
}

float HalyardManager::getMotorCurrent() {
    return ADC_TO_VOLT(analogRead(MOTOR_CURRENT_PIN)) * MOTOR_VOLT_TO_AMP_FAC;
}

void HalyardManager::updateSmoothedAmps() {
    double inst = getMotorCurrent();
    if (_smoothedAmps <= 0.0) {
        _smoothedAmps = inst;
    } else {
        _smoothedAmps = _smoothedAmps * (1.0 - _alpha) + inst * _alpha;
    }

    // Feed move current stats while motor is running
    if (_isRunning) {
        updateMoveCurrentStats((float)_smoothedAmps);
    }
}

float HalyardManager::getSmoothedAmps() {
    return _smoothedAmps;
}
