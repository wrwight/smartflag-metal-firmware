/**
 * @file    EventManager.cpp
 * @brief   Flag-event scheduling and lifecycle management for SmartFlag Gen3.
 *
 * @details
 * Ported from Gen2 SFScheduler. Owns the full lifecycle of flag-raising events:
 * receiving them via Particle subscription, parsing and validating JSON payloads,
 * resolving time marks to absolute GMT timestamps, persisting the event list to
 * EEPROM across reboots, and signalling the halyard when a scheduled transition
 * is due.
 *
 * Key responsibilities:
 *  - Subscribe to Particle topics "FE_\<federal\>" and "FE_\<state\>" and update
 *    subscriptions whenever jurisdiction config changes.
 *  - Parse incoming FlagEvent JSON fields: IDV, JUR, FLG, BMK, EMK, DEL, SJR.
 *  - Resolve time marks: date-only (sunrise/sunset by context), SR, SS,
 *    HH:MM GMT, HH:MM local (with TZ offset and DST).
 *  - Store up to N_EVENTS events in RAM (_EVL[]) and persist to EEPROM.
 *  - Fire the station-change callback (_setStationCB) when ordered station changes.
 *  - Reprocess all stored events on time-sync or DST change (via reprocessEvents()).
 *
 * EEPROM layout (from EEPROMManager.h):
 *  - @c EEPROM_ADDR_EVENT_HDR (144): EventHeader struct (8 bytes)
 *  - @c EEPROM_ADDR_EVENT_LIST   (152): FlagEvent[N_EVENTS] array
 *
 * @note  Particle.unsubscribe() drops ALL subscriptions for the device — there
 *        is no topic-specific form.  EventManager currently owns all subscriptions;
 *        if other modules ever need their own, they must be re-registered inside
 *        resetSubscriptions().
 *
 * @note  JSON_BUF is set to 1024 bytes.  Particle publish payloads are limited to
 *        622 bytes; keep showConfig() and showEvent() output within that limit.
 */

#include "EventManager.h"
#include "EEPROMManager.h"
#include "FlagUtils.h"
#include "Dbg.h"

// JSON working buffer size – keep in line with rest of Gen3 codebase
static const int JSON_BUF = 1024;

// ─────────────────────────────────────────────────────────────────────────────
//  Static member definitions
// ─────────────────────────────────────────────────────────────────────────────
const char EventManager::FED_FLAG[3] = "US";

// Global instance
EventManager evMgr;

// ─────────────────────────────────────────────────────────────────────────────
//  sfEventHandler  –  file-scope static Particle subscription callback
//
//  Particle.subscribe() requires a plain (non-capturing) function pointer.
//  This forwarder bridges that requirement to the evMgr instance method.
//  Both the federal (e.g. "FE-US") and state (e.g. "FE-OH") subscriptions point here;
//  the topic argument is available if needed for diagnostics but eventApplies()
//  does the real jurisdiction filtering inside receiveEvent().
// ─────────────────────────────────────────────────────────────────────────────
static void sfEventHandler( const char * /*topic*/, const char *data ) {
    evMgr.receiveEvent( String(data) );
}

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor / destructor
// ─────────────────────────────────────────────────────────────────────────────
EventManager::EventManager()
    : _upperFlag(""), _upperFlagPrio(0),
      _lat(0.0), _lng(0.0),
      _jurFederal(""), _jurState(""), _postalCode(""),
      _tzOffset(0.0f), _doDST(false), _sjrCount(0)
{
    memset( _sjrList, 0, sizeof(_sjrList) );
}

EventManager::~EventManager() {
}

// ─────────────────────────────────────────────────────────────────────────────
//  setup()  –  call once from main.ino setup()
// ─────────────────────────────────────────────────────────────────────────────
void EventManager::setup( void (*setStationCB)(FlagStation) ) {
    _setStationCB = setStationCB;
    loadConfig();                   // pull ConfigData / ConfigExt into local cache
    _configured = ( loadFromEEPROM() == 0 );
    reprocessEvents();              // re-evaluate stored events against current config / time

    // Register Particle cloud variables and ring-buffer inspector
    Particle.variable( "s_EventLIST",  [this](){ return showEventList();      } );
    Particle.variable( "s_ShowConfig", [this](){ return showConfig();          } );
    Particle.variable( "s_Event",      [this](){ return showEventAtCursor();   } );
    Particle.function( "s_EvIdx",      [this](String s) -> int { return setShowIdx(s.toInt()); } );

    // Register subscriptions for this unit's jurisdictions.
    // configScheduler() will call resetSubscriptions() again if jurisdictions change.
    // main.ino registers the Particle.function("s_Config") binding separately.
    resetSubscriptions();
}

// ─────────────────────────────────────────────────────────────────────────────
//  resetSubscriptions()  –  drop all subscriptions and re-register for the
//                           current jurFederal and jurState values.
//
//  Must be called:
//    1) Once during setup(), after loadConfig() has populated _jurFederal/_jurState
//    2) Any time configScheduler() changes FED or STA fields
//
//  Particle.unsubscribe() has no topic-specific form – it drops ALL subscriptions
//  for this device.  That is fine here because EventManager owns all subscriptions.
//  If other parts of the firmware ever need their own subscriptions, coordinate
//  so that resetSubscriptions() re-registers those as well.
// ─────────────────────────────────────────────────────────────────────────────
void EventManager::resetSubscriptions() {
    Particle.unsubscribe();   // drop all existing subscriptions

    if ( _jurFederal.length() > 0 ) {
        Particle.subscribe( _jurFederal, sfEventHandler );
        SFDBG::pub("EM", "subscribed to " + _jurFederal, true);
    }

    if ( _jurState.length() > 0 && _jurState != _jurFederal ) {
        Particle.subscribe( _jurState, sfEventHandler );
        SFDBG::pub("EM", "subscribed to " + _jurState, true);
    }
}


// ─────────────────────────────────────────────────────────────────────────────
void EventManager::loop() {
    if ( !_configured ) return;

    // Software timer replacement: check if our interval has elapsed
    if ( _msUntilNext > 0 ) {
        unsigned long elapsed = millis() - _timerStartMs;
        if ( elapsed >= _msUntilNext ) {
            _attentionFlag = true;
        }
    }

    if ( _attentionFlag ) {
        checkForChange();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  checkForChange()
//  Compare _orderedSta to what the halyard is currently doing.  If a movement
//  is due, call _setStationCB.  Then reprocess so timer resets.
//  (Gen2 had this stubbed – implemented here per design intent.)
// ─────────────────────────────────────────────────────────────────────────────
void EventManager::checkForChange() {
    _attentionFlag = false;
    reprocessEvents();          // recalculates _orderedSta, _nextChange, _nextSta
    if ( _setStationCB ) {
        _setStationCB( _orderedSta );
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  receiveEvent()  –  Particle subscription callback target
// ─────────────────────────────────────────────────────────────────────────────
int EventManager::receiveEvent( String JSONFlagEvent ) {

    FlagEventEx nEVL = parseEvent( JSONFlagEvent );
    if ( nEVL.eventID <= 0 || !nEVL.valid ) {
        SFDBG::pub("EM", "EVT parse fail", true);
        return (int)EMrc::PARSE_ERROR;
    }

    int idx = matchEVID( nEVL.eventID );

    if ( idx >= 0 ) {
        // ── Update or delete existing event ──────────────────────────────────
        if ( nEVL.eventVer < _EVL[idx].eventVer ) return (int)EMrc::SUCCESS;  // older version – ignore

        if ( nEVL.isDelete ) {
            clearEvent( _EVL[idx] );
        } else {
            _EVL[idx] = nEVL;
            _EVL[idx].applies = eventApplies( _EVL[idx] );
        }
    } else {
        // ── New event ─────────────────────────────────────────────────────────
        if ( nEVL.isDelete ) return (int)EMrc::SUCCESS;  // delete with no match – ignore

        purgeEvents();                           // make room if possible
        idx = matchEVID( 0 );                    // find an empty slot (eventID == 0)
        if ( idx < 0 ) return (int)EMrc::EVL_OVERFLOW;

        _EVL[idx] = nEVL;
        _EVL[idx].applies = eventApplies( _EVL[idx] );
    }

    reprocessEvents();
    checkAndReportStatus(true, "EVT");   // confirm receipt and publish updated schedule
    SFDBG::pub("EM", "new event processed");
    return (int)EMrc::SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
//  configScheduler()  –  Particle cloud function target
//  Accepts the same JSON field names as Gen2 (LAT, LNG, STD, DST, ZIP, FED,
//  STA, FPR, FLG).  Fields already live in ConfigData – this method updates
//  EEPROMManager and refreshes the local cache.
// ─────────────────────────────────────────────────────────────────────────────
int EventManager::configScheduler( String JSONconfig ) {

    JSONValue outerObj = JSONValue::parseCopy( JSONconfig.c_str() );
    JSONObjectIterator iter( outerObj );

    // Track whether jurisdiction fields change so we know if resetSubscriptions
    // is needed.  Other config changes (LAT, LNG, TZ, etc.) do not affect
    // subscription topics.
    String prevFed = _jurFederal;
    String prevSta = _jurState;

    while ( iter.next() ) {
        String field = String( iter.name() ).toUpperCase();

        if      ( field == "LAT" ) { _lat            = iter.value().toDouble();              }
        else if ( field == "LNG" ) { _lng             = iter.value().toDouble();              }
        else if ( field == "STD" ) { _tzOffset        = (float)iter.value().toDouble();       }
        else if ( field == "DST" ) { _doDST           = iter.value().toBool();                }
        else if ( field == "ZIP" ) { _postalCode       = String(iter.value().toString());      }
        else if ( field == "FED" ) { _jurFederal       = String(iter.value().toString());      }
        else if ( field == "STA" ) { _jurState         = String(iter.value().toString());      }
        else if ( field == "FPR" ) { _upperFlagPrio    = iter.value().toInt();                 }
        else if ( field == "FLG" ) { _upperFlag        = String(iter.value().toString());      }
        else if ( field == "SJR" ) {
            // Accept array form "SJR":[12,34] or scalar form "SJR":12
            _sjrCount = 0;
            memset( _sjrList, 0, sizeof(_sjrList) );
            JSONValue sjrVal = iter.value();
            if ( sjrVal.isArray() ) {
                JSONArrayIterator aIter( sjrVal );
                while ( aIter.next() && _sjrCount < 5 ) {
                    _sjrList[ _sjrCount++ ] = (uint16_t)aIter.value().toInt();
                }
            } else {
                int v = sjrVal.toInt();
                if ( v != 0 ) { _sjrList[0] = (uint16_t)v; _sjrCount = 1; }
            }
        }
        // All other recognised config fields (MOD, CRS, etc.) live in ConfigData
        // and are handled by EEPROMManager directly – not duplicated here.
    }

    _configured = true;

    // Re-subscribe if either jurisdiction string changed
    if ( _jurFederal != prevFed || _jurState != prevSta ) {
        resetSubscriptions();
    }

    // Persist the fields this function owns — read-modify-write to preserve
    // fields managed by other handlers (MOD, CRS, status_period_sec, etc.)
    {
        ConfigData cfg;
        readConfig( cfg );
        strncpy( cfg.FLG, _upperFlag.c_str(),    sizeof(cfg.FLG) - 1 );
        cfg.FPR = _upperFlagPrio;
        cfg.LAT = (float)_lat;
        cfg.LNG = (float)_lng;
        strncpy( cfg.FED, _jurFederal.c_str(),   sizeof(cfg.FED) - 1 );
        strncpy( cfg.STA, _jurState.c_str(),     sizeof(cfg.STA) - 1 );
        strncpy( cfg.ZIP, _postalCode.c_str(),   sizeof(cfg.ZIP) - 1 );
        cfg.STD = _tzOffset;
        cfg.DST = _doDST;
        writeConfig( cfg );
    }

    // Persist SJR list to ConfigExt (read-modify-write)
    {
        ConfigExt x;
        readConfigExt( x );
        if ( x.magic != CFGX_MAGIC || x.version != CFGX_VERSION ) {
            initConfigExt();
            readConfigExt( x );
        }
        x.sjrCount = (uint8_t)_sjrCount;
        for ( int i = 0; i < 5; i++ )
            x.sjrList[i] = ( i < _sjrCount ) ? _sjrList[i] : 0;
        writeConfigExt( x );
    }

    reprocessEvents();
    saveToEEPROM();

    SFDBG::pub("EM", "configScheduler complete");
    return (int)EMrc::SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
//  purgeEvents()  –  remove stale / invalid events
// ─────────────────────────────────────────────────────────────────────────────
int EventManager::purgeEvents() {
    int nPurged = 0;
    time_t now = Time.now();

    for ( int i = 0; i < N_EVENTS; i++ ) {
        if ( _EVL[i].eventID > 0 ) {
            bool expired  = ( _EVL[i].GMTend != 0 && _EVL[i].GMTend < now );
            bool noBegin  = ( _EVL[i].GMTbegin == 0 );
            if ( !_EVL[i].valid || noBegin || expired ) {
                SFDBG::pub("EM", "EVT " + String(_EVL[i].eventID) + " purged: "
                               + (expired ? "expired" : noBegin ? "no-begin" : "invalid"), true);
                clearEvent( _EVL[i] );
                nPurged++;
            }
        }
    }

    saveToEEPROM();
    return nPurged;
}

// ─────────────────────────────────────────────────────────────────────────────
//  clearEvent()
// ─────────────────────────────────────────────────────────────────────────────
void EventManager::clearEvent( FlagEventEx &ev ) {
    ev = FlagEventEx();  // reset to default-constructed state
}

// ─────────────────────────────────────────────────────────────────────────────
//  matchEVID()  –  return index of event with given ID, or -1
// ─────────────────────────────────────────────────────────────────────────────
int EventManager::matchEVID( int id ) {
    for ( int i = 0; i < N_EVENTS; i++ ) {
        if ( _EVL[i].eventID == id ) return i;
    }
    return -1;
}

// ─────────────────────────────────────────────────────────────────────────────
//  jurMatch()  –  prefix match, case-insensitive (verbatim from Gen2)
// ─────────────────────────────────────────────────────────────────────────────
bool EventManager::jurMatch( const String &pubJur, const String &subJur ) {
    return subJur.equalsIgnoreCase( pubJur.substring( 0, subJur.length() ) );
}

// ─────────────────────────────────────────────────────────────────────────────
//  eventApplies()  –  Gen2 logic PLUS Gen3 sub-jurisdiction filtering
// ─────────────────────────────────────────────────────────────────────────────
bool EventManager::eventApplies( const FlagEventEx &ev ) {
    // ── 1) Jurisdiction match (same as Gen2) ─────────────────────────────────
    bool jurOK = ( jurMatch(ev.eventJur, _jurFederal) ||
                   jurMatch(ev.eventJur, _jurState)     );
    if ( !jurOK ) {
        SFDBG::pub("EM", "EVT " + String(ev.eventID) + " jur-fail: pub=" + ev.eventJur
                        + " sub=" + _jurFederal + "/" + _jurState, true);
        return false;
    }

    // ── 2) Flag match (same as Gen2) ─────────────────────────────────────────
    bool flgOK = ( ev.eventFlag == String(FED_FLAG) ||
                   ev.eventFlag == _upperFlag         );
    if ( !flgOK ) {
        SFDBG::pub("EM", "EVT " + String(ev.eventID) + " flg-fail: " + ev.eventFlag
                        + " want=" + String(FED_FLAG) + "/" + _upperFlag, true);
        return false;
    }

    // ── 3) Sub-jurisdiction filtering (NEW – Gen3) ────────────────────────────
    //    If the event carries NO SJR list, it applies statewide – pass.
    //    If it carries a SJR list, the unit MUST have at least one configured SJR
    //    that matches one of the event's SJRs.  Units with no configured SJRs
    //    do NOT receive SJR-targeted events.
    if ( ev.sjrCount > 0 ) {
        bool sjrOK = false;
        for ( int i = 0; i < _sjrCount && !sjrOK; i++ ) {
            for ( int j = 0; j < ev.sjrCount && !sjrOK; j++ ) {
                if ( _sjrList[i] == (uint16_t)ev.sjrList[j] ) sjrOK = true;
            }
        }
        if ( !sjrOK ) {
            SFDBG::pub("EM", "EVT " + String(ev.eventID) + " sjr-fail: unitSJR=" + String(_sjrCount), true);
            return false;
        }
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  reprocessEvents()  –  re-evaluate all stored events (call after time-sync,
//                         DST change, or config change)
// ─────────────────────────────────────────────────────────────────────────────
void EventManager::reprocessEvents() {
    if ( !_configured ) return;

    for ( int idx = 0; idx < N_EVENTS; idx++ ) {
        if ( !_EVL[idx].valid ) {
            clearEvent( _EVL[idx] );
        } else if (
            parseTimeMark( _EVL[idx].GMTbegin, _EVL[idx].BMK, "H" ) != 0 ||
            parseTimeMark( _EVL[idx].GMTend,   _EVL[idx].EMK, "F" ) != 0   ) {
            SFDBG::pub("EM", "EVT " + String(_EVL[idx].eventID)
                            + " tmk-fail BMK=" + _EVL[idx].BMK, true);
            clearEvent( _EVL[idx] );   // time mark resolution failed – discard
        } else {
            _EVL[idx].applies = eventApplies( _EVL[idx] );
        }
    }

    setNextEvent();
    saveToEEPROM();
}

// ─────────────────────────────────────────────────────────────────────────────
//  setNextEvent()  –  compute _orderedSta / _nextChange / _nextSta
//                     (verbatim logic from Gen2, enums adapted)
// ─────────────────────────────────────────────────────────────────────────────
void EventManager::setNextEvent() {
    purgeEvents();   // removes stale events before computing schedule

    time_t now       = Time.now();
    time_t nullT     = now + 30L * 24 * 60 * 60;   // open-ended = now + 30 days
    time_t mxEnd     = now;
    time_t nxBeg     = nullT;
    bool   inProgress = false;
    bool   waiting    = false;

    time_t begs[N_EVENTS] = {0};
    time_t ends[N_EVENTS] = {0};

    for ( int idx = 0; idx < N_EVENTS; idx++ ) {
        if ( !_EVL[idx].applies ) continue;

        begs[idx] = _EVL[idx].GMTbegin;
        ends[idx] = ( _EVL[idx].GMTend == 0 ) ? nullT : _EVL[idx].GMTend;

        if ( begs[idx] > now ) {
            waiting = true;
            nxBeg = min( nxBeg, begs[idx] );
        } else if ( begs[idx] > 0 && ends[idx] > now ) {
            inProgress = true;
            mxEnd = max( mxEnd, ends[idx] );
        } else {
            clearEvent( _EVL[idx] );
        }
    }

    // Extend mxEnd to cover any event whose window overlaps the current mxEnd
    bool changed = true;
    while ( changed ) {
        changed = false;
        for ( int idx = 0; idx < N_EVENTS; idx++ ) {
            if ( !_EVL[idx].applies ) continue;
            if ( begs[idx] > now && begs[idx] <= mxEnd && ends[idx] > mxEnd ) {
                mxEnd   = ends[idx];
                changed = true;
            }
        }
    }

    if ( inProgress ) {
        _orderedSta  = FLAG_HALF;
        _nextChange  = mxEnd;
        _nextSta     = FLAG_FULL;
    } else if ( waiting ) {
        _orderedSta  = FLAG_FULL;
        _nextChange  = nxBeg;
        _nextSta     = FLAG_HALF;
    } else {
        _orderedSta  = FLAG_FULL;
        _nextChange  = 0;
        _nextSta     = FLAG_UNKNOWN;
    }

    if ( _setStationCB ) {
        _setStationCB( _orderedSta );
    }
    updEventTimer();
}

// ─────────────────────────────────────────────────────────────────────────────
//  updEventTimer()  –  schedule next attention check
//  Uses millis()-based software timer (no hardware Timer object in Gen3 design).
//  Fires at the sooner of:
//    a) 30 min before next sunrise
//    b) _nextChange
// ─────────────────────────────────────────────────────────────────────────────
void EventManager::updEventTimer() {
    _attentionFlag = false;

    time_t nowTime = Time.now();
    struct tm sunDay;
    sunDay = *gmtime( &nowTime );
    sunDay.tm_hour = 0;
    sunDay.tm_min  = 0;
    sunDay.tm_sec  = 0;

    time_t nextCheck;
    int sunErr = getSunrise( nextCheck, sunDay );
    nextCheck -= 30 * 60;   // 30 min before sunrise

    if ( sunErr || nextCheck < nowTime ) {
        // Already past sunrise – try tomorrow
        sunDay.tm_mday++;
        sunDay.tm_hour = 0;
        sunDay.tm_min  = 0;
        sunDay.tm_sec  = 0;
        mktime( &sunDay );   // normalize date (handles month rollover) and refresh tm_yday
        sunErr = getSunrise( nextCheck, sunDay );
        nextCheck -= 30 * 60;
    }
    if ( sunErr || nextCheck < nowTime ) {
        nextCheck = nowTime + 60 * 60;   // fallback: 60 minutes
    }

    // Use the earlier of sunrise-check and scheduled event change
    nextCheck = ( _nextChange == 0 ) ? nextCheck : min( nextCheck, _nextChange );

    _msUntilNext  = (unsigned long)( (nextCheck - nowTime) * 1000UL );
    _timerStartMs = millis();
}

// ─────────────────────────────────────────────────────────────────────────────
//  parseEvent()  –  decode incoming JSON event string into a FlagEventEx
// ─────────────────────────────────────────────────────────────────────────────
FlagEventEx EventManager::parseEvent( const String &json ) {
    FlagEventEx tEVL;
    tEVL.valid = true;
    tEVL.toSta = FLAG_HALF;   // default target station

    JSONValue outerObj = JSONValue::parseCopy( json.c_str() );
    JSONObjectIterator iter( outerObj );

    while ( iter.next() ) {
        String field = String( iter.name() ).toUpperCase();

        if ( field == "IDV" ) {
            String IDV  = String( iter.value().toString() );
            int    pLoc = IDV.indexOf('.');
            if ( pLoc < 0 ) { tEVL.valid = false; break; }
            tEVL.eventID  = IDV.substring(0, pLoc).toInt();
            if ( tEVL.eventID <= 0 ) { tEVL.valid = false; break; }
            tEVL.eventVer = IDV.substring(pLoc + 1).toInt();

        } else if ( field == "JUR" ) {
            tEVL.eventJur = String( iter.value().toString() );

        } else if ( field == "FLG" ) {
            tEVL.eventFlag = String( iter.value().toString() );

        } else if ( field == "BMK" ) {
            tEVL.BMK = String( iter.value().toString() );
            if ( parseTimeMark( tEVL.GMTbegin, tEVL.BMK, "H" ) != 0 ) {
                SFDBG::pub("EM", "EVT " + String(tEVL.eventID)
                                + " tmk-fail BMK=" + tEVL.BMK, true);
                tEVL.valid = false; break;
            }

        } else if ( field == "EMK" ) {
            tEVL.EMK = String( iter.value().toString() );
            if ( parseTimeMark( tEVL.GMTend, tEVL.EMK, "F" ) != 0 ) {
                SFDBG::pub("EM", "EVT " + String(tEVL.eventID)
                                + " tmk-fail EMK=" + tEVL.EMK, true);
                tEVL.valid = false; break;
            }

        } else if ( field == "DEL" ) {
            tEVL.isDelete = iter.value().toBool();

        } else if ( field == "SJR" ) {
            // ── Gen3 sub-jurisdiction list ────────────────────────────────────
            // Expected JSON format:  "SJR": [12, 34, 56]
            // JSONValue::isArray() check guards against scalar form.
            JSONValue sjrVal = iter.value();
            if ( sjrVal.isArray() ) {
                JSONArrayIterator aIter( sjrVal );
                while ( aIter.next() && tEVL.sjrCount < FlagEventEx::MAX_SJR ) {
                    tEVL.sjrList[ tEVL.sjrCount++ ] = aIter.value().toInt();
                }
            } else {
                // Single value form: "SJR": 12
                if ( tEVL.sjrCount < FlagEventEx::MAX_SJR ) {
                    tEVL.sjrList[ tEVL.sjrCount++ ] = sjrVal.toInt();
                }
            }
        }
    }

    if ( !tEVL.valid ) tEVL.eventID = 0;
    return tEVL;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Sun-time calculations (verbatim from Gen2)
// ─────────────────────────────────────────────────────────────────────────────

int EventManager::UTCSunEvent( float latitude, float longitude, int doy, int rs ) {
    float rd     = 57.295779513082322f;
    float lat    = latitude  / rd;
    float lon    = -longitude / rd;
    float zenith = 1.579522973054868f;

    unsigned char a = rs ? 6 : 18;
    float y    = 0.01721420632104f * (doy + a / 24.0f);
    float eqt  = 229.18f * ( 0.000075f + 0.001868f*cosf(y)  - 0.032077f*sinf(y)
                            - 0.014615f*cosf(y*2) - 0.040849f*sinf(y*2) );
    float decl = 0.006918f - 0.399912f*cosf(y) + 0.070257f*sinf(y)
               - 0.006758f*cosf(y*2) + 0.000907f*sinf(y*2)
               - 0.002697f*cosf(y*3) + 0.00148f*sinf(y*3);
    float ha   = cosf(zenith) / (cosf(lat)*cosf(decl)) - tanf(lat)*tanf(decl);

    if ( fabsf(ha) <= 1.0f ) {
        if ( rs ) {
            return (int)( 60.0f * (720.0f + 4.0f*(lon - acosf(ha))*rd - eqt) );
        } else {
            return (int)( 60.0f * (720.0f + 4.0f*(lon + acosf(ha))*rd - eqt) );
        }
    }
    return 86401;   // polar condition – no sunrise/sunset (sentinel > 86400 s/day)
}

int EventManager::getSunTime( time_t &tVal, struct tm &sTime, int srss ) {
    int t = UTCSunEvent( (float)_lat, (float)_lng, sTime.tm_yday, srss );
    tVal = 0;
    if ( t < 0 || t > 86400 ) return (int)EMrc::BAD_SUNEVENT;   // polar or negative (wrong-sign longitude)
    sTime.tm_sec  = t;        // t is seconds past midnight UTC
    tVal = mktime( &sTime );
    return (int)EMrc::SUCCESS;
}

int EventManager::getSunrise( time_t &tVal, struct tm &date ) {
    return getSunTime( tVal, date, 1 );
}

int EventManager::getSunset( time_t &tVal, struct tm &date ) {
    return getSunTime( tVal, date, 0 );
}

int EventManager::getZTime( time_t &tVal, struct tm &sTime, const String &hhmm ) {
    sTime.tm_hour = atoi( hhmm.substring(0, 2).c_str() );
    sTime.tm_min  = atoi( hhmm.substring(3, 5).c_str() );
    tVal = mktime( &sTime );
    return (int)EMrc::SUCCESS;
}

int EventManager::getLTime( time_t &tVal, struct tm &sTime, const String &hhmm ) {
    int rc = getZTime( tVal, sTime, hhmm );
    if ( rc == (int)EMrc::SUCCESS ) {
        // TZOffset is negative for west longitudes; subtracting a negative offset adds
        time_t secOffset = -(time_t)( _tzOffset * 3600.0f );
        if ( _doDST && isDST(sTime.tm_mday, sTime.tm_mon + 1, sTime.tm_wday) ) {
            secOffset -= 3600;
        }
        tVal += secOffset;
    }
    return rc;
}

int EventManager::parseTimeMark( time_t &tVal, const String &mTime, const String &mDest ) {
    if ( mTime == "TBD" ) { tVal = 0; return (int)EMrc::SUCCESS; }
    if ( mTime.length() < 10 ) return (int)EMrc::PARSE_ERROR;

    struct tm myTime;
    memset( &myTime, 0, sizeof(myTime) );
    myTime.tm_year = atoi( mTime.substring(0, 4).c_str() ) - 1900;
    myTime.tm_mon  = atoi( mTime.substring(5, 7).c_str() ) - 1;
    myTime.tm_mday = atoi( mTime.substring(8, 10).c_str() );
    mktime( &myTime );   // fills tm_yday, needed for sun calculations

    switch ( mTime.length() ) {
        case 10:   // date only – sunrise or sunset by mDest
            switch ( mDest.charAt(0) ) {
                case 'H': return getSunrise( tVal, myTime );
                case 'F': return getSunset ( tVal, myTime );
                default:  return (int)EMrc::NOT_HF;
            }

        case 13:   // date + SR or SS
            if ( mTime.substring(11,13) == "SR" ) return getSunrise( tVal, myTime );
            if ( mTime.substring(11,13) == "SS" ) return getSunset ( tVal, myTime );
            return (int)EMrc::NOT_SRSS;

        case 16:   // date + HH:MM (GMT default)
            return getZTime( tVal, myTime, mTime.substring(11,16) );

        case 17: { // date + HH:MM + Z or L
            char ref = mTime.charAt(16);
            if ( ref == 'Z' ) return getZTime( tVal, myTime, mTime.substring(11,16) );
            if ( ref == 'L' ) return getLTime( tVal, myTime, mTime.substring(11,16) );
            return (int)EMrc::NOT_ZL;
        }
    }
    return (int)EMrc::SUCCESS;
}

bool EventManager::isDST( int dayOfMonth, int month, int dayOfWeek ) {
    if ( month < 3 || month > 11 ) return false;
    if ( month > 3 && month < 11 ) return true;
    int previousSunday = dayOfMonth - dayOfWeek;
    if ( month == 3 ) return previousSunday >= 8;
    return previousSunday <= 0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Display helpers
// ─────────────────────────────────────────────────────────────────────────────
String EventManager::staToLetter( FlagStation sta ) {
    switch ( sta ) {
        case FLAG_FULL:    return "F";
        case FLAG_HALF:    return "H";
        case FLAG_STOP:    return "S";
        case FLAG_UNKNOWN: return "?";
        default:           return "?";
    }
}

String EventManager::showConfig() {
    char buf[JSON_BUF];
    memset( buf, 0, sizeof(buf) );
    JSONBufferWriter writer( buf, sizeof(buf) - 1 );

    writer.beginObject();
        writer.name("LAT").value( _lat,    6 );
        writer.name("LNG").value( _lng,    6 );
        writer.name("STD").value( _tzOffset, 1 );
        writer.name("DST").value( _doDST      );
        writer.name("ZIP").value( _postalCode  );
        writer.name("FED").value( _jurFederal  );
        writer.name("STA").value( _jurState    );
        writer.name("FPR").value( _upperFlagPrio );
        writer.name("FLG").value( _upperFlag   );
        writer.name("SJR").beginArray();
        for ( int i = 0; i < _sjrCount; i++ ) writer.value( (int)_sjrList[i] );
        writer.endArray();
    writer.endObject();

    return String(buf);
}

String EventManager::showEvent( const FlagEventEx &ev ) {
    char buf[JSON_BUF];
    memset( buf, 0, sizeof(buf) );
    JSONBufferWriter writer( buf, sizeof(buf) - 1 );

    writer.beginObject();
        writer.name("APP").value( ev.applies  );
        writer.name("VLD").value( ev.valid    );
        writer.name("IDV").value( String::format("%d.%d", ev.eventID, ev.eventVer) );
        if ( ev.eventID > 0 ) {
            if ( ev.GMTbegin > 0 ) writer.name("BMK").value( Time.format(ev.GMTbegin) );
            else                   writer.name("BMK").nullValue();
            writer.name("JUR").value( ev.eventJur  );
            writer.name("FLG").value( ev.eventFlag );
            if ( ev.GMTend > 0 )   writer.name("EMK").value( Time.format(ev.GMTend) );
            else                   writer.name("EMK").nullValue();
            writer.name("STA").value( staToLetter(ev.toSta) );
            // SJR list
            if ( ev.sjrCount > 0 ) {
                writer.name("SJR").beginArray();
                for ( int i = 0; i < ev.sjrCount; i++ ) writer.value( ev.sjrList[i] );
                writer.endArray();
            }
        }
    writer.endObject();

    return String(buf);
}

String EventManager::showEventList() {
    char buf[JSON_BUF];
    memset( buf, 0, sizeof(buf) );
    JSONBufferWriter writer( buf, sizeof(buf) - 1 );

    writer.beginObject();
        writer.name("EVL").beginArray();
        for ( int idx = 0; idx < N_EVENTS; idx++ ) {
            if ( _EVL[idx].eventID > 0 && _EVL[idx].valid ) {
                writer.value( String::format("%d.%d", _EVL[idx].eventID, _EVL[idx].eventVer) );
            } else {
                writer.nullValue();
            }
        }
        writer.endArray();
    writer.endObject();

    return String(buf);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Ring-buffer event inspector
// ─────────────────────────────────────────────────────────────────────────────

int EventManager::setShowIdx( int idx ) {
    if ( idx < 0 )        idx = 0;
    if ( idx >= N_EVENTS ) idx = N_EVENTS - 1;
    _showIdx = idx;
    return _showIdx;
}

String EventManager::showEventAtCursor() {
    String result = showEvent( _EVL[_showIdx] );

    // Advance to the next valid event slot in ring fashion.
    // "Valid" means eventID > 0 and valid flag set.
    // Start search one past current; wrap all the way around.
    int next = -1;
    for ( int i = 1; i <= N_EVENTS; i++ ) {
        int candidate = (_showIdx + i) % N_EVENTS;
        if ( _EVL[candidate].valid && _EVL[candidate].eventID > 0 ) {
            next = candidate;
            break;
        }
    }

    _showIdx = (next >= 0) ? next : 0;   // no valid events → reset to slot 0

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Query helpers
// ─────────────────────────────────────────────────────────────────────────────
int EventManager::getNEvents() {
    int n = 0;
    for ( int idx = 0; idx < N_EVENTS; idx++ ) {
        if ( _EVL[idx].valid && _EVL[idx].applies ) n++;
    }
    return n;
}

int EventManager::firstActiveEvent() {
    time_t now = Time.now();
    for ( int idx = 0; idx < N_EVENTS; idx++ ) {
        if ( !_EVL[idx].valid   ) continue;
        if ( !_EVL[idx].applies ) continue;
        if ( _EVL[idx].GMTbegin == 0        ) continue;
        if ( _EVL[idx].GMTbegin > now       ) continue;
        if ( _EVL[idx].GMTend   == 0        ) return _EVL[idx].eventID;  // open-ended = active
        if ( _EVL[idx].GMTend   > now       ) return _EVL[idx].eventID;
    }
    return -1;
}

// ─────────────────────────────────────────────────────────────────────────────
//  EEPROM helpers
// ─────────────────────────────────────────────────────────────────────────────

//  loadConfig()  –  pull configuration from EEPROMManager into local cache
//  Called at setup() and can be re-called after an external config change.
void EventManager::loadConfig() {
    ConfigData cfg;
    readConfig( cfg );
    _upperFlag     = String( cfg.FLG );
    _upperFlagPrio = cfg.FPR;
    _lat           = cfg.LAT;
    _lng           = cfg.LNG;
    _jurFederal    = String( cfg.FED );
    _jurState      = String( cfg.STA );
    _postalCode    = String( cfg.ZIP );
    _tzOffset      = cfg.STD;
    _doDST         = cfg.DST;

    // Load unit SJR list from ConfigExt
    ConfigExt x;
    readConfigExt( x );
    _sjrCount = 0;
    memset( _sjrList, 0, sizeof(_sjrList) );
    if ( x.magic == CFGX_MAGIC && x.version == CFGX_VERSION ) {
        int n = min( (int)x.sjrCount, 5 );
        for ( int i = 0; i < n; i++ ) _sjrList[i] = x.sjrList[i];
        _sjrCount = n;
    }
}

//  loadFromEEPROM()  –  restore saved event list
//
//  FlagEvent persists: idv, flg, bmk, emk, jur, sjrCount, sjrList[].
//  All fields required by eventApplies() survive reboots intact.
int EventManager::loadFromEEPROM() {
    EventHeader hdr;
    readEventHeader( hdr );

    if ( hdr.eventCount == 0 || hdr.eventCount > (uint8_t)N_EVENTS ) return 0;

    for ( int i = 0; i < N_EVENTS; i++ ) {
        _EVL[i] = FlagEventEx();   // clear first

        FlagEvent stored;
        if ( !readEvent( i, stored ) ) continue;
        if ( stored.idv[0] == '\0'  ) continue;   // empty slot

        String idvStr = String( stored.idv );
        int pLoc = idvStr.indexOf('.');
        if ( pLoc < 0 ) continue;

        int evID = idvStr.substring(0, pLoc).toInt();
        if ( evID <= 0 ) continue;

        _EVL[i].valid      = true;
        _EVL[i].eventID    = evID;
        _EVL[i].eventVer   = idvStr.substring(pLoc + 1).toInt();
        _EVL[i].eventFlag  = String( stored.flg );
        _EVL[i].BMK        = String( stored.bmk );
        _EVL[i].EMK        = String( stored.emk );
        _EVL[i].eventJur   = String( stored.jur );

        // Restore sub-jurisdiction list
        int nSjr = min( (int)stored.sjrCount, FlagEventEx::MAX_SJR );
        _EVL[i].sjrCount = nSjr;
        for ( int j = 0; j < nSjr; j++ ) {
            _EVL[i].sjrList[j] = (int)stored.sjrList[j];
        }
    }

    return 0;
}

//  saveToEEPROM()  –  persist current event list
//
//  Stores all FlagEvent fields: idv, flg, bmk, emk, jur, sjrCount, sjrList[].
//  Up to 7 SJR entries are stored (uint16_t each); entries beyond 7 are dropped
//  (no real-world event is expected to carry more than 7 sub-jurisdictions).
int EventManager::saveToEEPROM() {
    int count = 0;

    for ( int i = 0; i < N_EVENTS; i++ ) {
        FlagEvent stored;
        memset( &stored, 0, sizeof(stored) );

        if ( _EVL[i].valid && _EVL[i].eventID > 0 ) {
            snprintf( stored.idv, sizeof(stored.idv), "%d.%d",
                      _EVL[i].eventID, _EVL[i].eventVer );
            strncpy( stored.flg, _EVL[i].eventFlag.c_str(), sizeof(stored.flg) - 1 );
            strncpy( stored.bmk, _EVL[i].BMK.c_str(),       sizeof(stored.bmk) - 1 );
            strncpy( stored.emk, _EVL[i].EMK.c_str(),       sizeof(stored.emk) - 1 );
            strncpy( stored.jur, _EVL[i].eventJur.c_str(),  sizeof(stored.jur) - 1 );

            // Persist sub-jurisdiction list (clamped to 7 entries)
            int nSjr = min( _EVL[i].sjrCount, 7 );
            stored.sjrCount = (uint8_t)nSjr;
            for ( int j = 0; j < nSjr; j++ ) {
                stored.sjrList[j] = (uint16_t)_EVL[i].sjrList[j];
            }

            stored.deleted = 0;
            count++;
        }

        writeEvent( i, stored );
    }

    EventHeader hdr;
    memset( &hdr, 0, sizeof(hdr) );
    hdr.eventCount = (uint8_t)count;
    writeEventHeader( hdr );

    return 0;
}
