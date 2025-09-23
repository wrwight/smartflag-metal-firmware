#include "FaultManager.h"

void FaultManager::triggerFault(FaultType type, Direction dir, FlagStation target) {
    current.type = type;
    current.dir = dir;
    current.target = target;
    current.retries = 0;
    current.timestamp = Time.now();
}

void FaultManager::clearFault() {
    current = FaultContext(); // reset to default
}

bool FaultManager::hasFault() const {
    return current.type != FAULT_NONE;
}

const FaultContext& FaultManager::getFaultContext() const {
    return current;
}

void FaultManager::incrementRetry() {
    current.retries++;
}

uint8_t FaultManager::getRetryCount() const {
    return current.retries;
}
