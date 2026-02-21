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
    strncpy(cfg.ZIP, "",      sizeof(cfg.ZIP) - 1);

    cfg.STD = -5.0f;
    cfg.DST = true;

    strncpy(cfg.MOD, "G3", sizeof(cfg.MOD) - 1);

    cfg.CRS = false;

    // status_period_sec: heartbeat cadence in seconds.
    // Set to 0 via SetConfig to disable periodic heartbeat reports.
    cfg.status_period_sec = 21600;        // 6 hours default

    // force_report_min_gap_sec: retained for EEPROM compatibility,
    // but no longer used as a publish gate. The burst limiter in
    // FlagUtils.cpp handles runaway protection instead.
    cfg.force_report_min_gap_sec = 60;    // legacy default — not enforced

    return cfg;
}

bool applyDefaults(ConfigData &cfg) {
    bool changed = false;

    safeNullTerminate(cfg.FLG, sizeof(cfg.FLG));
    safeNullTerminate(cfg.FED, sizeof(cfg.FED));
    safeNullTerminate(cfg.STA, sizeof(cfg.STA));
    safeNullTerminate(cfg.ZIP, sizeof(cfg.ZIP));
    safeNullTerminate(cfg.MOD, sizeof(cfg.MOD));

    if (isEmptyStr(cfg.FLG, sizeof(cfg.FLG))) { strncpy(cfg.FLG, "US",    sizeof(cfg.FLG) - 1); changed = true; }
    if (cfg.FPR == 0)                           { cfg.FPR = 1;                                   changed = true; }
    if (isEmptyStr(cfg.FED, sizeof(cfg.FED))) { strncpy(cfg.FED, "FE-US", sizeof(cfg.FED) - 1); changed = true; }
    if (isEmptyStr(cfg.STA, sizeof(cfg.STA))) { strncpy(cfg.STA, "FE-XX", sizeof(cfg.STA) - 1); changed = true; }
    if (isEmptyStr(cfg.MOD, sizeof(cfg.MOD))) { strncpy(cfg.MOD, "G3",    sizeof(cfg.MOD) - 1); changed = true; }

    // NOTE: status_period_sec == 0 is a VALID value meaning "disabled".
    // Do NOT default it here. Only makeDefaultConfig() sets the initial value.

    // NOTE: STD / DST / LAT / LNG left as-is — 0 is valid for those fields.

    return changed;
}

bool validateAndClamp(ConfigData &cfg) {
    bool changed = false;

    if (cfg.FPR < 1) { cfg.FPR = 1; changed = true; }
    if (cfg.FPR > 9) { cfg.FPR = 9; changed = true; }

    safeNullTerminate(cfg.FLG, sizeof(cfg.FLG));
    safeNullTerminate(cfg.FED, sizeof(cfg.FED));
    safeNullTerminate(cfg.STA, sizeof(cfg.STA));
    safeNullTerminate(cfg.ZIP, sizeof(cfg.ZIP));
    safeNullTerminate(cfg.MOD, sizeof(cfg.MOD));

    // status_period_sec: 0 = disabled (valid). If non-zero, clamp to sane range.
    if (cfg.status_period_sec > 0) {
        if (cfg.status_period_sec < 60)    { cfg.status_period_sec = 60;    changed = true; }
        if (cfg.status_period_sec > 86400) { cfg.status_period_sec = 86400; changed = true; }
    }

    // force_report_min_gap_sec: retained for EEPROM layout compatibility.
    // Still clamped to keep EEPROM data sane, but no longer used as a gate.
    if (cfg.force_report_min_gap_sec < 5)    { cfg.force_report_min_gap_sec = 5;    changed = true; }
    if (cfg.force_report_min_gap_sec > 1800) { cfg.force_report_min_gap_sec = 1800; changed = true; }

    return changed;
}

} // namespace ConfigDefaults
