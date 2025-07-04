#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include "Particle.h"

class SensorManager {
  private:
    int _halyardPin = -1;
    int _lidPin = -1;
    bool _lidWasOpen;
    bool _wasPresent = false;
  
  public:
    // Constructor
    SensorManager(int lidPin, int halyardPin)
      : _halyardPin(halyardPin), _lidPin(lidPin) {
        pinMode(_halyardPin, INPUT);
        pinMode(_lidPin, INPUT);
        _lidWasOpen = lidOpen();   // Initialize previous lid state
        noteMarker();
      }

    // Accessors for testing state
    bool markerPresent() {
      return noteMarker();  // Update state before checking
    }
    
    bool markerAbsent() {
      return !markerPresent();
    }
    
    bool noteMarker() {
      _wasPresent = digitalRead(_halyardPin) == LOW;  // Update state;
      return _wasPresent;
    }

    bool lidClosed() {
      return !lidOpen();  // Lid is closed if it's not open
    }
    bool lidOpen() {
      return digitalRead(_lidPin) == HIGH;
    }

    void update();  // To be called every loop
    bool lidJustClosed();
    bool lidJustOpened();
    bool markerJustArrived();
    bool markerJustDeparted();
    bool markerFlipped();

};

#endif
