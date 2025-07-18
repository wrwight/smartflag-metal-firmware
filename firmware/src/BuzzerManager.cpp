// BuzzerManager.cpp
#include "BuzzerManager.h"
#include "Particle.h"

extern BuzzerManager buzzer;

// String militarySongs[] = {
//     "2114", "e.ggfed",  // USAF "Off we go..."
//     "2114", "e.gg",  // USAF "Off we go..."
// };
// Function to handle Particle function call for playing tones
// Expects JSON input like: {"p":"1234", "f":"abcd"}
int PlayTones(String json) {
    JSONValue root = JSONValue::parseCopy(json);
    if (!root.isObject()) {
        return -1; // Invalid JSON format
    }
    JSONObjectIterator iter(root);

    String pattern = "";
    String freqs = "";

    while (iter.next()) {
        if (iter.name() == "p") {
            pattern = String(iter.value().toString());
        } else if (iter.name() == "f") {
            freqs = String(iter.value().toString());
        }
    }

    if (pattern.length() == 0 || freqs.length() == 0) {
        return -2; // Missing or invalid keys
    }

    buzzer.playPattern(pattern, freqs);
    return 0;
}


void BuzzerManager::begin() {
    pinMode(buzzerPin, OUTPUT);
    noTone(buzzerPin);
    Particle.function("PlayTones", PlayTones);
}

void BuzzerManager::playPattern(const String& p, const String& f) {
    pattern = p;
    freqPattern = f;
    index = 0;
    lastChange = millis();
    isOn = false;
    isSilent = false;
}

void BuzzerManager::playEvent(BuzzerEvent event) {
    switch (event) {
        case BUZZ_POWER_ON:
            playPattern("2114", "e.gg");        // USAF "Off we go..."
            break;
        case BUZZ_CONNECT:
            playPattern("11112", "cccec");      // T-Mobile jingle approximation
            break;
        case BUZZ_LOST:
            playPattern("224", "afF");          // Old wired phone disconnection tone
            break;
        case BUZZ_FLAG_UP:
            playPattern("1111134", "g.g.g.C");  // triple dit - HIGH
            break;
        case BUZZ_FLAG_DOWN:
            playPattern("1111134", "g.g.g.c");  // triple dit - LOW
            break;
        case BUZZ_STALL:
            playPattern("111111111", "af.ffe.dc");  // Nintendo "death jingle" style
            break;
        case BUZZ_TIMEOUT:
            playPattern("1313141444", "F.C.F.C..c");  // Tic-toc...
            break;
        case BUZZ_HALF:
            playPattern("11134", "g.g.c");  // dit-dit, LOW
        break;
        case BUZZ_FULL:
            playPattern("11134", "g.g.C");  // dit-dit, HIGH
            break;
        case BUZZ_STOP:
            playPattern("2224", "ccca");  // Three descending sequences
            break;
        case BUZZ_STARTUP:  // Startup sound
            playPattern("44114", "cc.B.");  // Low, then one high note
            break;
        case BUZZ_ON_STATION:  // Startup sound
            playPattern("441114", "cc.BB.");  // Low, then two high notes
            break;
        case BUZZ_CALIB:  // Calibration sound
            playPattern("4411114", "cc.BBB.");  // Low, then three high notes
            break;
        case BUZZ_MOVING_TO_STATION:  // Sound when moving to station
            playPattern("44111114", "cc.BBBB.");  // Low, then four high notes
            break;
        case BUZZ_LID_OPEN:  // Sound when lid is open
            playPattern("441111114", "cc.BBBBB.");  // Low, then five high notes
            break;
        case BUZZ_FAULT_RECOVERY:  // Sound for fault recovery
            playPattern("4411111114", "cc.BBBBBB.");  // Low, then six high notes
            break;
        case BUZZ_DEBUG_1:
            // playPattern("442144", "aa.e..");  // Two lows, then debug count
            playPattern("1", "c");  // Two lows, then debug count
            break;
            case BUZZ_DEBUG_2:
            // playPattern("4421144", "aa.ee..");  // Two lows, then debug count
            playPattern("1", "f");  // Two lows, then debug count
            break;
            case BUZZ_DEBUG_3:
            // playPattern("44211144", "aa.eee..");  // Two lows, then debug count
            playPattern("1", "C");  // Two lows, then debug count
            break;
        case BUZZ_DEBUG_4:  
            playPattern("442111144", "aa.eeee..");  // Two lows, then debug count
            break;
        case BUZZ_HIGHTICK:
            // High-pitched tick sound for timing
            playPattern("1", "A");  // Single high note
            break;
        case BUZZ_SILENT_1S:
            playPattern("4444", "....");  // No sound, just silence
            break;
    }
}

void BuzzerManager::playEventWait(BuzzerEvent event) {
    playEvent(event);
    while (!isFinished()) {
        update();
        Particle.process();  // Ensure cloud background tasks
    }
}

void BuzzerManager::update() {
    if (pattern.length() == 0 || index >= pattern.length()) return;

    unsigned long now = millis();
    char currentSymbol = pattern.charAt(index);
    unsigned int duration = getDuration(currentSymbol);

    if (!isOn && !isSilent) {
        char freqChar = (index < freqPattern.length()) ? freqPattern.charAt(index) : '\0';
        int freq = getFrequency(freqChar);
        if (freq > 0) {
            tone(buzzerPin, freq);
            isOn = true;
        } else {
            noTone(buzzerPin);
            isSilent = true;
        }
        lastChange = now;
    }

    if ((isOn || isSilent) && now - lastChange >= duration) {
        noTone(buzzerPin);
        isOn = false;
        isSilent = false;
        index++;
        lastChange = now;
    }
}

bool BuzzerManager::isFinished() {
    return index >= pattern.length();
}

unsigned int BuzzerManager::getDuration(char symbol) {
    for (int i = 0; i < 5; i++) {
        if (symbol == symbols[i]) return durations[i];
    }
    return 0;
}

bool BuzzerManager::isAudible(char symbol) {
    return symbol != ' ';
}

int BuzzerManager::getFrequency(char freqChar) {
    switch (freqChar) {
        case 'a': return 440;
        case 'b': return 494;
        case 'c': return 523;
        case 'd': return 587;
        case 'e': return 659;
        case 'f': return 698;
        case 'g': return 784;
        case 'A': return 880;
        case 'B': return 988;
        case 'C': return 1047;
        case 'D': return 1175;
        case 'E': return 1319;
        case 'F': return 1397;
        case 'G': return 1568;
        case '.': case '_': return 0;
        default: return 2000;
    }
}
