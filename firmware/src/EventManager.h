// EventManager.h
#ifndef EVENT_MANAGER_H
#define EVENT_MANAGER_H

#include "Particle.h"
#include "math.h"
#include "HalyardManager.h"

extern HalyardManager halMgr1;  // Halyard manager instance
const int JSONbufSize = 1024;
/* EEPROM control values */

const char G_EE_TYP = 'G';                      // identifying character
const int  G_EE_LOC = 16;                       // starting point in EEPROM for G_ structure (gearmotor data)
const int  G_EE_VER = 1;                        // update this if the G_EE structure changes (forces a reload)
const int  G_EE_RES = 64;                       // # bytes reserved for G_EE (ver 1 actual size is 24)

const char D_EE_TYP = 'D';                      // identifying character
const int  D_EE_LOC = G_EE_LOC + G_EE_RES;      // starting point in EEPROM for D_ structure (director data)
const int  D_EE_VER = 1;                        // update this if the D_EE structure changes (forces a reload)
const int  D_EE_RES = 128;                      // # bytes reserved for D_EE (ver 1 actual size is 52)

const char S_EE_TYP = 'S';                      // identifying character
const int  S_EE_LOC = D_EE_LOC + D_EE_RES;      // starting point in EEPROM for S_ structure (scheduler data)
const int  S_EE_VER = 1;                        // update this if the S_EE structure changes (forces a reload)
const int  S_EE_RES = 2048;                     // # bytes reserved for S_EE (ver 1 actual size is 2000)


/*
 *   EventManager.h - define EventManager and flagEvent classes
 *   
 *      EventManager class schedules the movement of flags based on instructions from the host.
 *      Public methods allow a supervisory program to send configuration information, flag events, and to perform settings.
 *       
 *      flagEvent class contains data which describe a valid (but perhaps not applicable) flag event.
 */

class flagEvent {
    public:
     bool valid = false;            // true if event parsing is successful
     bool applies = false;          // true if event is determined to apply to this unit
     bool isDelete = false;         // true if the JSON string DEL field is true (event will be deleted from list if found)
     int eventID = 0;               //   IDv             main event ID number
     int eventVer = 0;              //   idV             version number for event
     time_t GMTbegin = 0;           //   BMK             start time for non-full location
     String eventJur = "";          //   JUR             published jurisdiction
     String eventFlag = "";         //   FLG             published flag (most senior, e.g. US at <ST>)
     time_t GMTend = 0;             //   EMK             end time for non-full location (if 0, event is open-ended)
     String BMK = "";
     String EMK = "";
};

enum ERC { SUCCESS , GENFAIL , NOT_HF , BAD_SUNEVENT , NOT_SRSS , NOT_ZL , PARSE_ERROR , EVL_OVERFLOW };

class EventManager {
    private:
        bool _attentionFlag = false;
        bool _conf_Sched = false;

    public:

        const String federalFlag = "US";
        int put_Count = 0;

//    Private properties:            JSON field

        Timer * changeTimer;
        EventManager();              // constructor
        ~EventManager();             // destructor
        void eventTimer();          // TSR

        String upperFlag;           // FLG                two-character abbreviation for topmost flag on pole (controls compliance)
        String halyardType;         // HAL                which type of halyard (A.B.C)?
        int upperFlagPrio;          // FPR                upperFlag's priority (1=federal, 2=state/region, 3=other)
        double SFlat;               // LAT                flagpole latitude (used for sun time calculations)
        double SFlong;              // LNG                flagpole longitude (used for sun time calculations)
        String jurFederal;          // FED                federal jurisdiction (used for subscriptions, qualification of events)
        String jurState;            // STA                state/regional jurisdiction (used for subscriptions, qualification of events)
        // const int nSubs = 10;
        // int jurSubs [nSubs];        // SJR[]              list of applicable state subjurisdictions
        String postalCode;          // ZIP                implicit subJurisdiction, postal (zip) code where flagpole resides
        float TZOffset;             // STD                hours offset for local (standard)
        bool doDST;                 // DST                <true> if DST should be observed (US rules only at this time)
        String _fedSub;          // FEDSUB             federal jurisdiction subscription (for cloud events)
        String _staSub;          // STASUB             state jurisdiction subscription (for cloud events
        const static int nEvents = 20;      // number of events that can be stored
        flagEvent EVL[nEvents];     // list of pending events
        // flagEvent EVL;                    // structured entry showing pending/active flag event


// these next items MUST be reevaluated each time an event is added/changed/removed from the eventList (or config is changed)

        time_t nextChange;         //                    epoch time (GMT) for next ordered flag movement
        FlagStation nextSta;            //                    station for next ordered movement 
        // float nextPos;            //                    position for next ordered movement (only applies if nextSta == STSta::NUL)

//     Private methods:

        int UTCSunEvent(float latitude, float longitude, int doy, int rs);
        int parseTimeMark ( time_t &tVal , String mTime , String mDest );       // convert string "Time Mark" to epoch time
        int getSunTime ( time_t &tVal , tm &sTime , int SRSS );
        int getSunrise ( time_t &tVal , tm &date );                              // convert tm date to sunrise GMT time_t
        int getSunset ( time_t &tVal , tm &date );                               // convert tm date to sunset GMT time_t
        int getZTime ( time_t &tVal , tm &date , String hhmm );                         // convert tm date and "hh:mm" GMT to GMT time_t
        int getLTime ( time_t &tVal , tm &date , String hhmm );                         // convert tm date and "hh:mm" Local to GMT time_t
        time_t timegm(struct tm* t);    
        bool needsAttention() { return _attentionFlag; }

        bool isDST(int dayOfMonth, int month, int dayOfWeek);  // North American Algorithm
        void clearEvent ( flagEvent &cEVL );
        int matchEVID ( int IDtoMatch );
        bool jurMatch ( String pubJur, String subJur);          // special comparison logic for jurisdictions
        bool eventApplies ( flagEvent aEVL );                    // compare aEVL attributes to unit config - return false if not applicable
        flagEvent parseEvent( String JSONFlagEvent );           // parse a flag event JSON before processing the data
        void setNextEvent ();                                    // go through any/all posted flag events and update pending event parameters
        void updEventTimer ();
        void reprocessEvents ();                        // check events after changes (e.g. config)
        String s_ShowNext ();
        int getEE();                    // load key data from EEPROM (if valid)
        int putEE();                    // store key data in EEPROM for backup

    // public:
        
    // Public methods:

        void begin();                                   // one-time setups for the Scheduler
        int configScheduler( String JSONconfig );       // set configuration attributes from JSON (see code for details)
        int purgeEvents ();                             // erase any out-of-date or invalid events to make room in array
        String staToLetter ( FlagStation station );             // translate FlagStation enum to single letter (?=UNKNOWN)
        int receiveEvent ( String JSONFlagEvent );      // 0 is success, all other values are error code
        String getJurFed () { return jurFederal; }
        String getJurSta () { return jurState;   }
        String getHalType () { return halyardType; }    // A=orig., B=plait on orig., C=secureBox
        int getNEvents ();
        int firstActiveEvent ();
        bool isConfig () { return _conf_Sched; }         // true if the scheduler has been configured
        void resetSubscriptions();
        void eventHandler(const char *eventName, const char *data); // handle incoming events from the cloud

    // Public data retrieval
    
        String showConfig ();                           // format the current configuration parameters in JSON
        String showEvent ( flagEvent sEVL );            // format the provided EVL entry in JSON
        String showEventList ( );                       // produce numbered list of defined events
        time_t nextFlagChange ();                       // return the GMT for the next scheduled flag movement
        FlagStation nextFlagStation();                        // return FlagStation value for next scheduled flag station
        void checkForChange();                          // see if a flag movement is due - if so, implement it
        String      showEE();                                               // format EEPROM data in JSON
    // Public properties:   [at this time, the Scheduler controls these values, but control may move to the Director...]

        FlagStation orderedSta;                               // proper location of the flag (in AUTO mode)
        unsigned int msUntilNext;                                // milliseconds until next flag position check
};

// flag event data (only store valid responses - even if they don't apply)
// flag events must be re-assessed any time the EEPROM data is restored
struct s_EEevent {
    int _eventID = 0;
    int _eventVer = 0;
    time_t _GMTbegin = 0;
    time_t _GMTend = 0;
    char _eventJur[10] = {0};     // 9-char string max (excluding terminator)
    char _eventFlag[3] = {0};     // 2-char string (plus terminator)
    char _BMK[25] = {0};          // 24-char string (plus terminator)
    char _EMK[25] = {0};          // 24-char string (plus terminator)
};

// scheduler configuration data (including pending events)

struct s_EEdata {
        char        _segtype = S_EE_TYP;        // must be “S”
        char        _segvers = S_EE_VER;        // increment if structure changes
        char        _upperFlag[3] = {0};        // always 2 bytes (3 with terminator)
        int         _upperFlagPrio = 0;
        double      _SFlat = 0.0;
        double      _SFlong = 0.0;
        char        _jurFederal[10] = {0};      // 9-char string max (excluding terminator)
        char        _jurState[10] = {0};        // 9-char string max (excluding terminator)
        char        _postalCode[6] = {0};       // 5-char string max (excluding terminator)
        float       _TZOffset = 0;
        bool        _doDST = false;
        s_EEevent   _EVL[EventManager::nEvents] = {0};
        int         _put_Count;
};

#endif
// End of EventManager.h