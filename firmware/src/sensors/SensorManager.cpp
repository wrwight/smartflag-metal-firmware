#include "SensorManager.h"

void SensorManager::update() {
  // Nothing needed yet, but could debounce or record events
}

bool SensorManager::lidJustClosed() {
  bool isClosed = lidClosed();
  bool justClosed = _lidWasOpen && isClosed;
  _lidWasOpen = !isClosed;  // Update state for next call
  return justClosed;
}

bool SensorManager::lidJustOpened() {
  bool isOpen = lidOpen();
  bool justOpened = isOpen && !_lidWasOpen;
  _lidWasOpen = isOpen;  // Update state for next call
  return justOpened;
}

bool SensorManager::markerJustArrived() {
  bool wasPresent = _wasPresent;  // Store previous state
  bool isPresent = markerPresent();
  return isPresent && !wasPresent;  // Just arrived if was not present before but is now
}

bool SensorManager::markerJustDeparted() {
  bool wasPresent = _wasPresent;  // Store previous state
  bool isPresent = markerPresent();
  return !isPresent && wasPresent;  // Just departed if was present before but is not now
}
bool SensorManager::markerFlipped() {
  return markerJustArrived() || markerJustDeparted();
}