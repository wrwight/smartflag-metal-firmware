#pragma once

#include "Particle.h"
#include "HalyardManager.h"

enum FaultType {
    FAULT_NONE,
    FAULT_TIMEOUT,
    FAULT_STALL,
    FAULT_UNKNOWN
};

struct FaultContext {
    FaultType type = FAULT_NONE;
    Direction dir = CCW;
    FlagStation target = FLAG_UNKNOWN;
    uint8_t retries = 0;
    time_t timestamp = 0;
};

class FaultManager {
  public:
    void triggerFault(FaultType type, Direction dir, FlagStation target);
    void clearFault();
    bool hasFault() const;
    const FaultContext& getFaultContext() const;
    void incrementRetry();
    uint8_t getRetryCount() const;

  private:
    FaultContext current;
};
