#include "HalyardManager.h"
#include "BuzzerManager.h"

float _stallLimitAmps = 1.8;
#define CURRENT_SENSOR_SCALE 2.0  // Example: 5A per volt

extern BuzzerManager buzzer;

void HalyardManager::runMotor(Direction dir, unsigned long durationMs, uint8_t targetSpeed, unsigned long rampTimeMs) {
  // dumpMotor( dir, durationMs, targetSpeed, rampTimeMs );
  if (_dirPin == -1 || _pwmPin == -1 || _enablePin == -1 ) return;

  buzzer.playEventWait(dir == CW ? BUZZ_FLAG_DOWN : BUZZ_FLAG_UP);

  digitalWrite(_dirPin, (dir == CW) ? HIGH : LOW);

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
      stopMotor();
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
      stopMotor();
    }
  }
}

void HalyardManager::stopMotor() {
  digitalWrite(_enablePin, LOW);
  analogWrite(_pwmPin, 0);
  _isRunning = false;
  buzzer.playEventWait(BUZZ_STOP);
}
