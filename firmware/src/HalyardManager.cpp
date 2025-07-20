#include "HalyardManager.h"
#include "BuzzerManager.h"
#include "sensors/Sensor.h"

float _stallLimitAmps = 1.8;
#define CURRENT_SENSOR_SCALE 2.0  // Example: 5A per volt

extern BuzzerManager buzzer;
extern Sensor lidSensor;        // Lid sensor 
extern Sensor halfSensor;       // Half marker 
extern Sensor fullSensor;       // Full marker 

void HalyardManager::begin() {
  pinMode(_dirPin, OUTPUT);
  pinMode(_pwmPin, OUTPUT);
  pinMode(_enablePin, OUTPUT);
  pinMode(_currentSensePin, INPUT);

  digitalWrite(_dirPin, LOW);
  analogWrite(_pwmPin, LOW);
  digitalWrite(_enablePin, LOW);

  invalidateStation();  // Start with unknown station
  _isRunning = false;  // Motor is initially stopped
  _stall = false;  // No stall condition at startup
}
void HalyardManager::runMotor(Direction dir, unsigned long durationMs, uint8_t targetSpeed, unsigned long rampTimeMs) {
  
  if (_dirPin == -1 || _pwmPin == -1 || _enablePin == -1 ) {
    _moveStatus = FLAG_MOVE_NONE;  // No motor pins defined, cannot run
    return;
  }

  buzzer.playEventWait(dir == CW ? BUZZ_FLAG_DOWN : BUZZ_FLAG_UP);

  digitalWrite(_dirPin, (dir == CW) ? HIGH : LOW);
  _lastDirection = dir;  // Store last direction for future reference
  _moveStatus = (dir == CW) ? FLAG_MOVING_DOWN : FLAG_MOVING_UP;  // Update move status
  
  // Establish initial ramp parameters
  
  _targetSpeed = targetSpeed;
  _rampStartTime = millis();
  _rampDuration = rampTimeMs < _minRampStartTime ? _minRampStartTime : rampTimeMs;  // Ensure minimum ramp time
  _rampActive = true;
  _currentSpeed = 0 ;  // Start at 0 for ramping (now mandatory - must ramp to avoid initial current spike, which causes stall detection)
  
  analogWrite(_pwmPin, _currentSpeed);  // Start at 0
  digitalWrite(_enablePin, HIGH);

  if ( durationMs == 0 ) {
    _stopTime = 0;  // No stop time, run indefinitely
  } else {
    _stopTime = millis() + durationMs;
  }
  // _stopTime = millis() + durationMs;
  _isRunning = true;
  _stall = false;  // Reset stall condition when starting motor}
}

void HalyardManager::update() {
  if (_isRunning) {
    
    // 1. Stall check first
    float voltage = analogRead(_currentSensePin) * (3.3 / 4095.0);
    float amps = voltage * CURRENT_SENSOR_SCALE;

    if (amps >= _stallLimitAmps) {
      _stall = true;
      stopMotor( FLAG_MOVE_STALL );  // Stop motor on stall condition
      // Log.error("STALL detected: %.2f A", amps);
      buzzer.playEventWait(BUZZ_STALL);
      return;  // Exit early — no further updates needed
    }

    // 2. Ramp PWM (only if not stalled)
    if (_rampActive) {
      unsigned long elapsed = millis() - _rampStartTime;
      if (elapsed >= _rampDuration) {
        _currentSpeed = _targetSpeed;
        _rampActive = false;
      } else {
        float progress = (float)elapsed / (float)_rampDuration;
        _currentSpeed = (uint8_t)(_targetSpeed * progress);
      }
      analogWrite(_pwmPin, _currentSpeed);
    }

    // 3. Time-based stop
    if (_stopTime > 0 && millis() >= _stopTime) {
      stopMotor( FLAG_MOVE_TIMEOUT );  // Stop motor on timeout
      buzzer.playEventWait(BUZZ_STOP);
    }

    // 4. Check marker for Just Arrived
    if ( lowering() ) {
      if ( halfSensor.isPresent() ) { // Flag has arrived at HALF station
        stopMotor( FLAG_ON_STATION );  // Stop motor
        setActualStation(FLAG_HALF);  // Set actual station to HALF
        buzzer.playEventWait(BUZZ_HALF);
      }
    } else if ( fullSensor.isPresent() ) {
        stopMotor( FLAG_ON_STATION);  // Stop motor
        setActualStation(FLAG_FULL);  // Set actual station to FULL
        buzzer.playEventWait(BUZZ_FULL);
    }
  }
}

void HalyardManager::stopMotor( FlagMoveStatus status ) {
  digitalWrite(_enablePin, LOW);
  analogWrite(_pwmPin, 0);
  _isRunning = false;
  _moveStatus = status;  // Update move status to reflect stop
  _rampActive = false;  // Stop any active ramping
}

FlagStation HalyardManager::getActualStation() {  // Return the actual station only if the marker is present
  if ( ( _actual == FLAG_HALF && !halfSensor.isPresent() ) ||
       ( _actual == FLAG_FULL && !fullSensor.isPresent() )    ) {
    setActualStation(FLAG_UNKNOWN);  // If marker is not present, reset actual station
  }
  return _actual;  
}

