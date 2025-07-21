#include "EventManager.h"
#include <ctime>
#include <cstring>
#include <cmath>
#include <cstdio>

/********************************************************
 *    EventManager Methods - code
 ********************************************************/

EventManager::EventManager() {
    changeTimer = new Timer( 100000, &EventManager::eventTimer, *this, true );     // one-shot timer - reschedules itself
    changeTimer->stop();
}

EventManager::~EventManager() {
    delete changeTimer;
}

void EventManager::eventTimer() {
    _attentionFlag = true;
}

void EventManager::begin() {
    _conf_Sched = ( getEE() == 0 );  // load saved values (if legit, set _conf_Sched to true - have config)
    resetSubscriptions();
    reprocessEvents();              // determine if events still apply and 


    Particle.variable ( "s_EventLIST" , [this](){ return this->showEventList (); } );
    Particle.variable ( "s_ShowConfig"  , [this](){ return this->showConfig(); } );
    Particle.function ( "s_Config" , &EventManager::configScheduler, this );
    Particle.function ( "s_Event" , &EventManager::receiveEvent, this );
}


int EventManager::matchEVID ( int IDtoMatch ){
    for ( int i=0; i<nEvents; i++) {
        if ( EVL[i].eventID == IDtoMatch ) return i;   // return the matching index
    }
    return -1;      // -1 indicates no match
}

bool EventManager::jurMatch ( String pubJur, String subJur) {
/*
 *  Comparing jurisdictions - first truncate the PUBlished value, then 
 *     compare the result to the SUBscribed value (NOT case sensitive)
 */
    return subJur.equalsIgnoreCase(pubJur.substring(0,subJur.length()));
}

bool EventManager::eventApplies ( flagEvent aEVL ){               // compare aEVL attributes to unit config - return false if not applicable
/*
 *  Application standards:
 *      1) EVL.eventJur <matches> jurFederal -or- jurState (see jurMatch)
 *      1a)<<subJur processing, when implemented>>
 *  AND 2) EVL.eventFlag = "US" -or- EVL.eventFlag = upperFlag (<ST>))
 */
    return ( ( jurMatch (aEVL.eventJur, jurFederal)  || jurMatch(aEVL.eventJur, jurState)  ) &&    // jurisdiction must match federal or state
             ( aEVL.eventFlag == federalFlag         || aEVL.eventFlag == upperFlag        ) );    // flag must be US or upper flag on this halyard
}


void EventManager::setNextEvent () {          // go through any/all posted flag events and update pending event parameters
/*
 *  Values to be set:
 *      FlagStation orderedSta        station where the flag should currently be placed (in auto mode)
 *      time_t nextChange       epoch time (GMT) for next ordered flag movement (if 0, none scheduled)
 *      FlagStation nextSta           station for next ordered movement (if FlagStation::NUL, none scheduled)
 */

    purgeEvents();                                      // routine purge to eliminate old/invalid events
    
    time_t now = Time.now();
    bool inProgress = false;
    bool waiting = false;

    time_t nullT = now + 30*24*60*60;                   // null end time is now + 30days (in seconds)
    time_t mxEnd = now;
    time_t nxBeg = nullT;
    time_t begs[nEvents] = { 0 };
    time_t ends[nEvents] = { 0 };

    for ( int idx = 0; idx < nEvents; idx++ ) {
        if ( EVL[idx].applies ) {
            begs[idx] = EVL[idx].GMTbegin;
            ends[idx] = ( EVL[idx].GMTend == 0 ) ? nullT : EVL[idx].GMTend ;

            if ( begs[idx] > now ) {                    // future
                waiting = true;
                nxBeg = min(nxBeg , begs[idx]);
            } else if ( begs[idx] > 0 && ends[idx] > now ) {               // current
                inProgress = true;
                mxEnd = max( mxEnd , ( EVL[idx].GMTend == 0 ) ? nullT : EVL[idx].GMTend );
            } else {
                clearEvent( EVL[idx] );
            }
        }
    }

    bool changed = true;

    while ( changed ) {
        changed = false;
        for ( int idx=0; idx < nEvents; idx++ ) {
            if ( EVL[idx].applies ) {
                if ( begs[idx] > now && 
                    begs[idx] <= mxEnd &&
                    ends[idx] > mxEnd ) {
                    mxEnd = ends[idx];
                    changed = true;
                }  
            }
        }
    }

    if ( inProgress ) {
        orderedSta = FLAG_HALF;
        nextChange = mxEnd;
        nextSta = FLAG_FULL;
    } else if ( waiting ) {
        orderedSta = FLAG_FULL;
        nextChange = nxBeg;
        nextSta = FLAG_HALF;
    } else {
        orderedSta = FLAG_FULL;
        nextChange = 0;
        nextSta = FLAG_UNKNOWN;
    }

    halMgr1.setOrderedStation(orderedSta);
    updEventTimer();

    return;
}

void EventManager::updEventTimer () {

/* 
*   updEventTimer() - ensures flag is positioned properly and updates timer setting for next check
*
*      - tell the Director to put (keep) the flag at orderedSta
*      - Reschedule timer for the sooner of:
*          1) nextChange
*          2) 30 min before the next sunrise (if no sunrise, then 7:30am local)
*      - TSR is eventTimer  (sets "needsAttention" flag, which makes loop() call reprocessEvents)
*
*/
    _attentionFlag = false;

// rescheduling timer for next check

    time_t nextCheck;
    time_t nowTime = Time.now();            // gets GMT from Particle
    struct tm sunDay;
    sunDay = *localtime( &nowTime );        // gets "local" time, but it's GMT anyway
    sunDay.tm_hour = 0;                     // zero out the time component
    sunDay.tm_min = 0;
    sunDay.tm_sec = 0;

    int sunErr = getSunrise( nextCheck , sunDay );    // find sunrise associated with this day
    nextCheck -= 30*60;                   // 30 min before sunrise (allows time to re-evaluate)

    if ( sunErr || nextCheck < nowTime ) {            // problem?  already past sunrise today?
        sunDay.tm_mday++;                   // repeat process for tomorrow
        sunDay.tm_hour = 0;
        sunDay.tm_min = 0;
        sunDay.tm_sec = 0;
        sunErr = getSunrise( nextCheck , sunDay );    // tomorrow's sunrise?
        nextCheck -= 30*60;                   // 30 min before sunrise (allows time to re-evaluate)
    }
    if ( sunErr || nextCheck < nowTime ) nextCheck = nowTime + 60*60;       // if both sunrises fail, use 60 min

// Now we either have a good sunrise (less 30 min.) or one hour.
// delay time is from now until minimum of nextCheck or nextChange

    nextCheck = ( nextChange == 0 ? nextCheck : min(nextCheck, nextChange) ) ;
    msUntilNext = (nextCheck - nowTime)*1000;
    changeTimer->changePeriod(msUntilNext);

}

String EventManager::staToLetter ( FlagStation station ) {
    switch ( station ) {
        case FLAG_FULL : return "F";
        case FLAG_HALF : return "H";
        default        : return "?";
    }
}

int EventManager::configScheduler( String JSONconfig ){       // set configuration attributes from JSON (see code for details)
/*
 *  parse JSON string into configuration values and set the configuration
 */
    JSONValue outerObj = JSONValue::parseCopy(JSONconfig.c_str());
    JSONObjectIterator iter(outerObj);

    while(iter.next()) {

        String field = String(iter.name()).toUpperCase();

        if        ( field == "LAT") { SFlat                 = iter.value().toDouble();             // latitude
        } else if ( field == "LNG") { SFlong                = iter.value().toDouble();             // longitude
        } else if ( field == "STD") { TZOffset              = iter.value().toDouble();             // time zone offset (hrs.)
        } else if ( field == "DST") { doDST                 = iter.value().toBool();               // DST applies?
        } else if ( field == "ZIP") { postalCode            = String(iter.value().toString());     // Zip/postal code
        } else if ( field == "FED") { jurFederal            = String(iter.value().toString());     // Federal jurisdiction
        } else if ( field == "STA") { jurState              = String(iter.value().toString());     // State jurisdiction
        // } else if ( field == "SJR") { load jurSubs array
        } else if ( field == "FPR") { upperFlagPrio         = iter.value().toInt();                // Topmost flag's priority
        } else if ( field == "FLG") { upperFlag             = String(iter.value().toString());     // Topmost flag (abbr.)
        }
    }
    _conf_Sched = true;
    reprocessEvents();          // any time we reconfigure, we must reprocess events

    putEE();
    resetSubscriptions();
    /** SEND A STATUS REPORT */
    // myLink.status("Configured");

    return 0;
}

int EventManager::purgeEvents (){
    int nPurged = 0;
    time_t now = Time.now();

    for ( int i=0; i<nEvents; i++) {
        if ( EVL[i].eventID > 0 ) {            // slot holds event, candidates are either:
            if  (  !EVL[i].valid                //      1) not valid,
                || EVL[i].GMTbegin == 0         // -or- 2) have no defined beginning,
                || ( ( EVL[i].GMTend != 0  ) && 
                     ( EVL[i].GMTend < now )  ) ) { // -or- 3) have a defined end that's in the past
                clearEvent(EVL[i]);             // CLEAR IT OUT!
                nPurged++;                      // keep count for return value
            }
        }
    }

    putEE();

    return nPurged;   // return the number of events cleared
}

void EventManager::clearEvent ( flagEvent &cEVL ){
    flagEvent tEVL = {0};             // initialized with NUL values
    cEVL = tEVL;                // maybe use *cEVL, not cEVL ??
}

void EventManager::reprocessEvents () {

    if ( !isConfig() ) return;

    for ( int idx=0; idx<nEvents; idx++ ) {
/*
    Re-evaluate BMK and EMK (lat/long and TZ information may change)
        and see if the event still applies (based on jurisdiction or flag changes)
    Keep events that are valid, but don't apply - config can change that.
*/            
        if ( !EVL[idx].valid ) {
            clearEvent ( EVL[idx] );        // if invalid -or- time marks fail, event is no good
        } else if (
            parseTimeMark ( EVL[idx].GMTbegin , EVL[idx].BMK , "H" ) ||
            parseTimeMark ( EVL[idx].GMTend   , EVL[idx].EMK , "F" )    ) {
            clearEvent ( EVL[idx] );        // if invalid -or- time marks fail, event is no good
        } else {
            EVL[idx].applies = eventApplies( EVL[idx] );    // otherwise, check application
        }
    }

    setNextEvent();

    putEE();

    return;
}

String EventManager::showConfig (){   // return current configuration parameters in JSON format

    char buf[ JSONbufSize ];
    memset(buf, 0, sizeof(buf));
    JSONBufferWriter writer(buf, sizeof(buf) - 1);

    writer.beginObject();
        writer.name( "LAT").value(SFlat,6);
        writer.name( "LNG").value(SFlong,6);
        writer.name( "STD").value(TZOffset,1);
        writer.name( "DST").value(doDST);
        writer.name( "ZIP").value(postalCode);
        writer.name( "FED").value(jurFederal);
        writer.name( "STA").value(jurState);
        writer.name( "FPR").value(upperFlagPrio);
        writer.name( "FLG").value(upperFlag);
    writer.endObject();

    return String(buf);
}

String EventManager::showEvent ( flagEvent sEVL ){
    
    char buf[ JSONbufSize ];
    memset(buf, 0, sizeof(buf));
    JSONBufferWriter writer(buf, sizeof(buf) - 1);

    writer.beginObject();
        writer.name( "APP").value(sEVL.applies);
        writer.name( "VLD").value(sEVL.valid);
        writer.name( "IDV").value(String::format("%d.%d", sEVL.eventID, sEVL.eventVer));
        if ( sEVL.eventID >= 0 ) {                                      // 0 ID is empty (null) event
            if ( sEVL.GMTbegin > 0 ) {
                writer.name( "BMK").value(Time.format(sEVL.GMTbegin));
            } else {
                writer.name( "BMK").nullValue();
            }
            writer.name( "JUR").value(sEVL.eventJur);
            writer.name( "FLG").value(sEVL.eventFlag);
            if ( sEVL.GMTend > 0 ) {
                writer.name( "EMK").value(Time.format(sEVL.GMTend));
            } else {
                writer.name( "EMK").nullValue();
            }
        }
    writer.endObject();

    return String(buf);
}

String EventManager::showEventList ( ) {
    char buf[ JSONbufSize ];
    memset(buf, 0, sizeof(buf));
    JSONBufferWriter writer(buf, sizeof(buf) - 1);

    writer.beginObject();
        writer.name( "EVL").beginArray();
        for ( int idx=0; idx<nEvents; idx++ ) {
            if ( EVL[idx].eventID > 0 && EVL[idx].valid ) {
                writer.value(String::format("%d.%d", EVL[idx].eventID, EVL[idx].eventVer));
            } else {
                writer.nullValue();
            }
        }
        writer.endArray();
    writer.endObject();
    return String(buf);
}

int EventManager::receiveEvent ( String JSONFlagEvent ){          // 0 is success, all other values are error code
    flagEvent nEVL = parseEvent ( JSONFlagEvent );
    if ( nEVL.eventID <= 0 || !nEVL.valid ) return ERC::PARSE_ERROR;                // something wrong with JSON event

    int idx = matchEVID ( nEVL.eventID );
    if (  idx >= 0 ) {
// matches existing event, process as update (or delete) 
        
        if ( nEVL.eventVer < EVL[idx].eventVer ) return ERC::SUCCESS;     // IGNORE, because new event is lower version than existing
        if ( nEVL.isDelete ) {
// clear the stored EVL entry 
            clearEvent ( EVL[idx] );
        } else {
            EVL[idx] = nEVL;                         // copy new values into old event 
            EVL[idx].applies = eventApplies( EVL[idx] );
        }
    } else {
// does not match existing event, process as add (or delete)
        if ( nEVL.isDelete ) return ERC::SUCCESS;  // deleting with no match - just ignore
        
// << list processing - search list for space, don't just check "EVL" >>    
        purgeEvents();           // routinely purge old/invalid events
        idx = matchEVID ( 0 );                  // choose first EVL with ID=0 as new slot
        if ( idx < 0 ) return ERC::EVL_OVERFLOW;       // overflow - no room to store new event
        EVL[idx] = nEVL;
        EVL[idx].applies = eventApplies( EVL[idx] );
    }
    reprocessEvents();
    /** Send NEW EVENT status report */
    // myLink.status("New Event");
    return ERC::SUCCESS;
}

flagEvent EventManager::parseEvent( String JSONFlagEvent ){  // parse a flag event JSON before processing the data

    flagEvent tEVL;                             // initialized with NUL values
    tEVL.valid = true;                          // used to signal parsing errors
    
// first, decode the JSON fields

    JSONValue outerObj = JSONValue::parseCopy(JSONFlagEvent.c_str());
    JSONObjectIterator iter(outerObj);

    while(iter.next()) {

        String field = String(iter.name()).toUpperCase();
    
        if        ( field == "IDV") {       // Split IDV into 
            String IDV = String(iter.value().toString());                   //bring in as string, then split at period
            int pLoc = IDV.indexOf( '.' );
            if ( pLoc < 0 ) { tEVL.valid = false; break; }               // Bad format IDV (must have period)
            tEVL.eventID = IDV.substring(0,pLoc).toInt();           // main event ID number
            if ( tEVL.eventID <= 0 )  { tEVL.valid = false; break; }     // if eventID is bad (=0), quit parsing
            tEVL.eventVer = IDV.substring(pLoc+1).toInt();          // event version (don't update with earlier versions)
        } else if ( field == "JUR") { 
            tEVL.eventJur = String(iter.value().toString());              // used for comparing with device jurisdiction(s)
        // } else if ( field == "SJR") { << need code to parse incoming SJR(s) >> }     // SJRs are not in use right now
        } else if ( field == "FLG") {                               
            tEVL.eventFlag = String(iter.value().toString());           // most senior affected flag ("US" "st" "OT")
        } else if ( field == "BMK") { 
            tEVL.BMK = String(iter.value().toString());     //capture, then process beginning mark
            if ( parseTimeMark ( tEVL.GMTbegin , tEVL.BMK , "H" ) != 0 ) { tEVL.valid = false; break; }
        } else if ( field == "EMK") { 
            tEVL.EMK = String(iter.value().toString());     //capture, then process beginning mark
            if ( parseTimeMark ( tEVL.GMTend , tEVL.EMK , "F" ) != 0 ) { tEVL.valid = false; break; }
        } else if ( field == "DEL") { 
            tEVL.isDelete = iter.value().toBool();          // mark for deletion?
        }
    }

    if ( !tEVL.valid ) tEVL.eventID = 0;     // eventID of zero is error condition
    return tEVL;                        // send the data back in the return value
}

int EventManager::UTCSunEvent(float latitude, float longitude, int doy, int rs) {
/*
* UTCSunEvent is derived from the Sunrise library in Particle, but stripped down and with some minor changes
*   to address using UTC instead of local time zone.
* 
*   latitude = latitude of the event (positive is North), decimal format (e.g. Kingsport, TN is 36.5484)
*   longitude = longitude of the event (positive is East), decimal format (e.g. Kingsport, TN is -82.5618)
*   month = Jan is 1, Dec is 12
*   day = day of the month (1-31)
*   rs = sunset (0) or sunrise (any non-zero value)
*/
    float rd=57.295779513082322;    // 360/2pi - degrees to radians converter
    float lat=latitude/rd;          // lat in radians
    float lon=-longitude/rd;        // long in radians
    float zenith=1.579522973054868; // this is the zenith for ACTUAL sun time (vs. nautical, civil, etc.)

    // int doy=(month-1)*30.3+day-1;                  // approximate day of year (0-365)
    unsigned char a;
    if ( rs ) a=6; else a=18;                  // approximate 6:00am for sunrise, 6:00pm for sunset 
    float y=0.01721420632104*(doy+a/24);           // compute fractional year (as radians) - factor is 2pi/365
    float eqt=229.18 * (0.000075+0.001868*cos(y)  -0.032077*sin(y) -0.014615*cos(y*2) -0.040849*sin(y* 2) );          // compute equation of time
    float decl=0.006918-0.399912*cos(y)+0.070257*sin(y)-0.006758*cos(y*2)+0.000907*sin(y*2)-0.002697*cos(y*3)+0.00148*sin(y*3);       // compute solar declination
    float ha=(  cos(zenith) / (cos(lat)*cos(decl)) -tan(lat) * tan(decl)  );          //compute hour angle

    if(fabs(ha)<=1) {                               //  "ha" must be <=1; means sunrise/set actually occurs - think polar regions
        if ( rs ) {                            		// SunRISE...
            // return 720+4*(lon-acos(ha))*rd-eqt;     //   ...so SUBTRACT the "hour angle" component from solar noon
            return int(60.0*(720.0+4.0*(lon-acos(ha))*rd-eqt));     //   ...so SUBTRACT the "hour angle" component from solar noon
        } else    {				                    // SunSET...
            // return 720+4*(lon+acos(ha))*rd-eqt;     //   ...so ADD the "hour angle" component to solar noon
            return int(60.0*(720.0+4.0*(lon+acos(ha))*rd-eqt));     //   ...so ADD the "hour angle" component to solar noon
        } 
    }
    // no sunrise/set today...so return a big number! (>2160 is not credible)
    return 10000;

}

/** **************************** TIME STUFF - START ********************************************************** */

// int EventManager::parseTimeMark ( time_t &tVal , String mTime , String mDest ){

// /*  Allowable Time Mark (mTime) formats:
//  *
//  *  FMT#    Format          Len     Description
//  *  ====  ================  ===     =======================================    
//  *   #1   YYYY-MM-DD         10     date alone - use sunrise (if HF="H") or sunset (if HF="F")
//  *   #2   YYYY-MM-DD SR      13     sunrise on specified date
//  *   #3   YYYY-MM-DD SS      13     sunset on specified date
//  *   #4   YYYY-MM-DD HH:MM   16     hour and minute (defaults to GMT)
//  *   #5   YYYY-MM-DD HH:MMZ  17     hour and minute (explicit GMT)
//  *   #6   YYYY-MM-DD HH:MML  17     hour and minute (local time - uses DST if applicable)
//  * 
//  *  mDest must be H or F, but it is only validated when passing only the date (format #1)
//  */
//     if ( mTime == "TBD" ) { tVal = 0; return ERC::SUCCESS; }  // TBD just returns 0 for time

//     struct std::tm myTime = {0};                           // must set initial values to zero

// // Always parse the date - consider try-catch block to handle bad conversions

//     myTime.tm_year=atoi(mTime.substring(0, 4))-1900;       // YYYY Years (convert to since 1900)
//     myTime.tm_mon=atoi(mTime.substring(5, 7).c_str())-1;   // MM   Month (Jan = 0)
//     myTime.tm_mday=atoi(mTime.substring(8, 10).c_str());   // DD   Day

//     mktime ( &myTime );                                    // initial run to set tm_doy (needed for sun times)

//     switch ( mTime.length() ) {           // different formats are distinguished initially by length of the string

//         case 10:        // Fmt#1: Only date is provided - calculate sunrise (H) or sunset (F) using date/location
//             switch ( mDest.charAt(0) ) {
//                 case 'H':
//                     return getSunrise ( tVal , myTime );           // by default, flags go to HALF at sunrise
//                 case 'F':
//                     return getSunset ( tVal , myTime );            // by default, flags go to FULL at sunset
//                 default:
//                     return ERC::NOT_HF;                               // 2 means HF is not valid (must be "H" or "F")
//             }

//         case 13:         // Fmt#2 and #3: Must have "SR" or "SS" appended
//             if ( mTime.substring(11,13) == "SR" ) return getSunrise ( tVal , myTime );
//             if ( mTime.substring(11,13) == "SS" ) return getSunset  ( tVal , myTime );
//             return ERC::NOT_SRSS;   // Was expected SR or SS, got something else

//         case 16:         // Fmt#4: HH:MM (defaulting to GMT)
//             return getZTime ( tVal , myTime, mTime.substring(11,16) );

//         case 17:         // Fmt#5 or #6: "Z" or "L" is specified, so convert time appropriately
//             char timeRef = mTime.charAt(16);
//             switch ( timeRef ) {
//                 case 'Z': return getZTime ( tVal , myTime, mTime.substring(11,16) );
//                 case 'L': return getLTime ( tVal , myTime, mTime.substring(11,16) );
//             }
//             return ERC::NOT_ZL;     // Was expected Z or L, got something else
//     }
//     return ERC::SUCCESS;
// }
/** **************************** TIME STUFF - END ********************************************************** */

// Utility: Convert degrees to radians and vice versa
constexpr double degToRad(double deg) { return deg * M_PI / 180.0; }
constexpr double radToDeg(double rad) { return rad * 180.0 / M_PI; }

// Utility: Normalize an angle to [0, 360)
double normalizeDegrees(double deg) {
    while (deg < 0) deg += 360.0;
    while (deg >= 360.0) deg -= 360.0;
    return deg;
}

// Compute Julian Day from year, month, day
double julianDay(int y, int m, int d) {
    if (m <= 2) { y--; m += 12; }
    int A = y / 100;
    int B = 2 - A + (A / 4);
    return floor(365.25 * (y + 4716)) + floor(30.6001 * (m + 1)) + d + B - 1524.5;
}

// Compute sunrise/sunset UTC time (in seconds from midnight UTC)
double solarEventUTC(bool isSunrise, double JD, double lat, double lng) {
    double n = JD - 2451545.0 + 0.0008;
    double Jstar = n - (lng / 360.0);

    double M = normalizeDegrees(357.5291 + 0.98560028 * Jstar);
    double C = 1.9148 * sin(degToRad(M)) + 0.0200 * sin(degToRad(2 * M)) + 0.0003 * sin(degToRad(3 * M));
    double λ = normalizeDegrees(M + 102.9372 + C + 180.0);

    double Jtransit = 2451545.0 + Jstar + 0.0053 * sin(degToRad(M)) - 0.0069 * sin(degToRad(2 * λ));

    double δ = asin(sin(degToRad(λ)) * sin(degToRad(23.44))); // declination
    double cosH = (sin(degToRad(-0.833)) - sin(degToRad(lat)) * sin(δ)) / (cos(degToRad(lat)) * cos(δ));
    if (cosH < -1.0 || cosH > 1.0) return -1; // sun doesn't rise/set

    double H = radToDeg(acos(cosH)) / 360.0;
    double Jevent = isSunrise ? (Jtransit - H) : (Jtransit + H);

    double secondsUTC = (Jevent - floor(Jevent)) * 86400.0;
    return secondsUTC;
}

// Converts UTC seconds on a given date to time_t
time_t makeTimeUTC(int year, int month, int day, int seconds) {
    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = 0;
    t.tm_min = 0;
    t.tm_sec = seconds;
    return timegm(&t);
}

time_t getSunriseUTC(int year, int month, int day, float lat, float lng) {
    double jd = julianDay(year, month, day);
    int sec = (int)round(solarEventUTC(true, jd, lat, lng));
    return makeTimeUTC(year, month, day, sec);
}

time_t getSunsetUTC(int year, int month, int day, float lat, float lng) {
    double jd = julianDay(year, month, day);
    int sec = (int)round(solarEventUTC(false, jd, lat, lng));
    return makeTimeUTC(year, month, day, sec);
}

time_t timegm(struct tm* t) {
    return mktime(t);  // On Particle, time is already UTC
}

time_t parseTimeMark(
    const char* timeMark,
    char HF,
    float LAT,
    float LNG,
    int TZ,
    bool DST
) {
    int year, month, day, hour = 0, minute = 0;
    char suffix[3] = {0};
    size_t len = strlen(timeMark);

    // Base date
    if (sscanf(timeMark, "%4d-%2d-%2d", &year, &month, &day) != 3)
        return 0; // Invalid

    if (len == 10) {
        // #1: YYYY-MM-DD
        return (HF == 'H') ? getSunriseUTC(year, month, day, LAT, LNG)
                           : getSunsetUTC(year, month, day, LAT, LNG);
    }

    if (len == 13) {
        // #2 or #3: YYYY-MM-DD SR/SS
        sscanf(timeMark + 11, "%2s", suffix);
        if (strcmp(suffix, "SR") == 0)
            return getSunriseUTC(year, month, day, LAT, LNG);
        if (strcmp(suffix, "SS") == 0)
            return getSunsetUTC(year, month, day, LAT, LNG);
        return 0;
    }

    if (sscanf(timeMark + 11, "%2d:%2d", &hour, &minute) != 2)
        return 0;

    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = 0;

    if (len == 16) {
        // #4: YYYY-MM-DD HH:MM (default GMT)
        return timegm(&t);
    } else if (len == 17 && timeMark[16] == 'Z') {
        // #5: GMT explicit
        return timegm(&t);
    } else if (len == 17 && timeMark[16] == 'L') {
        // #6: Local time
        int totalOffset = TZ * 3600;
        if (DST) totalOffset += 3600;
        return timegm(&t) - totalOffset;
    }

    return 0;
}

bool EventManager::isDST(int dayOfMonth, int month, int dayOfWeek){  // North American Algorithm

    if (month < 3 || month > 11) return false;        // Dec-Feb is never DST
    if (month > 3 && month < 11) return true;         // Apr-Oct is always DST

    int previousSunday = dayOfMonth - dayOfWeek;      // What day of the month is the previous Sunday?
    if (month == 3) return previousSunday >= 8;       // Mar is DST if previous Sunday is on or after 8th
    return previousSunday <= 0;                       // Nov is DST if previous Sunday is before the 1st
}

int EventManager::getNEvents (){
    int n = 0;
    for ( int idx=0; idx<nEvents; idx++ ) {
        if ( EVL[idx].valid && EVL[idx].applies ) n++;
    }
    return n;
}

int EventManager::firstActiveEvent () {
// return the first valid, applicable event where the BegTime <= now() and EndTime is either TBD or > now();

    time_t now = Time.now();

    for ( int idx=0; idx<nEvents; idx++ ) {
/*
    Re-evaluate BMK and EMK (lat/long and TZ information may change)
        and see if the event still applies (based on jurisdiction or flag changes)
    Keep events that are valid, but don't apply - config can change that.
*/            
        if ( !EVL[idx].valid ) continue;
        if ( !EVL[idx].applies ) continue;
        if ( EVL[idx].GMTbegin == 0 ) continue;
        if ( EVL[idx].GMTbegin > now ) continue;
        if ( EVL[idx].GMTend > now ) return EVL[idx].eventID;
    }
    return -1;      // no active events found
}

int EventManager::getEE(){               // load key data from EEPROM (if valid)

    s_EEdata  s_EE;

    EEPROM.get( S_EE_LOC , s_EE );

/******************************************************
 *                     WARNING!!!                     *
 *                                                    *
 *         Any change to the order or content         *
 *         of fields may invalidate stored            *
 *         data!  Always consider conversion!         *
 *                                                    *
 ******************************************************/

    if ( s_EE._segtype != S_EE_TYP || s_EE._segvers != S_EE_VER ) return 1;   // failed to load - unexpected type/ver

// restore scheduler configuration data (including pending events)

    upperFlag       = String(s_EE._upperFlag);
    upperFlagPrio   = s_EE._upperFlagPrio;
    SFlat           = s_EE._SFlat;
    SFlong          = s_EE._SFlong;
    jurFederal      = String(s_EE._jurFederal);
    jurState        = String(s_EE._jurState);
    postalCode      = String(s_EE._postalCode);
    TZOffset        = s_EE._TZOffset;
    doDST           = s_EE._doDST;

    for ( int i=0; i<EventManager::nEvents; i++) {
        EVL[i].eventID = s_EE._EVL[i]._eventID;
        EVL[i].valid = ( EVL[i].eventID > 0 );
        if ( EVL[i].valid ) {
            EVL[i].eventVer = s_EE._EVL[i]._eventVer;
            EVL[i].GMTbegin = s_EE._EVL[i]._GMTbegin;
            EVL[i].GMTend = s_EE._EVL[i]._GMTend;
            EVL[i].eventJur = String(s_EE._EVL[i]._eventJur);
            EVL[i].eventFlag = String(s_EE._EVL[i]._eventFlag);
            EVL[i].BMK = String(s_EE._EVL[i]._BMK);
            EVL[i].EMK = String(s_EE._EVL[i]._EMK);
        } else {
            flagEvent feNull;
            EVL[i] = feNull;
        }
    }
    put_Count = s_EE._put_Count;

    return 0;
}

int EventManager::putEE(){                   // store key data in EEPROM for backup

    s_EEdata  s_EE;

    EEPROM.get( S_EE_LOC , s_EE );          // retrieve previous contents

/******************************************************
 *                     WARNING!!!                     *
 *                                                    *
 *         Any change to the order or content         *
 *         of fields may invalidate stored            *
 *         data!  Always consider conversion!         *
 *                                                    *
 ******************************************************/
    if ( s_EE._segtype != S_EE_TYP ||           // initialize new storage
         s_EE._segvers != S_EE_VER    ) {
            s_EE._segtype = S_EE_TYP;
            s_EE._segvers = S_EE_VER;
            put_Count = 0;
    }

// store scheduler configuration data (including pending events)
//      strncpy( myC, myS.c_str(), cLen-1 );
//      myC[cLen] = '\0';

    strncpy( s_EE._upperFlag , EventManager::upperFlag.c_str() , sizeof(s_EE._upperFlag ) );
    s_EE._upperFlag[sizeof(s_EE._upperFlag)] = '\0';

    s_EE._upperFlagPrio = upperFlagPrio;
    s_EE._SFlat         = SFlat;
    s_EE._SFlong        = SFlong;

    strncpy( s_EE._jurFederal , EventManager::jurFederal.c_str() , sizeof(s_EE._jurFederal ) );
    s_EE._jurFederal[sizeof(s_EE._jurFederal)] = '\0';

    strncpy( s_EE._jurState   , EventManager::jurState.c_str()   , sizeof(s_EE._jurState   ) );
    s_EE._jurState[sizeof(s_EE._jurState)] = '\0';

    strncpy( s_EE._postalCode , EventManager::postalCode.c_str() , sizeof(s_EE._postalCode ) );
    s_EE._postalCode[sizeof(s_EE._postalCode)] = '\0';

    s_EE._TZOffset      = TZOffset;
    s_EE._doDST         = doDST;

    s_EEevent s_EEe_blank;
    for ( int i=0; i<EventManager::nEvents; i++) {
        if ( EVL[i].valid ) {
            s_EE._EVL[i]._eventID       = EVL[i].eventID;
            s_EE._EVL[i]._eventVer      = EVL[i].eventVer;
            s_EE._EVL[i]._GMTbegin      = EVL[i].GMTbegin;
            s_EE._EVL[i]._GMTend        = EVL[i].GMTend;
            strncpy( s_EE._EVL[i]._eventJur , EVL[i].eventJur.c_str() , sizeof(s_EE._EVL[i]._eventJur ) );
            s_EE._EVL[i]._eventJur[sizeof(s_EE._EVL[i]._eventJur )-1] = '\0';
            strncpy( s_EE._EVL[i]._eventFlag , EVL[i].eventFlag.c_str() , sizeof(s_EE._EVL[i]._eventFlag ) );
            s_EE._EVL[i]._eventFlag[sizeof(s_EE._EVL[i]._eventFlag )-1] = '\0';
            strncpy( s_EE._EVL[i]._BMK , EVL[i].BMK.c_str() , sizeof(s_EE._EVL[i]._BMK ) );
            s_EE._EVL[i]._BMK[sizeof(s_EE._EVL[i]._BMK )-1] = '\0';
            strncpy( s_EE._EVL[i]._EMK , EVL[i].EMK.c_str() , sizeof(s_EE._EVL[i]._EMK ) );
            s_EE._EVL[i]._EMK[sizeof(s_EE._EVL[i]._EMK )-1] = '\0';
        } else {
            s_EE._EVL[i] = s_EEe_blank;
        }
    }
    s_EE._put_Count = ++put_Count;
    EEPROM.put( S_EE_LOC , s_EE );
    return 0;
}

String EventManager::showEE(){               // load key data from EEPROM (if valid)

    s_EEdata  s_EE;

    EEPROM.get( S_EE_LOC , s_EE );

/******************************************************
 *                     WARNING!!!                     *
 *                                                    *
 *         Any change to the order or content         *
 *         of fields may invalidate stored            *
 *         data!  Always consider conversion!         *
 *                                                    *
 ******************************************************/

    char buf[ JSONbufSize ];
    memset(buf, 0, sizeof(buf));
    JSONBufferWriter writer(buf, sizeof(buf) - 1);
    writer.beginObject();
        writer.name( "STYP" ).value(String(s_EE._segtype));
        writer.name( "SVER" ).value(s_EE._segvers);
        writer.name( "PUTS" ).value(s_EE._put_Count);
        writer.name( "FLAG" ).value(String(s_EE._upperFlag));
        writer.name( "FPRI" ).value(s_EE._upperFlagPrio);
        writer.name( "SLAT" ).value(s_EE._SFlat,6);
        writer.name( "SLNG" ).value(s_EE._SFlong,6);
        writer.name( "JFED" ).value(String(s_EE._jurFederal));
        writer.name( "JSTA" ).value(String(s_EE._jurState));
        writer.name( "POST" ).value(String(s_EE._postalCode));
        writer.name( "TZOF" ).value(s_EE._TZOffset);
        writer.name( "DDST" ).value(s_EE._doDST);

        writer.name( "EVLS").beginArray();
        for ( int i=0; i<EventManager::nEvents; i++) {
            if ( s_EE._EVL[i]._eventID > 0 ) {
                writer.beginObject();
                    writer.name("ID").value(s_EE._EVL[i]._eventID);
                    writer.name("VR").value(s_EE._EVL[i]._eventVer);
                    writer.name("GB").value(Time.format(s_EE._EVL[i]._GMTbegin, "%F %TZ"));
                    writer.name("GE").value(Time.format(s_EE._EVL[i]._GMTend, "%F %TZ"));
                    writer.name("EJ").value(String(s_EE._EVL[i]._eventJur));
                    writer.name("EF").value(String(s_EE._EVL[i]._eventFlag));
                    writer.name("BM").value(String(s_EE._EVL[i]._BMK));
                    writer.name("EM").value(String(s_EE._EVL[i]._EMK));
                    writer.endObject();
            } else {
                writer.nullValue();
            }
        }
        writer.endArray();
    writer.endObject();

    return String(buf);
}
void EventManager::resetSubscriptions() {
/*      Cancel previous subscriptions, then 
 *      resubscribe to Federal and State jurisdictions 
 *          (from mySched, where they're configured)
 *      "log" the jurisdictions in fedSub and staSub as confirmation of successful subscrfiption
*/
    if ( (getJurFed() == _fedSub &&
          getJurSta() == _staSub    ) ) return;      // no change

    Particle.unsubscribe();

    if ( isConfig() ) {

        String jur = getJurFed ();      // Federal subscription
        _fedSub = "";
        if ( jur.length() > 0 ) {
            if ( Particle.subscribe( jur , &EventManager::eventHandler, this) ) _fedSub = jur;
        }

        jur = getJurSta ();      // State subscription
        _staSub = "";
        if ( jur.length() > 0 ) {
            if ( Particle.subscribe( jur , &EventManager::eventHandler, this) ) _staSub = jur;
        }
    }
}

void EventManager::eventHandler(const char *eventName, const char *data) {

    int iRet = receiveEvent( String(data) );
    if ( iRet )
    /** Send "BAD EVENT" Status Report */
        // status(String::format( "Bad event [ERR:%d] Data:%s" , iRet , data ));
    return;
}

