#ifndef EVENT_LOG_API_H
#define EVENT_LOG_API_H

// Firmware-side event log: storage, clock discipline and accessors. The
// ring logic itself lives in event_log.h (pure, natively tested).

#include <stdint.h>
#include "event_log.h"

// Anything before 2024-01-01 means the clock was never set (or the RTC cell
// is flat): treat it as no clock rather than reporting 1970
constexpr uint32_t MinPlausibleEpoch = 1704067200u;

void eventLogBegin();                                 // seed the clock from the RTC
void eventLogRecord(uint8_t level, const char *text);  // called by writeLog
void eventClockSynced(uint32_t epoch);                // validated NTP time
bool eventClockValid();     // a time is available (RTC or NTP)
bool eventClockNtpSynced(); // a real NTP reply has been accepted this boot
uint32_t eventNowEpoch();   // 0 when the clock has never been set
uint32_t eventBootId();     // changes every boot; lets clients spot a reset
const EventLog &eventLogRef();

#endif
