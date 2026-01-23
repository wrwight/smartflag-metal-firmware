#include "Particle.h"
#include "ConfigDefaults.h"
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

    ConfigData cfg = ConfigDefaults::makeDefaultConfig();
    EEPROM.put(EEPROM_ADDR_CONFIG, cfg);

    StatusData status = {};  // Zero-initialize the whole struct
    status.reboot_count = 0;
    strcpy(status.AEID, ""); // Explicitly assign empty string
    status.OSTA = FLAG_UNKNOWN;
    status.NEXT = 0;
    status.EVLD = 0;
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

static uint8_t maxEventsInEEPROM() {
    int upperBound = EEPROM_ADDR_CFGX;  // CFGX starts here
    int bytesAvailable = upperBound - EEPROM_ADDR_EVENT_LIST;
    if (bytesAvailable <= 0) return 0;
    return (uint8_t)(bytesAvailable / (int)sizeof(FlagEvent));
}

bool migrateEEPROM(uint8_t oldVersion) {

    if (oldVersion == 1 && EEPROM_VERSION == 2) {

        // Read existing data from v1 layout
        ConfigData cfg_v1;
        EEPROM.get(EEPROM_ADDR_CONFIG, cfg_v1);

        StatusData st_v1;
        EEPROM.get(EEPROM_ADDR_STATUS, st_v1);

        EventHeader eh;
        EEPROM.get(EEPROM_ADDR_EVENT_HDR, eh);

        // Read event list (bounded)
        uint8_t maxEv = maxEventsInEEPROM();
        uint8_t count = eh.eventCount;
        if (count > maxEv) count = maxEv;

        FlagEvent* events = nullptr;
        if (count > 0) {
            events = new FlagEvent[count];
            for (uint8_t i = 0; i < count; i++) {
                int addr = EEPROM_ADDR_EVENT_LIST + i * sizeof(FlagEvent);
                EEPROM.get(addr, events[i]);
            }
        }

        // Re-init EEPROM with new header/version/layout
        initEEPROM();

        // Restore preserved data
        EEPROM.put(EEPROM_ADDR_CONFIG, cfg_v1);

        // For v2: keep everything we read, and ensure new field is initialized.
        // In v1, reboot_count bytes would have been part of reserved, likely 0.
        // We still enforce explicit init here.
        st_v1.reboot_count = 0;
        EEPROM.put(EEPROM_ADDR_STATUS, st_v1);

        eh.eventCount = count; // ensure bounded
        EEPROM.put(EEPROM_ADDR_EVENT_HDR, eh);

        if (events && count > 0) {
            for (uint8_t i = 0; i < count; i++) {
                int addr = EEPROM_ADDR_EVENT_LIST + i * sizeof(FlagEvent);
                EEPROM.put(addr, events[i]);
            }
            delete[] events;
        }

        // Publish debug (forced) so you can confirm migration happened
        SFDBG::pub("EEP", "Migrated v1->v2 (preserved cfg/status/events)", true);
        return true;
    }

    // Unknown migration path
    SFDBG::pub("EEP", String::format("No migration path from v%u to v%u", oldVersion, EEPROM_VERSION), true);
    return false;
}

void bumpRebootCount() {
    StatusData st;
    EEPROM.get(EEPROM_ADDR_STATUS, st);

    st.reboot_count += 1;
    st.TIME = Time.now();               // optional: treat as write time
    EEPROM.put(EEPROM_ADDR_STATUS, st);

    // SFDBG::pub("BOOT", String::format("reboot_count=%lu", (unsigned long)st.reboot_count), true);
}

// ====================
// Wrappers
// ====================
void readConfig(ConfigData &cfg) {
    EEPROM.get(EEPROM_ADDR_CONFIG, cfg);

    bool changed = false;
    changed |= ConfigDefaults::applyDefaults(cfg);
    changed |= ConfigDefaults::validateAndClamp(cfg);

    // Optional but recommended: write back repaired config so it persists.
    if (changed) {
        writeConfig(cfg);
        Log.info("Config repaired (defaults/clamp) and written back to EEPROM.");
        SFDBG::pub("CFG", "repaired+writeback");
    }
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

void initConfigExt() {
    ConfigExt x = {};
    x.magic = CFGX_MAGIC;
    x.version = CFGX_VERSION;
    x.flags = 0;

    x.stall_limit_ma = 1800;     // default 1800 mA
    x.move_timeout_sec = 120;    // default 120 sec

    EEPROM.put(EEPROM_ADDR_CFGX, x);
    SFDBG::pub("CFGX", "init defaults", true);
}

void readConfigExt(ConfigExt &x) {
    EEPROM.get(EEPROM_ADDR_CFGX, x);
}

void writeConfigExt(const ConfigExt &x) {
    EEPROM.put(EEPROM_ADDR_CFGX, x);
}

static bool clampConfigExt(ConfigExt &x) {
    bool changed = false;

    // stall_limit_ma: clamp to reasonable bounds
    if (x.stall_limit_ma < 200)  { x.stall_limit_ma = 200;  changed = true; }
    if (x.stall_limit_ma > 5000) { x.stall_limit_ma = 5000; changed = true; }

    // move_timeout_sec: clamp to reasonable bounds
    if (x.move_timeout_sec < 10)   { x.move_timeout_sec = 10;   changed = true; }
    if (x.move_timeout_sec > 600)  { x.move_timeout_sec = 600;  changed = true; }

    return changed;
}

bool validateOrInitConfigExt() {
    ConfigExt x;
    readConfigExt(x);

    if (x.magic != CFGX_MAGIC || x.version != CFGX_VERSION) {
        initConfigExt();
        return true;
    }

    bool changed = false;

    // defaults if unset
    if (x.stall_limit_ma == 0) { x.stall_limit_ma = 1800; changed = true; }
    if (x.move_timeout_sec == 0) { x.move_timeout_sec = 120; changed = true; }

    changed |= clampConfigExt(x);

    if (changed) {
        writeConfigExt(x);
        SFDBG::pub("CFGX", "repaired+clamped", true);
    }

    return true;
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

    ConfigExt x;
    readConfigExt(x);

    // Ensure CFGX is initialized (safe, cheap)
    if (x.magic != CFGX_MAGIC || x.version != CFGX_VERSION) {
        initConfigExt();
        readConfigExt(x);
    }

    char buffer[256];
    memset(buffer, 0, sizeof(buffer));               // optional but fine
    JSONBufferWriter writer(buffer, sizeof(buffer)); // use full buffer size

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
    writer.name("SPS").value(cfg.status_period_sec);
    writer.name("MGS").value(cfg.force_report_min_gap_sec);

    // --- ConfigExt additions ---
    writer.name("SLM").value((int)x.stall_limit_ma);      // Stall Limit (mA)
    writer.name("TMO").value((int)x.move_timeout_sec);    // Timeout (sec)

    writer.endObject();

    // Lock down output length (prevents buffer tail artifacts)
    size_t n = writer.bufferSize();   // if your build uses dataSize(), swap this line
    if (n > sizeof(buffer)) n = sizeof(buffer);
    if (n < sizeof(buffer)) buffer[n] = '\0';            // optional terminator

    return String(buffer, n);
}

bool jsonToConfig(const String &jsonStr) {
    ConfigData cfg;
    readConfig(cfg);

    ConfigExt x;
    readConfigExt(x);

    // If CFGX not initialized yet, fix it now (safe)
    if (x.magic != CFGX_MAGIC || x.version != CFGX_VERSION) {
        initConfigExt();
        readConfigExt(x);
    }

    JSONValue root = JSONValue::parseCopy(jsonStr);
    if (!root.isValid() || !root.isObject()) {
        SFDBG::pub("CFG", "Invalid JSON for config update", true);
        return false;
    }

    JSONObjectIterator it(root);
    while (it.next()) {
        String keyStr = String(it.name());
        const char *key = keyStr.c_str();
        JSONValue val = it.value();

        if (strcmp(key, "FLG") == 0 && val.isString()) {
            strncpy(cfg.FLG, val.toString().data(), sizeof(cfg.FLG) - 1);
            cfg.FLG[sizeof(cfg.FLG) - 1] = '\0';
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
            cfg.FED[sizeof(cfg.FED) - 1] = '\0';
        }
        else if (strcmp(key, "STA") == 0 && val.isString()) {
            strncpy(cfg.STA, val.toString().data(), sizeof(cfg.STA) - 1);
            cfg.STA[sizeof(cfg.STA) - 1] = '\0';
        }
        else if (strcmp(key, "ZIP") == 0 && val.isString()) {
            strncpy(cfg.ZIP, val.toString().data(), sizeof(cfg.ZIP) - 1);
            cfg.ZIP[sizeof(cfg.ZIP) - 1] = '\0';
        }
        else if (strcmp(key, "STD") == 0 && val.isNumber()) {
            cfg.STD = val.toDouble();
        }
        else if (strcmp(key, "DST") == 0 && val.isBool()) {
            cfg.DST = val.toBool();
        }
        else if (strcmp(key, "MOD") == 0 && val.isString()) {
            strncpy(cfg.MOD, val.toString().data(), sizeof(cfg.MOD) - 1);
            cfg.MOD[sizeof(cfg.MOD) - 1] = '\0';
        }
        else if (strcmp(key, "SLM") == 0 && val.isNumber()) {
            x.stall_limit_ma = (uint16_t)val.toInt();
        }
        else if (strcmp(key, "TMO") == 0 && val.isNumber()) {
            x.move_timeout_sec = (uint16_t)val.toInt();
        }

        // === Step 3C additions (consistent with your current style) ===
        // Long keys maintained for "backward compatibility" - should be able to eliminate later.
        // Note: supplying values of 0 causes a reversion to the defaults/clamps in validateAndClamp().

        else if ((strcmp(key, "SPS") == 0 || strcmp(key, "status_period_sec") == 0) && val.isNumber()) {
            cfg.status_period_sec = (uint32_t)val.toInt();
        }
        else if ((strcmp(key, "MGS") == 0 || strcmp(key, "force_report_min_gap_sec") == 0) && val.isNumber()) {
            cfg.force_report_min_gap_sec = (uint16_t)val.toInt();
}

    }

    ConfigDefaults::applyDefaults(cfg);         // ensure defaults for missing fields
    ConfigDefaults::validateAndClamp(cfg);      // ensure valid ranges

    writeConfig(cfg);
    bool changedX = clampConfigExt(x);
    writeConfigExt(x);

    if (changedX) {
        SFDBG::pub("CFGX", "patched+clamped", true);
    }

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
        halMgr1.applyConfigExtToRuntime();
        return 1;
    }
    return -1;
}