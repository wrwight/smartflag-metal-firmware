#ifndef HALYARD_MANAGER_H
#define HALYARD_MANAGER_H

#include "Particle.h"
#include "sensors/SensorManager.h"
#include "BuzzerManager.h"

enum Direction {
  CW,  // Clockwise
  CCW  // Counter-clockwise
};

enum FlagStation {
  FLAG_UNKNOWN,
  FLAG_FULL,   // Flag is fully raised
  FLAG_HALF,   // Flag is half raised
};

enum FlagMoveStatus {
  FLAG_MOVE_NONE,      // No movement
  FLAG_MOVING_UP,        // Moving up (CW)
  FLAG_MOVING_DOWN,      // Moving down (CCW)
  FLAG_ON_STATION,   // Moving to station
  FLAG_MOVE_CANCELLED,  // Movement cancelled
  FLAG_MOVE_TIMEOUT,      // Movement timeout
  FLAG_MOVE_STALL      // Stall condition detected
};

class HalyardManager {
private:
  // Motor configuration
  int _dirPin = -1;
  int _pwmPin = -1;
  int _enablePin = -1;
  int _currentSensePin = -1;  // Pin for current sensing
  bool _isRunning = false;
  bool _encoderPresent = false;
  bool _stall = false;  // Stall condition flag
  FlagMoveStatus _moveStatus = FLAG_MOVE_NONE;  // Current move status
  unsigned long _lastUpdateTime = 0;  // Last update time for the motor status
  Direction _lastDirection;

  // Ramp control
  const unsigned long _minRampStartTime = 50;  // Minimum ramp time in ms
  unsigned long _stopTime = 0;  // Time to stop motor, 0 means run indefinitely
  unsigned long _rampStartTime = 0;
  unsigned long _rampDuration = 0;  // Duration of the ramp in ms
  uint8_t _targetSpeed = 255;  // Target speed for the motor
  uint8_t _currentSpeed = 0;  // Current speed during ramping
  bool _rampActive = false;  // Whether ramping is active

  // Station tracking
  FlagStation _ordered = FLAG_FULL;
  FlagStation _actual = FLAG_UNKNOWN;


public:
  // Constructor
  HalyardManager(int dirPin, int pwmPin, int enablePin, int currentSensePin, bool encoder = false)
    : _dirPin(dirPin), _pwmPin(pwmPin), _enablePin(enablePin), _currentSensePin(currentSensePin), _encoderPresent(encoder) {

      invalidateStation();  // Start with unknown station
      _isRunning = false;  // Motor is initially stopped
      _stall = false;  // No stall condition at startup
    }

    // Station accessors
  FlagStation getOrderedStation() const { return _ordered; }
  void setOrderedStation(FlagStation s) { _ordered = s; }

  FlagStation getActualStation() ;

  FlagMoveStatus getMoveStatus() const { return _moveStatus; }

  void setActualStation(FlagStation s) { _actual = s; }

  void invalidateStation() {
    _actual = FLAG_UNKNOWN;
    Serial.println("Flag position invalidated.");
  }

  void begin();
  // Motor control
  void runMotor(Direction dir, unsigned long durationMs, uint8_t targetSpeed, unsigned long rampTimeMs);
  void update();
  void stopMotor( FlagMoveStatus status = FLAG_MOVE_NONE);
  bool isRunning() const { return _isRunning; };
  bool stallDetected() const { return _stall; };
  void clearStall() { _stall = false; };
  bool lowering() const { return _lastDirection == CW; };
};

#endif
