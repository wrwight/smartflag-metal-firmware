#include "ConfigDefaults.h"

namespace ConfigDefaults {

static bool isEmptyStr(const char *s, size_t n) {
    if (!s || n == 0) return true;
    return s[0] == '\0';
}

static void safeNullTerminate(char *s, size_t n) {
    if (!s || n == 0) return;
    s[n - 1] = '\0';
}

ConfigData makeDefaultConfig() {
    ConfigData cfg = {0};

    strncpy(cfg.FLG, "US", sizeof(cfg.FLG) - 1);
    cfg.FPR = 1;

    cfg.LAT = 0.0f;
    cfg.LNG = 0.0f;

    strncpy(cfg.FED, "FE-US", sizeof(cfg.FED) - 1);
    strncpy(cfg.STA, "FE-XX", sizeof(cfg.STA) - 1);
    strncpy(cfg.ZIP, "", sizeof(cfg.ZIP) - 1);

    cfg.STD = -5.0f;
    cfg.DST = true;

    strncpy(cfg.MOD, "G3", sizeof(cfg.MOD) - 1);

    // Keep CRS as-is for now (it exists in your struct), default false is fine.
    cfg.CRS = false;
    cfg.status_period_sec = 21600;         // 6 hours
    cfg.force_report_min_gap_sec = 60;     // 1 minute

    return cfg;
}

bool applyDefaults(ConfigData &cfg) {
    bool changed = false;

    // Ensure null termination even if EEPROM had garbage.
    safeNullTerminate(cfg.FLG, sizeof(cfg.FLG));
    safeNullTerminate(cfg.FED, sizeof(cfg.FED));
    safeNullTerminate(cfg.STA, sizeof(cfg.STA));
    safeNullTerminate(cfg.ZIP, sizeof(cfg.ZIP));
    safeNullTerminate(cfg.MOD, sizeof(cfg.MOD));

    if (isEmptyStr(cfg.FLG, sizeof(cfg.FLG))) {
        strncpy(cfg.FLG, "US", sizeof(cfg.FLG) - 1);
        changed = true;
    }
    if (cfg.FPR == 0) {
        cfg.FPR = 1;
        changed = true;
    }
    if (isEmptyStr(cfg.FED, sizeof(cfg.FED))) {
        strncpy(cfg.FED, "FE-US", sizeof(cfg.FED) - 1);
        changed = true;
    }
    if (isEmptyStr(cfg.STA, sizeof(cfg.STA))) {
        strncpy(cfg.STA, "FE-XX", sizeof(cfg.STA) - 1);
        changed = true;
    }
    if (isEmptyStr(cfg.MOD, sizeof(cfg.MOD))) {
        strncpy(cfg.MOD, "G3", sizeof(cfg.MOD) - 1);
        changed = true;
    }
    if (cfg.status_period_sec == 0) {
        cfg.status_period_sec = 21600;
        changed = true;
    }
    if (cfg.force_report_min_gap_sec == 0) {
        cfg.force_report_min_gap_sec = 60;
        changed = true;
    }

    // NOTE: We do NOT “default-fill” STD/DST/LAT/LNG if 0, because 0 can be valid.
    // Those will be handled by initEEPROM() default config, or explicit patches.

    return changed;
}

bool validateAndClamp(ConfigData &cfg) {
    bool changed = false;

    // Clamp FPR to a sane range (you can widen later)
    if (cfg.FPR < 1) { cfg.FPR = 1; changed = true; }
    if (cfg.FPR > 9) { cfg.FPR = 9; changed = true; }

    // Null terminate again defensively
    safeNullTerminate(cfg.FLG, sizeof(cfg.FLG));
    safeNullTerminate(cfg.FED, sizeof(cfg.FED));
    safeNullTerminate(cfg.STA, sizeof(cfg.STA));
    safeNullTerminate(cfg.ZIP, sizeof(cfg.ZIP));
    safeNullTerminate(cfg.MOD, sizeof(cfg.MOD));

    // Status cadence: min 5 minutes, max 24 hours (tweak if you want)
    if (cfg.status_period_sec < 60) { cfg.status_period_sec = 60; changed = true; }
    if (cfg.status_period_sec > 86400) { cfg.status_period_sec = 86400; changed = true; }

    // Forced min-gap: min 5 seconds, max 30 minutes
    if (cfg.force_report_min_gap_sec < 5) { cfg.force_report_min_gap_sec = 5; changed = true; }
    if (cfg.force_report_min_gap_sec > 1800) { cfg.force_report_min_gap_sec = 1800; changed = true; }

    return changed;
}

} // namespace ConfigDefaults
