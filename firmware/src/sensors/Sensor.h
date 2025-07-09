#ifndef SENSOR_H
#define SENSOR_H

#include "Particle.h"

class Sensor {
private:
  int _sensorPin = -1;
  bool _presentIf = LOW;  // Marker is present if pin reads LOW by default

public:
  // Constructor with optional presentIf argument
  Sensor(int sensorPin, bool presentIf = LOW)
    : _sensorPin(sensorPin), _presentIf(presentIf) {
      pinMode(_sensorPin, INPUT);
  }

  // Returns true if sensor reads present
  bool isPresent() const {
    return (digitalRead(_sensorPin) == _presentIf);
  }

  // Optional: expose details
  int getPin() const { return _sensorPin; }
  bool getPresentIf() const { return _presentIf; }
};

#endif
