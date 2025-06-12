#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include "Particle.h"

class SensorManager {
  private:
    int _halyardPin = -1;
    int _lidPin = -1;
    int _sensorEnablePin = -1;
    bool _lidWasOpen;
    bool _wasPresent = false;
  
  public:
    // Constructor
    SensorManager(int sensor_enab , int lidPin, int halyardPin)
      : _sensorEnablePin(sensor_enab), _halyardPin(halyardPin), _lidPin(lidPin) {
        pinMode(_sensorEnablePin, OUTPUT);
        digitalWrite(_sensorEnablePin, HIGH);  // Enable sensors
        pinMode(_halyardPin, INPUT);
        pinMode(_lidPin, INPUT);
        _lidWasOpen = lidOpen();   // Initialize previous lid state (HIGH means closed)
      }

    // Accessors for testing state
    bool markerPresent() {
      return digitalRead(_halyardPin) == HIGH;
    }

    bool lidClosed() {
      return digitalRead(_lidPin) == HIGH;
    }
    bool lidOpen() {
      return digitalRead(_lidPin) == LOW;
    }

    void update();  // To be called every loop
    bool lidJustClosed();
    bool lidJustOpened();
    bool markerJustArrived() ;
    bool markerJustDeparted() ;

};

#endif
