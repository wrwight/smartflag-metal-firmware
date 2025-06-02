#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include "Particle.h"

class SensorManager {
public:
  static void initialize(int halyardPin, int lidPin);
  static void update();  // To be called every loop
  static bool halyardTriggered();
  static bool lidOpen();
  static bool lidJustClosed();
  static bool lidJustOpened();

private:
  static int _halyardPin;
  static int _lidPin;
  static bool _prevLidState;
};

#endif
