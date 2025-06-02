#ifndef HALYARD_MANAGER_H
#define HALYARD_MANAGER_H

#include "Particle.h"

enum Direction {
  CW,
  CCW
};




class HalyardManager {
  public:
  static void initialize(int dirPin, int pwmPin);
  static void runMotor(Direction dir, unsigned long durationMs, uint8_t targetSpeed = 255, unsigned long rampTimeMs = 300);
  static void update();  // Call this in loop()
  static void stopMotor();
  static bool isRunning();
  static const int ENABLE_MOTOR = D7;
  
  static void setTargetPosition(const char* pos);
  static const char* getTargetPosition();
  static void confirmArrival();  // call when sensor triggers
  static const char* getLastKnownPosition();
  static void handleSensorTriggered();
  
  // Stall detection
  static void setStallAmpsThreshold(float amps);  // e.g., from config
  static float getStallAmpsThreshold();
  static bool stallDetected();  // query stall condition
  static void clearStall();     // reset stall flag
  
  private:
  static int _dirPin;
  static int _pwmPin;
  static bool _isRunning;
  static unsigned long _stopTime;
  
  static const char* _targetPosition;
  static const char* _currentPosition;

  // ramp speed control
  static uint8_t _targetSpeed;
  static uint8_t _currentSpeed;
  static unsigned long _rampStartTime;
  static unsigned long _rampDuration;
  static bool _rampActive;
};

#endif
