/**
 * @file    EventManager.h
 * @brief   Public interface for the SmartFlag Gen3 event scheduling module.
 *
 * @details
 * Ported from Gen2 SFScheduler.  Declares @c FlagEventEx (in-RAM event record),
 * @c EMrc (return codes), and @c EventManager (the scheduling engine).
 *
 * Key adaptations from Gen2:
 *  - SFSta::FULL / SFSta::HALF  →  FLAG_FULL / FLAG_HALF  (FlagStation, HalyardManager.h)
 *  - myDirector.setOrderedSta() →  halMgr1.setOrderedStation()  (via _setStationCB)
 *  - EEPROM I/O                 →  EEPROMManager helpers  (EEPROM_ADDR_EVENT_HDR / _LIST)
 *  - myLink.resetSubscriptions()→  evMgr.resetSubscriptions()
 *  - s_EEevent                  →  FlagEvent struct in EEPROMManager.h
 *  - Sub-jurisdiction filtering →  NEW — SJR[] in event JSON; sjrID in ConfigExt
 *
 * **Subscription design:**
 * Each unit subscribes to exactly two Particle topics whose full names are
 * stored directly in config (e.g. @c "FE-US", @c "FE-UT").  No prefix is
 * added by the firmware, allowing alternative prefixes or extensions in the
 * future.  Both topics route through the file-scope @c sfEventHandler()
 * forwarder to @c EventManager::receiveEvent().
 *
 * @c eventApplies() performs the second-level filter using a prefix match on
 * the event JSON @c JUR field against @c jurFederal / @c jurState: a more
 * specific published jurisdiction (e.g. @c "FE-UT-X") matches a less specific
 * subscription (@c "FE-UT"), but a less specific publish never matches a more
 * specific subscription.  Whenever @c jurFederal or @c jurState changes,
 * @c resetSubscriptions() drops all subscriptions and re-registers the two
 * correct topics.
 */

#pragma once

#include "application.h"   // Particle / Device OS
#include "math.h"

#include "HalyardManager.h"   // FlagStation enum
#include "EEPROMManager.h"    // FlagEvent struct, EEPROM addresses

// ─────────────────────────────────────────────────────────────────────────────
/**
 * @enum  EMrc
 * @brief Return codes used by EventManager methods.
 *
 * Methods that can fail return @c (int)EMrc rather than bare integers so that
 * callers can compare against named values.  @c SUCCESS == 0 so that standard
 * @c if(rc) error checks work naturally.
 */
enum class EMrc : int {
    SUCCESS       = 0,
    GENFAIL       = 1,
    NOT_HF        = 2,
    BAD_SUNEVENT  = 3,
    NOT_SRSS      = 4,
    NOT_ZL        = 5,
    PARSE_ERROR   = 6,
    EVL_OVERFLOW  = 7
};

// ─────────────────────────────────────────────────────────────────────────────
/**
 * @class  FlagEventEx
 * @brief  In-RAM representation of a single flag-raising event.
 *
 * @details
 * Extends the on-disk @c FlagEvent POD (EEPROMManager.h) with derived and
 * runtime-only fields that are not persisted: @c valid, @c applies, @c isDelete,
 * and the resolved @c GMTbegin / @c GMTend epoch timestamps.
 *
 * Raw time-mark strings (@c BMK, @c EMK) are retained so that events whose
 * marks reference sunrise/sunset can be re-resolved by @c reprocessEvents()
 * whenever the clock is corrected or DST transitions.
 *
 * The @c sjrList / @c sjrCount fields are Gen3-only, carrying the optional list
 * of sub-jurisdiction IDs from the event JSON @c SJR field.
 */
class FlagEventEx {
public:
    bool         valid      = false;        // true after successful parse
    bool         applies    = false;        // true if event applies to this unit
    bool         isDelete   = false;        // true if JSON DEL field is true
    int          eventID    = 0;            // IDV – main event ID
    int          eventVer   = 0;            // idV – version number
    time_t       GMTbegin   = 0;            // resolved GMT begin epoch
    time_t       GMTend     = 0;            // resolved GMT end epoch (0 = open-ended)
    String       eventJur   = "";           // JUR – published jurisdiction
    String       eventFlag  = "";           // FLG – published flag abbr. (e.g. "US", "TN")
    FlagStation  toSta      = FLAG_UNKNOWN; // target station; HALF assumed for standard events
    String       BMK        = "";           // raw begin time-mark string
    String       EMK        = "";           // raw end time-mark string

    // Sub-jurisdiction list (NEW – Gen3 only)
    static const int MAX_SJR = 10;
    int          sjrList[MAX_SJR] = {0};    // SJR[] – sub-jurisdiction IDs from event JSON
    int          sjrCount   = 0;            // number of valid entries in sjrList
};

// ─────────────────────────────────────────────────────────────────────────────
/**
 * @class  EventManager
 * @brief  Scheduling engine that drives the halyard based on cloud-pushed events.
 *
 * @details
 * A single global instance (@c evMgr, defined in EventManager.cpp) is used
 * throughout the firmware.  Callers interact through three entry points:
 *
 *  1. @c setup() — called once from @c main.ino setup(); loads config and events
 *     from EEPROM, registers Particle cloud variables, and subscribes to topics.
 *  2. @c loop()  — called every @c loop(); fires the software timer and calls
 *     @c checkForChange() when the next scheduled transition is due.
 *  3. @c reprocessEvents() — called from the @c time_changed system event hook
 *     to re-resolve sunrise/sunset marks after a clock correction.
 *
 * The station-change callback passed to @c setup() decouples EventManager from
 * HalyardManager: the callback is typically a lambda that calls
 * @c halMgr1.setOrderedStation(sta).
 */
class EventManager {

public:
    // ── Constants ────────────────────────────────────────────────────────────
    static const int  N_EVENTS     = 20;     // maximum stored events
    static const char FED_FLAG[3]; // "US"  – federal flag abbreviation

    // ── Constructor / destructor ─────────────────────────────────────────────
    EventManager();
    ~EventManager();

    // ── Lifecycle (call from main.ino) ────────────────────────────────────────

    /// @brief  One-time initialisation.  Pass a callback (or lambda) that routes
    ///         to halMgr1.setOrderedStation() — keeps EventManager decoupled from
    ///         HalyardManager.  Must be called after EEPROM is valid and after
    ///         Particle.connect() (subscribes to configured topics immediately).
    void setup( void (*setStationCB)(FlagStation) );

    /// @brief  Call every loop().  Checks the software timer and fires
    ///         checkForChange() → reprocessEvents() when a transition is due.
    void loop();

    // ── Public API (Particle cloud functions / subscriptions) ─────────────────

    /// @brief  Internal target for Particle subscription callbacks.
    ///         NOT registered with Particle directly — the file-scope
    ///         @c sfEventHandler() forwarder in EventManager.cpp calls this,
    ///         working around the capturing-lambda limitation of the subscribe API.
    /// @return 0 (@c EMrc::SUCCESS) or a non-zero @c EMrc cast to int on error.
    int    receiveEvent       ( String JSONFlagEvent );

    /// @brief  Particle.function() target registered as @c "s_Config" in main.ino.
    ///         Updates config fields from JSON (LAT, LNG, STD, DST, ZIP, FED, STA,
    ///         FPR, FLG, SJR) and calls resetSubscriptions() if jurisdictions change.
    /// @return 0 (@c EMrc::SUCCESS).
    int    configScheduler    ( String JSONconfig    );

    /// @brief  Drops all Particle subscriptions (unsubscribe is all-or-nothing)
    ///         and re-registers for the configured federal and state topic strings.
    ///         Called internally by setup() and configScheduler(); main.ino does
    ///         not need to call this directly.
    void   resetSubscriptions ();

    // ── Query helpers ─────────────────────────────────────────────────────────
    /// @brief  Count of currently valid, applicable events in @c _EVL[].
    int         getNEvents       ();
    /// @brief  Event ID of the first in-progress event, or -1 if none active.
    int         firstActiveEvent ();
    /// @brief  True once config has been loaded (setup() complete or configScheduler() called).
    bool        isConfigured     () const { return _configured; }
    /// @brief  GMT epoch of the next scheduled flag transition (0 if none pending).
    time_t      nextFlagChange   () const { return _nextChange; }
    /// @brief  Station the flag will move TO at the next transition.
    FlagStation nextFlagStation  () const { return _nextSta;   }
    /// @brief  Station the flag should be at RIGHT NOW according to the schedule.
    FlagStation orderedStation   () const { return _orderedSta; }

    // ── Display / diagnostics ─────────────────────────────────────────────────
    /// @brief  JSON string of current scheduler config (Particle variable payload).
    String showConfig   ();
    /// @brief  JSON string of full detail for one event (used by ring-buffer inspector).
    String showEvent    ( const FlagEventEx &ev );
    /// @brief  JSON array of valid event "ID.VER" strings (Particle variable payload).
    String showEventList();

    // ── Ring-buffer event inspector ───────────────────────────────────────────
    /// @brief  Set cursor to @p idx (clamped to 0..N_EVENTS-1).
    ///         Call via Particle.function("s_EvIdx"), then read "s_Event" variable.
    ///         Returns the clamped index actually set.
    int    setShowIdx   ( int idx );
    /// @brief  JSON detail for the event slot at the current cursor index.
    ///         Registered as Particle.variable("s_Event") in setup().
    String showEventAtCursor();

    // ── Called after time-sync or DST change ──────────────────────────────────
    /// @brief  Re-evaluates all stored events against the current clock and config.
    ///         Must be called from the @c time_changed system event hook in main.ino
    ///         and after any config change that affects time resolution (TZ, DST, lat/lng).
    void reprocessEvents();

    // ── Particle.variable lambda helpers (bind to these in main.ino) ──────────
    //    Particle.variable("s_EventLIST", [](){ return evMgr.showEventList(); });
    //    Particle.variable("s_ShowConfig", [](){ return evMgr.showConfig(); });
    //    (Or let setup() register them – see EventManager.cpp)

private:
    // ── Configuration (mirrors Gen2 SFScheduler private props) ───────────────
    //    Most fields read from ConfigData / ConfigExt via EEPROMManager.
    //    Kept in local copies for fast access during event processing.
    String  _upperFlag;        // FLG  – topmost flag abbreviation
    int     _upperFlagPrio;    // FPR  – priority (1=fed, 2=state, 3=other)
    double  _lat;              // LAT  – flagpole latitude
    double  _lng;              // LNG  – flagpole longitude
    String  _jurFederal;       // FED  – federal jurisdiction
    String  _jurState;         // STA  – state/regional jurisdiction
    String  _postalCode;       // ZIP  – postal code
    float   _tzOffset;         // STD  – hours offset from UTC (standard time)
    bool    _doDST;            // DST  – observe DST (North American rules)
    uint16_t _sjrList[5];      // SJR  – this unit's configured sub-jurisdiction IDs
    int      _sjrCount;        //        number of valid entries in _sjrList (0 = unassigned)
                               //        stored in ConfigExt.sjrCount / sjrList

    // ── State ─────────────────────────────────────────────────────────────────
    bool        _configured  = false;
    bool        _attentionFlag = false;

    FlagStation _orderedSta  = FLAG_UNKNOWN;  // where flag should be RIGHT NOW (auto mode)
    time_t      _nextChange  = 0;             // GMT epoch of next flag movement
    FlagStation _nextSta     = FLAG_UNKNOWN;  // station for next movement

    unsigned long _msUntilNext = 0;           // ms until next timer fires
    unsigned long _timerStartMs = 0;          // millis() when timer was last set

    // ── In-RAM event list ─────────────────────────────────────────────────────
    FlagEventEx _EVL[N_EVENTS];

    // ── Station callback (set in setup()) ────────────────────────────────────
    void (*_setStationCB)(FlagStation) = nullptr;

    // ── Ring-buffer cursor ────────────────────────────────────────────────────
    int  _showIdx = 0;   // index for s_Event / s_EvIdx inspector

    // ── Private helpers ───────────────────────────────────────────────────────
    void        setNextEvent   ();
    void        updEventTimer  ();
    void        checkForChange ();

    int         purgeEvents    ();
    void        clearEvent     ( FlagEventEx &ev );
    int         matchEVID      ( int id );

    bool        jurMatch       ( const String &pubJur, const String &subJur );
    bool        eventApplies   ( const FlagEventEx &ev );

    FlagEventEx parseEvent     ( const String &json );

    // Sun-time helpers (ported verbatim from Gen2)
    int         UTCSunEvent    ( float latitude, float longitude, int doy, int rs );
    int         parseTimeMark  ( time_t &tVal, const String &mTime, const String &mDest );
    int         getSunTime     ( time_t &tVal, struct tm &sTime, int srss );
    int         getSunrise     ( time_t &tVal, struct tm &date );
    int         getSunset      ( time_t &tVal, struct tm &date );
    int         getZTime       ( time_t &tVal, struct tm &date, const String &hhmm );
    int         getLTime       ( time_t &tVal, struct tm &date, const String &hhmm );

    bool        isDST          ( int dayOfMonth, int month, int dayOfWeek );

    // EEPROM helpers
    int         loadFromEEPROM ();
    int         saveToEEPROM   ();
    void        loadConfig     ();   // pull ConfigData / ConfigExt into local copies

    // Formatting
    String      staToLetter    ( FlagStation sta );
};

// ─────────────────────────────────────────────────────────────────────────────
//  Global instance declaration (defined in EventManager.cpp)
// ─────────────────────────────────────────────────────────────────────────────
extern EventManager evMgr;
