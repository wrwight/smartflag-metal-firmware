#include "SensorManager.h"

int SensorManager::_halyardPin = -1;
int SensorManager::_lidPin = -1;
bool SensorManager::_prevLidState = true;

void SensorManager::initialize(int halyardPin, int lidPin) {
  _halyardPin = halyardPin;
  _lidPin = lidPin;

  pinMode(_halyardPin, INPUT);
  pinMode(_lidPin, INPUT);

  _prevLidState = digitalRead(_lidPin);
}

void SensorManager::update() {
  // Nothing needed yet, but could debounce or record events
}

bool SensorManager::halyardTriggered() {
  return digitalRead(_halyardPin) == HIGH;
}

bool SensorManager::lidOpen() {
  return digitalRead(_lidPin) == LOW;
}

bool SensorManager::lidJustClosed() {
  bool current = digitalRead(_lidPin);
  bool wasOpen = !_prevLidState && current;
  _prevLidState = current;
  return wasOpen;
}

bool SensorManager::lidJustOpened() {
  bool current = digitalRead(_lidPin);
  bool wasClosed = _prevLidState && !current;
  _prevLidState = current;
  return wasClosed;
}
