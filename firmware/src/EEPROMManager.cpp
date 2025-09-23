#include "Particle.h"
#include "EEPROMManager.h"
#include "HalyardManager.h"
#include "FlagUtils.h"
#include "fsm/SmartFlagFSM.h"
#include "sensors/Sensor.h"
#include <time.h>

// These must be defined in main firmware
extern HalyardManager halMgr1;
extern FSMController fsm;
extern Sensor lidSensor;        // Lid sensor

// ====================
// Validation & Migration
// ====================
bool validateOrMigrateEEPROM() {
    EEPROMHeader hdr;
    EEPROM.get(EEPROM_ADDR_HEADER, hdr);

    if (hdr.magic != EEPROM_MAGIC) {
        Log.info("EEPROM not initialized or wrong magic. Initializing...");
        initEEPROM();
        return true;
    }

    if (hdr.version < EEPROM_VERSION) {
        Log.info("EEPROM version %d found. Migrating to %d...", hdr.version, EEPROM_VERSION);
        return migrateEEPROM(hdr.version);
    }

    if (hdr.version > EEPROM_VERSION) {
        Log.error("EEPROM version %d is newer than firmware (%d).", hdr.version, EEPROM_VERSION);
        return false;
    }

    return true;
}

void initEEPROM() {
    Log.info("Initializing EEPROM...");

    EEPROMHeader hdr = {
        .magic = EEPROM_MAGIC,
        .version = EEPROM_VERSION,
        .flags = 0,
        .lastWriteUTC = Time.now()
    };
    EEPROM.put(EEPROM_ADDR_HEADER, hdr);

    ConfigData cfg = {0};
    strncpy(cfg.FLG, "US", sizeof(cfg.FLG));
    cfg.FPR = 1;
    cfg.LAT = 0.0;
    cfg.LNG = 0.0;
    strncpy(cfg.FED, "FE-US", sizeof(cfg.FED));
    strncpy(cfg.STA, "FE-XX", sizeof(cfg.STA));
    strncpy(cfg.ZIP, "", sizeof(cfg.ZIP));
    cfg.STD = -5.0;
    cfg.DST = true;
    strncpy(cfg.MOD, "G3", sizeof(cfg.MOD));
    EEPROM.put(EEPROM_ADDR_CONFIG, cfg);

    StatusData status = {};  // Zero-initialize the whole struct
    strcpy(status.AEID, ""); // Explicitly assign empty string
    status.OSTA = FLAG_UNKNOWN;
    status.NEXT = 0;
    status.EVLD = false;
    // strcpy(status.MVST, "NONE");
    // status.VLTS = 0.0f;
    // status.AMPS = 0.0f;
    status.TIME = Time.now();
    // status.LID = false;
    status.NSTA = FLAG_UNKNOWN;

    EEPROM.put(EEPROM_ADDR_STATUS, status);

    EventHeader eventHeader = {0};
    EEPROM.put(EEPROM_ADDR_EVENT_HDR, eventHeader);

    Log.info("EEPROM initialized with magic 'G3'.");
}

bool migrateEEPROM(uint8_t oldVersion) {
    if (oldVersion == 1) {
        Log.info("Migration logic not yet implemented. Reinitializing.");
        initEEPROM();
        return true;
    }
    Log.error("Unrecognized EEPROM version %d. Aborting.", oldVersion);
    return false;
}

// ====================
// Wrappers
// ====================
void readConfig(ConfigData &cfg) {
    EEPROM.get(EEPROM_ADDR_CONFIG, cfg);
}

void writeConfig(const ConfigData &cfg) {
    EEPROM.put(EEPROM_ADDR_CONFIG, cfg);
}

static const char* moveStatusToCode(FlagMoveStatus status) {
    switch (status) {
        case FLAG_MOVE_NONE:       return "NONE";
        case FLAG_MOVING_UP:       return "UP  ";
        case FLAG_MOVING_DOWN:     return "DOWN";
        case FLAG_ON_STATION:      return "STAT";
        case FLAG_MOVE_CANCELLED:  return "CNCL";
        case FLAG_MOVE_TIMEOUT:    return "TIME";
        case FLAG_MOVE_STALL:      return "STAL";
        default:                   return "UNKN";
    }
}

void readStatus(StatusData &status) {
    EEPROM.get(EEPROM_ADDR_STATUS, status);
}

void writeStatus(const StatusData &statusIn) {

    StatusData status = statusIn;
    status.TIME = Time.now();
    EEPROM.put(EEPROM_ADDR_STATUS, status);
}

void readEventHeader(EventHeader &hdr) {
    EEPROM.get(EEPROM_ADDR_EVENT_HDR, hdr);
}

void writeEventHeader(const EventHeader &hdr) {
    EEPROM.put(EEPROM_ADDR_EVENT_HDR, hdr);
}

bool readEvent(uint8_t index, FlagEvent &evt) {
    EventHeader hdr;
    readEventHeader(hdr);
    if (index >= hdr.eventCount) return false;

    int addr = EEPROM_ADDR_EVENT_LIST + index * sizeof(FlagEvent);
    EEPROM.get(addr, evt);
    return true;
}

bool writeEvent(uint8_t index, const FlagEvent &evt) {
    EventHeader hdr;
    readEventHeader(hdr);
    if (index >= hdr.eventCount) return false;

    int addr = EEPROM_ADDR_EVENT_LIST + index * sizeof(FlagEvent);
    EEPROM.put(addr, evt);
    return true;
}

// ====================
// JSON Helpers
// ====================
String configToJSON() {
    ConfigData cfg;
    readConfig(cfg);

    char buffer[256];
    JSONBufferWriter writer(buffer, sizeof(buffer) - 1);

    writer.beginObject();
    writer.name("FLG").value(cfg.FLG);
    writer.name("FPR").value(cfg.FPR);
    writer.name("LAT").value(cfg.LAT, 6);
    writer.name("LNG").value(cfg.LNG, 6);
    writer.name("FED").value(cfg.FED);
    writer.name("STA").value(cfg.STA);
    writer.name("ZIP").value(cfg.ZIP);
    writer.name("STD").value(cfg.STD, 2);
    writer.name("DST").value(cfg.DST);
    writer.name("MOD").value(cfg.MOD);
    writer.endObject();

    buffer[writer.dataSize()] = 0;
    return String(buffer);
}

bool jsonToConfig(const String &jsonStr) {
    ConfigData cfg;
    readConfig(cfg);

    JSONValue root = JSONValue::parseCopy(jsonStr);
    if (!root.isValid() || !root.isObject()) {
        Log.error("Invalid JSON for config update");
        return false;
    }

    JSONObjectIterator it(root);
    while (it.next()) {
        String keyStr = String(it.name());
        const char *key = keyStr.c_str();
        JSONValue val = it.value();

        if (strcmp(key, "FLG") == 0 && val.isString()) {
            strncpy(cfg.FLG, val.toString().data(), sizeof(cfg.FLG) - 1);
        }
        else if (strcmp(key, "FPR") == 0 && val.isNumber()) {
            cfg.FPR = val.toInt();
        }
        else if (strcmp(key, "LAT") == 0 && val.isNumber()) {
            cfg.LAT = val.toDouble();
        }
        else if (strcmp(key, "LNG") == 0 && val.isNumber()) {
            cfg.LNG = val.toDouble();
        }
        else if (strcmp(key, "FED") == 0 && val.isString()) {
            strncpy(cfg.FED, val.toString().data(), sizeof(cfg.FED) - 1);
        }
        else if (strcmp(key, "STA") == 0 && val.isString()) {
            strncpy(cfg.STA, val.toString().data(), sizeof(cfg.STA) - 1);
        }
        else if (strcmp(key, "ZIP") == 0 && val.isString()) {
            strncpy(cfg.ZIP, val.toString().data(), sizeof(cfg.ZIP) - 1);
        }
        else if (strcmp(key, "STD") == 0 && val.isNumber()) {
            cfg.STD = val.toDouble();
        }
        else if (strcmp(key, "DST") == 0 && val.isBool()) {
            cfg.DST = val.toBool();
        }
        else if (strcmp(key, "MOD") == 0 && val.isString()) {
            strncpy(cfg.MOD, val.toString().data(), sizeof(cfg.MOD) - 1);
        }
    }

    writeConfig(cfg);
    return true;
}

void saveOSTA (FlagStation osta) {
    StatusData status;
    readStatus(status);
    status.OSTA = osta;
    writeStatus(status);
}

void saveNSTA (FlagStation nsta) {
    StatusData status;
    readStatus(status);
    status.NSTA = nsta;
    writeStatus(status);
}

time_t parseUTC(const String& utcString) {
    
    struct tm t = {0};
    if (sscanf(utcString.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d",
       &t.tm_year, &t.tm_mon, &t.tm_mday,
       &t.tm_hour, &t.tm_min, &t.tm_sec) !=6) {
        return 0; // Invalid format
    }

    t.tm_year -= 1900;
    t.tm_mon -= 1;

    time_t localTime = mktime(&t);  // Interprets the tm structure as local time
    return localTime;               // Convert to time_t in UTC
}


// ====================
// Cloud Handlers
// ====================
int setConfigHandler(String data) {
    if (jsonToConfig(data)) {
        // Log.info("Config updated via cloud: %s", configToJSON().c_str());
        return 1;
    }
    return -1;
}