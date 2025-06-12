// BuzzerManager.cpp
#include "BuzzerManager.h"
#include "Particle.h"

extern BuzzerManager buzzer;

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
            playPattern("1212124", "C.D.E.F");
            break;
        case BUZZ_CONNECT:
            // T-Mobile jingle approximation (3 short notes, 1 long, 1 short)
            playPattern("11112", "cccec");
            break;
        case BUZZ_LOST:
            // "This number is disconnected" style: 3 steady tones with pauses
            playPattern("224", "afF");
            break;
        case BUZZ_FLAG_UP:
            // playPattern("111211112", "e.eg.c.ce");
            playPattern("1113", "aceA");
            break;
        case BUZZ_FLAG_DOWN:
            // playPattern("21214", "c.c.f");
            playPattern("1113", "Aeca");
            break;
        case BUZZ_STALL:
            // Nintendo "death jingle" style: descending eerie tones
            playPattern("111111111", "af.ffe.dc");
            break;
        case BUZZ_HALF:
        playPattern("344", "cCc");  // First three notes of "Taps"
            break;
        case BUZZ_FULL:
        playPattern("344", "gGc");  // First three notes of "Colors" trumpet call
            break;
        case BUZZ_STOP:
            // playPattern("11141114114", "CBA.Agf.dcb");  // Three descending sequences
            playPattern("2224", "ccca");  // Three descending sequences
            break;
        case BUZZ_DEBUG_1:
            playPattern("442144", "aa.e..");  // Two lows, then debug count
            break;
        case BUZZ_DEBUG_2:
            playPattern("4421144", "aa.ee..");  // Two lows, then debug count
            break;
        case BUZZ_DEBUG_3:
            playPattern("44211144", "aa.eee..");  // Two lows, then debug count
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
