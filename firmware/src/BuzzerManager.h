// BuzzerManager.h
#ifndef BUZZER_MANAGER_H
#define BUZZER_MANAGER_H

#include "Particle.h"

enum BuzzerEvent {
    BUZZ_POWER_ON,
    BUZZ_CONNECT,
    BUZZ_LOST,
    BUZZ_FLAG_UP,
    BUZZ_FLAG_DOWN,
    BUZZ_STALL,
    BUZZ_TIMEOUT,
    BUZZ_HALF,
    BUZZ_FULL,
    BUZZ_STOP,
    BUZZ_STARTUP,  // Startup sound
    BUZZ_ON_STATION,  // Sound when on station
    BUZZ_CALIB,  // Calibration sound
    BUZZ_MOVING_TO_STATION,  // Sound when moving to station
    BUZZ_LID_OPEN,  // Sound when lid is open
    BUZZ_FAULT_RECOVERY,  // Sound for fault recovery    
    BUZZ_DEBUG_1,
    BUZZ_DEBUG_2,
    BUZZ_DEBUG_3,
    BUZZ_DEBUG_4,
    BUZZ_HIGHTICK,
    BUZZ_SILENT_1S
};

class BuzzerManager {
private:
    const int buzzerPin = A0;
    String pattern = "";
    String freqPattern = "";
    unsigned int index = 0;
    unsigned long lastChange = 0;
    bool isOn = false;
    bool isSilent = false;

    const unsigned int durations[5] = { 120, 240, 360, 480, 60 };  // for '1', '2', '3', '4', ' '
    const char symbols[5] = { '1', '2', '3', '4', ' ' };

    unsigned int getDuration(char symbol);
    bool isAudible(char symbol);
    int getFrequency(char freqChar);

public:
    void begin();
    void playPattern(const String& pattern, const String& freqPattern = "");
    void playEvent(BuzzerEvent event);
    void playEventWait(BuzzerEvent event);
    void update();
    bool isFinished();
};

int PlayTones(String json);

#endif
