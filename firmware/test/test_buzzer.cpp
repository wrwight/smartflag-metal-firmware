/** Abandoned this test - 06/04/2025 - will test within main code */
#include <AUnit.h>
#include "../src/BuzzerManager.h"

// Stub millis() to simulate time
unsigned long mockMillis = 0;
unsigned long millis() {
    return mockMillis;
}

// Override Particle functions for test env
void tone(int pin, int freq) {}
void noTone(int pin) {}
void pinMode(int pin, int mode) {}

test(BuzzerManager_BasicPattern) {
    BuzzerManager buzzer;
    buzzer.begin();
    buzzer.playPattern("123 ");

    // Step through pattern manually
    mockMillis = 0;
    buzzer.update();  // Should start tone for '1'

    mockMillis += 60;
    buzzer.update();  // End tone '1'

    mockMillis += 50;
    buzzer.update();  // Start tone for '2'

    mockMillis += 120;
    buzzer.update();  // End tone '2'

    mockMillis += 50;
    buzzer.update();  // Start tone for '3'

    mockMillis += 180;
    buzzer.update();  // End tone '3'

    mockMillis += 50;
    buzzer.update();  // Start silent ' '

    mockMillis += 60;
    buzzer.update();  // End silent pause

    assertTrue(buzzer.isFinished());
}
