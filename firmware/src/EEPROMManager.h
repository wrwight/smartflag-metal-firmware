#ifndef EEPROM_MANAGER_H
#define EEPROM_MANAGER_H

#include "Particle.h"
#include "HalyardManager.h"
#include "FlagUtils.h"

// ====================
// Constants
// ====================
#define EEPROM_MAGIC    0x4733  // 'G3'
#define EEPROM_VERSION  3
#define EEPROM_TOTAL_BYTES 2047

#define CFGX_MAGIC   0xC0DE
#define CFGX_VERSION 2
#define EEPROM_ADDR_CFGX (EEPROM_TOTAL_BYTES - 64)   // 1983

// EEPROM layout offsets
#define EEPROM_ADDR_HEADER     0
#define EEPROM_ADDR_CONFIG     16
#define EEPROM_ADDR_STATUS     80
#define EEPROM_ADDR_EVENT_HDR  144
#define EEPROM_ADDR_EVENT_LIST 152

// ====================
// Data Structures
// ====================
// IntelliSense may mis-evaluate sizeof() / alignment for EEPROM structs.
// The Particle compiler is the source of truth.

struct EEPROMHeader {
    uint16_t magic;         // Identifier
    uint8_t version;        // Structure version
    uint8_t flags;          // Reserved for future
    time32_t lastWriteUTC;  // Timestamp of last EEPROM update
    uint8_t reserved[8];    // Padding/future use
};
static_assert(sizeof(EEPROMHeader) == 16, "EEPROMHeader must be 16 bytes");

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
    bool CRS;            // currentStudy (boolean)      // JSON: CRS
    uint32_t status_period_sec;         // normal status cadence (default 21600)
    uint16_t force_report_min_gap_sec;  // minimum gap between forced reports (default 60)
    uint8_t reserved[2];               // Padding to make 64 bytes
};
static_assert(sizeof(ConfigData) == 64, "ConfigData must be 64 bytes");

// --- StatusData ---
#define STATUSDATA_SIZE 64
struct StatusData {
    FlagStation OSTA;      // orderedStation (enum)           // JSON: OSTA
    FlagStation NF01;           // null field 01 (was ASTA)        // JSON: n/a
    char AEID[12];         // activeEventID                   // JSON: AEID
    uint32_t NEXT;         // nextActionTime (epoch)          // JSON: NEXT
    uint8_t EVLD;          // eventValid                      // JSON: EVLD
    char NF02[5];               // null field 02 (was MVST)        // JSON: n/a
    float NF03;                 // null field 03 (was VLTS)        // JSON: n/a
    float NF04;                 // null field 04 (was AMPS)        // JSON: n/a
    uint32_t TIME;         // timestamp last written          // JSON: TIME
    uint8_t NF05;       // null field 05 (was LID)         // JSON: n/a 
    FlagStation NSTA;        // nextStation (enum)              // JSON: NSTA
    uint32_t reboot_count;   // count of boots (persisted)       // JSON: RBT (later)
    uint8_t reserved[15];    // Padding/future use
};
static_assert(sizeof(StatusData) == 64, "StatusData must be 64 bytes");

// --- Events ---
struct EventHeader {
    uint8_t eventCount;
    uint8_t reserved[7];
};
static_assert(sizeof(EventHeader) == 8, "EventHeader must be 8 bytes");

struct FlagEvent {
    char     idv[12];        // Event IDV
    char     flg[3];         // Flag abbreviation
    char     bmk[20];        // Begin mark
    char     emk[20];        // End mark
    uint8_t  deleted;        // DEL flag
    char     jur[8];         // Jurisdiction string (e.g. "FE-US")
    uint8_t  sjrCount;       // Number of valid sjrList entries (0..7)
    uint8_t  reserved2;      // alignment pad
    uint16_t sjrList[7];     // Sub-jurisdiction IDs (uint16_t; max 7 entries)
};
static_assert(sizeof(FlagEvent) == 80, "FlagEvent must be 80 bytes");

// --- ConfigExt (CFGX) ---
struct ConfigExt {
    uint16_t magic;            // CFGX_MAGIC
    uint8_t  version;          // CFGX_VERSION
    uint8_t  flags;            // future use

    uint16_t stall_limit_ma;   // current-based stall threshold (mA). default 1800
    uint16_t move_timeout_sec; // timeout-based stall threshold (sec). default 120

    uint8_t  sjrCount;         // number of valid unit SJR entries (0..5)
    uint8_t  sjrPad;           // alignment padding
    uint16_t sjrList[5];       // unit's configured sub-jurisdiction IDs (up to 5)

    uint8_t reserved[44];      // pad to 64 bytes
};
static_assert(sizeof(ConfigExt) == 64, "ConfigExt must be 64 bytes");

// ====================
// Compile-time EEPROM layout checks

static_assert(EEPROM_ADDR_HEADER + sizeof(EEPROMHeader) <= EEPROM_ADDR_CONFIG,
              "EEPROM overlap: HEADER spills into CONFIG");

static_assert(EEPROM_ADDR_CONFIG + sizeof(ConfigData) <= EEPROM_ADDR_STATUS,
              "EEPROM overlap: CONFIG spills into STATUS");

static_assert(EEPROM_ADDR_STATUS + sizeof(StatusData) <= EEPROM_ADDR_EVENT_HDR,
              "EEPROM overlap: STATUS spills into EVENT_HDR");

static_assert(EEPROM_ADDR_EVENT_HDR + sizeof(EventHeader) <= EEPROM_ADDR_EVENT_LIST,
              "EEPROM overlap: EVENT_HDR spills into EVENT_LIST");

// ====================
// Core Functions
// ====================
bool validateOrMigrateEEPROM();
void initEEPROM();
bool migrateEEPROM(uint8_t oldVersion);
void bumpRebootCount();
void initConfigExt();
bool validateOrInitConfigExt();
void readConfigExt(ConfigExt &x);
void writeConfigExt(const ConfigExt &x);
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
