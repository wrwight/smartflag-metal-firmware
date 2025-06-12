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
  bool isPresent = markerPresent();
  bool justArrived = !_wasPresent && isPresent;  // Just arrived if was not present before
  _wasPresent = isPresent;  // Update state for next call
  return justArrived;
}

bool SensorManager::markerJustDeparted() {
  bool isPresent = markerPresent();
  bool justDeparted = _wasPresent && !isPresent;  // Just arrived if was not present before
  _wasPresent = isPresent;  // Update state for next call
  return justDeparted;
}
