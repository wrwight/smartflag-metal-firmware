#include "HalyardManager.h"

const char* HalyardManager::_targetPosition = nullptr;
const char* HalyardManager::_currentPosition = nullptr;
float _stallLimitAmps = 1.8;
bool _stall = false;
const int _currentSensePin = A4;
const int HalyardManager::ENABLE_MOTOR;
#define CURRENT_SENSOR_SCALE 2.0  // Example: 5A per volt

int HalyardManager::_dirPin = -1;
int HalyardManager::_pwmPin = -1;
bool HalyardManager::_isRunning = false;
unsigned long HalyardManager::_stopTime = 0;

// ramp speed control variables
uint8_t HalyardManager::_targetSpeed = 255;
uint8_t HalyardManager::_currentSpeed = 0;
unsigned long HalyardManager::_rampStartTime = 0;
unsigned long HalyardManager::_rampDuration = 0;
bool HalyardManager::_rampActive = false;

void HalyardManager::initialize(int dirPin, int pwmPin) {
  _dirPin = dirPin;
  _pwmPin = pwmPin;

  pinMode(_dirPin, OUTPUT);
  pinMode(_pwmPin, OUTPUT);
  pinMode(ENABLE_MOTOR, OUTPUT);
  
  digitalWrite(_dirPin, LOW);
  analogWrite(_pwmPin, 0);
  digitalWrite(ENABLE_MOTOR, LOW);  // motor disabled by default
}

void HalyardManager::runMotor(Direction dir, unsigned long durationMs, uint8_t targetSpeed, unsigned long rampTimeMs) {
  if (_dirPin == -1 || _pwmPin == -1) return;

  digitalWrite(_dirPin, (dir == CW) ? HIGH : LOW);
  _targetSpeed = targetSpeed;
  _currentSpeed = 0;
  _rampStartTime = millis();
  _rampDuration = rampTimeMs;
  _rampActive = (rampTimeMs > 0);

  analogWrite(_pwmPin, _currentSpeed);  // Start at 0
  digitalWrite(ENABLE_MOTOR, HIGH);

  _stopTime = millis() + durationMs;
  _isRunning = true;
}

void HalyardManager::update() {
if (_isRunning) {
  
  // 1. Stall check first
  float voltage = analogRead(_currentSensePin) * (3.3 / 4095.0);
  float amps = voltage * CURRENT_SENSOR_SCALE;

  if (amps >= _stallLimitAmps) {
    _stall = true;
    stopMotor();
    Log.error("STALL detected: %.2f A", amps);
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

void HalyardManager::stopMotor() {
  digitalWrite(ENABLE_MOTOR, LOW);
  analogWrite(_pwmPin, 0);
  _isRunning = false;
}

bool HalyardManager::isRunning() {
  return _isRunning;
}

void HalyardManager::setTargetPosition(const char* pos) {
  _targetPosition = pos;
}

const char* HalyardManager::getTargetPosition() {
  return _targetPosition;
}

void HalyardManager::confirmArrival() {
  if (_targetPosition != nullptr) {
    _currentPosition = _targetPosition;
    _targetPosition = nullptr;
  }
}

const char* HalyardManager::getLastKnownPosition() {
  return _currentPosition;
}

void HalyardManager::handleSensorTriggered() {
  if (_isRunning) {
    stopMotor();
    confirmArrival();
    Log.info("Sensor confirmed halyard at %s", _currentPosition);
  }
}

void HalyardManager::setStallAmpsThreshold(float amps) {
  _stallLimitAmps = amps;
}

float HalyardManager::getStallAmpsThreshold() {
  return _stallLimitAmps;
}

bool HalyardManager::stallDetected() {
  return _stall;
}

void HalyardManager::clearStall() {
  _stall = false;
}
