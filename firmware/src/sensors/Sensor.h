#ifndef SENSOR_H
#define SENSOR_H

#include "Particle.h"

class Sensor {
private:
  int _sensorPin = -1;
  bool _presentIf = LOW;  // Marker is present if pin reads LOW by default
  bool _active = false; // Optional: track if sensor is active

public:
  // Constructor with optional presentIf argument
  Sensor(int sensorPin, bool presentIf = LOW)
    : _sensorPin(sensorPin), _presentIf(presentIf) {
      _active = false; // Initially inactive
  }

  // Set up the sensor pin
  void begin() {
    pinMode(_sensorPin, INPUT);
    _active = true; // Mark sensor as active
  }

  // Returns true if sensor reads present
  bool isPresent() const {
    return (_active && (digitalRead(_sensorPin) == _presentIf));
  }

  // Optional: expose details
  int getPin() const { return _sensorPin; }
  bool getPresentIf() const { return _presentIf; }
};

#endif
