#ifndef HALYARD_MANAGER_H
#define HALYARD_MANAGER_H

#include "Particle.h"
#include "BuzzerManager.h"

// ADC pins
#define SET_PIN(x)                                  ((pin_t)(x))
#define PD15                                    SET_PIN(15)
#define PD16                                    SET_PIN(16)
#define PA3                                     PD16
#define PA4                                     PD15
#define INPUT_VOLT_SENSE_PIN                        PD16
#define MOTOR_CURRENT_PIN                           PD15

// ADC coefs
#define ADC_MAX                                     4095
#define ADC_REF_VOLT                                3.3
#define ADC_TO_VOLT(x)                              ((double)((x) * ADC_REF_VOLT / ADC_MAX))
#define INPUT_VOLT_SCALE_FAC                        11      // V(in) --[100K resistor]--<measure>--[10K resistor]--||| (ground)
#define MOTOR_VOLT_TO_AMP_FAC                       2.0     // Motor current sensing coefs
// #define MOTOR_VOLT_TO_AMP_FAC                       0.661   // Motor current sensing coefs

// Motor direction
enum Direction {
  CW,  // Clockwise  (lowers flag)
  CCW  // Counter-clockwise (raises flag)
};

enum FlagStation {
  FLAG_UNKNOWN,
  FLAG_FULL,
  FLAG_HALF,
  FLAG_STOP
};

enum FlagMoveStatus {
  FLAG_MOVE_NONE,
  FLAG_MOVING_UP,
  FLAG_MOVING_DOWN,
  FLAG_ON_STATION,
  FLAG_MOVE_CANCELLED,
  FLAG_MOVE_TIMEOUT,
  FLAG_MOVE_STALL
};

class HalyardManager {
  private:
    int _dirPin          = -1;
    int _pwmPin          = -1;
    int _enablePin       = -1;
    int _currentSensePin = -1;
    bool _isRunning      = false;
    bool _encoderPresent = false;
    bool _stall          = false;
    FlagMoveStatus _moveStatus    = FLAG_MOVE_NONE;
    unsigned long _lastUpdateTime = 0;
    Direction _lastDirection;

    double _smoothedAmps;
    const double _alpha = 0.2;
    double readInstantAmps();

    // Ramp control
    const unsigned long _minRampStartTime = 50;
    const unsigned long _defRunTime       = 10000;
    unsigned long _stopTime      = 0;
    unsigned long _rampStartTime = 0;
    unsigned long _rampDuration  = 0;
    uint8_t _targetSpeed  = 255;
    uint8_t _currentSpeed = 0;
    bool    _rampActive   = false;

    // Stall and timeout configuration (remotely settable)
    float    _stallLimitAmps = 1.8f;
    uint16_t _moveTimeoutSec = 120;

    // Station tracking
    FlagStation _ordered = FLAG_FULL;
    FlagStation _actual  = FLAG_UNKNOWN;
    FlagStation _forced  = FLAG_UNKNOWN;
    unsigned long _forcedDuration  = 0;
    unsigned long _forcedBeginning = 0;

    // Station captured at move-start, for reporting
    FlagStation _departureStation = FLAG_UNKNOWN;

  public:
    HalyardManager(int dirPin, int pwmPin, int enablePin, int currentSensePin, bool encoder = false)
      : _dirPin(dirPin), _pwmPin(pwmPin), _enablePin(enablePin),
        _currentSensePin(currentSensePin), _encoderPresent(encoder) {
        invalidateStation();
        _isRunning = false;
        _stall     = false;
    }

    // Station accessors
    FlagStation getOrderedStation() const { return _forced != FLAG_UNKNOWN ? _forced : _ordered; }
    void setOrderedStation(FlagStation s);
    FlagStation getActualStation();
    FlagMoveStatus getMoveStatus() const { return _moveStatus; }
    void setForcedStation(FlagStation s, unsigned long expiration);
    void setActualStation(FlagStation s) { _actual = s; }
    void invalidateStation() {
        _actual = FLAG_UNKNOWN;
        Serial.println("Flag position invalidated.");
    }

    // Configuration setters/getters
    void     applyConfigExtToRuntime();
    void     setStallLimitMa(uint16_t ma) { _stallLimitAmps = ((float)ma) / 1000.0f; }
    void     setMoveTimeoutSec(uint16_t sec) { _moveTimeoutSec = sec; }
    float    getStallLimitAmps()  const { return _stallLimitAmps; }
    uint16_t getMoveTimeoutSec()  const { return _moveTimeoutSec; }

    void  begin();
    void  runMotor(Direction dir, unsigned long durationMs, uint8_t targetSpeed, unsigned long rampTimeMs);
    void runMotor(Direction dir, unsigned long durationMs, uint8_t targetSpeed, 
              unsigned long rampTimeMs, FlagStation departureStation = FLAG_UNKNOWN);
    void  update();
    float getMotorCurrent();
    float getSmoothedAmps();
    float getInputVoltage();
    void  updateSmoothedAmps();
    void  stopMotor(FlagMoveStatus status = FLAG_MOVE_NONE);
    bool  isRunning()     const { return _isRunning; }
    bool  stallDetected() const { return _stall; }
    void  clearStall()          { _stall = false; }
    bool  lowering()      const { return _lastDirection == CW; }
};

#endif
