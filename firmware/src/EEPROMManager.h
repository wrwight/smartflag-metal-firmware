#ifndef EEPROM_MANAGER_H
#define EEPROM_MANAGER_H

#include "Particle.h"
#include "HalyardManager.h"
#include "FlagUtils.h"

// ====================
// Constants
// ====================
#define EEPROM_MAGIC    0x4733  // 'G3'
#define EEPROM_VERSION  1

// EEPROM layout offsets
#define EEPROM_ADDR_HEADER     0
#define EEPROM_ADDR_CONFIG     16
#define EEPROM_ADDR_STATUS     80
#define EEPROM_ADDR_EVENT_HDR  144
#define EEPROM_ADDR_EVENT_LIST 152

// ====================
// Data Structures
// ====================

struct EEPROMHeader {
    uint16_t magic;         // Identifier
    uint8_t version;        // Structure version
    uint8_t flags;          // Reserved for future
    time32_t lastWriteUTC;  // Timestamp of last EEPROM update
    uint8_t reserved[8];    // Padding/future use
};

// --- ConfigData ---
struct ConfigData {
    char FLG[3];         // upperFlag (2-letter String) // JSON: FLG
    int  FPR;            // upperFlagPrio (integer)     // JSON: FPR
    float LAT;           // SFlat (float)               // JSON: LAT
    float LNG;           // SFlong (float)              // JSON: LNG
    char FED[10];        // jurFederal (up to 9 chars)  // JSON: FED
    char STA[10];        // jurState (up to 9 chars)    // JSON: STA
    char ZIP[6];         // postalCode (up to 5 chars)  // JSON: ZIP
    float STD;           // TZOffset (float)            // JSON: STD
    bool DST;            // doDST (boolean)             // JSON: DST
    char MOD[3];         // modelType (2-letter String) // JSON: MOD
    uint8_t reserved[21];// Padding to make 64 bytes
};

// --- StatusData ---
struct StatusData {
    FlagStation OSTA;      // orderedStation (enum)           // JSON: OSTA
    FlagStation NF01;           // null field 01 (was ASTA)        // JSON: n/a
    char AEID[12];         // activeEventID                   // JSON: AEID
    uint32_t NEXT;         // nextActionTime (epoch)          // JSON: NEXT
    bool EVLD;             // eventValid                      // JSON: EVLD
    char NF02[5];               // null field 02 (was MVST)        // JSON: n/a
    float NF03;                 // null field 03 (was VLTS)        // JSON: n/a
    float NF04;                 // null field 04 (was AMPS)        // JSON: n/a
    uint32_t TIME;         // timestamp last written          // JSON: TIME
    bool NF05;                  // null field 05 (was LID)         // JSON: n/a
    FlagStation NSTA;      // nextStation (enum)              // JSON: NSTA
    uint8_t reserved[21];  // Padding to make 64 bytes
};

// --- Events ---
struct EventHeader {
    uint8_t eventCount;
    uint8_t reserved[7];
};

struct FlagEvent {
    char idv[12];       // Event IDV
    char flg[3];        // Flag abbreviation
    char bmk[20];       // Begin mark
    char emk[20];       // End mark
    bool deleted;       // DEL flag
    uint8_t reserved[9];
};

// ====================
// Core Functions
// ====================
bool validateOrMigrateEEPROM();
void initEEPROM();
bool migrateEEPROM(uint8_t oldVersion);

// ====================
// Wrapper Functions
// ====================
void readConfig(ConfigData &cfg);
void writeConfig(const ConfigData &cfg);

void readStatus(StatusData &status);
void writeStatus(const StatusData &status);

void readEventHeader(EventHeader &hdr);
void writeEventHeader(const EventHeader &hdr);

bool readEvent(uint8_t index, FlagEvent &evt);
bool writeEvent(uint8_t index, const FlagEvent &evt);

// ====================
// JSON Helpers
// ====================
String configToJSON();
bool jsonToConfig(const String &jsonStr);

void saveOSTA (FlagStation osta);

bool jsonToStatus(const String &jsonStr);

// ====================
// Cloud Handlers
// ====================
int setConfigHandler(String data);

#endif
