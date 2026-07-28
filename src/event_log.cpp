// Event log storage and clock discipline (item: event log + RTC).
//
// Every writeLog() call lands here as well as on the OLED and serial, so
// the endpoint shows the same narrative the device has always printed -
// just timestamped, retained and reachable over the network.
//
// Clock: the Teensy 4.1's SNVS RTC keeps time across resets from the VBAT
// coin cell. At boot we read it once, so log entries carry real timestamps
// before the network is even up; when NTP later delivers a better time we
// adopt it and write it back to the RTC, so the next boot starts
// disciplined. Entries recorded before any clock is available carry epoch 0
// and are rendered by uptime instead of a misleading 1970 date.
//
// Timestamps come from a CACHED base epoch plus elapsed millis - never from
// TimeLib's now(). now() re-invokes the sync provider, and that performs a
// blocking DNS lookup plus NTP wait (seconds); with writeLog on the
// settings-apply, fault and OTA paths, a log line must never be able to
// stall the control loop or outlive the watchdog.

#include "event_log.h"
#include "event_log_api.h"
#include <Arduino.h>
#include <TimeLib.h>

extern "C" {
uint32_t rtc_get(void);
void rtc_set(uint32_t t);
}

// 128 entries x ~112 bytes = ~14KB: OCRAM, not the scarce DTCM. DMAMEM is a
// NOLOAD section, so the struct's in-class initializers never run - every
// field is established by eventLogBegin(), which setup() calls first.
DMAMEM static EventLog gLog;
static bool clockValid = false;
static bool ntpSynced = false;
static uint32_t baseEpoch = 0;   // epoch at baseMillis
static uint32_t baseMillis = 0;
static uint32_t bootId = 0;

void eventLogBegin() {
  eventLogClear(gLog);
  // Identity for this boot, so a client can tell its cursor is stale after
  // a reset rather than silently polling a sequence space that restarted
  bootId = rtc_get() ^ ARM_DWT_CYCCNT ^ 0x5A5A5A5Au;
  if (bootId == 0) {
    bootId = 1;
  }
  const uint32_t rtc = rtc_get();
  if (rtc >= MinPlausibleEpoch) {
    baseEpoch = rtc;
    baseMillis = millis();
    setTime(rtc); // for anything else that uses TimeLib
    clockValid = true;
  }
}

uint32_t eventNowEpoch() {
  if (!clockValid) {
    return 0;
  }
  return baseEpoch + (millis() - baseMillis) / 1000u;
}

void eventLogRecord(uint8_t level, const char *text) {
  eventLogAdd(gLog, level, text, eventNowEpoch(), millis());
}

// Called by the NTP task with an already-validated epoch: adopt the time and
// persist it to the battery-backed RTC so the next cold boot is close.
void eventClockSynced(uint32_t epoch) {
  if (epoch < MinPlausibleEpoch) {
    return; // never adopt or persist an implausible time
  }
  const bool firstSync = !ntpSynced;
  // Adopt first, so anything logged below is stamped with the new time
  baseEpoch = epoch;
  baseMillis = millis();
  clockValid = true;
  ntpSynced = true;
  setTime(epoch);

  // Write the RTC only when it actually disagrees. Comparing against the
  // RTC's own reading (not the last value we wrote) matters: rtc_set stops
  // the SNVS counter and discards its sub-second fraction, so writing on
  // every sync would keep re-quantising the clock backwards by up to 1s.
  const int32_t drift = static_cast<int32_t>(epoch - rtc_get());
  if (drift > 2 || drift < -2) {
    rtc_set(epoch);
  }
  if (firstSync) {
    eventLogRecord(EventInfo, "Clock synchronized from NTP");
  }
}

bool eventClockValid() {
  return clockValid;
}

bool eventClockNtpSynced() {
  return ntpSynced;
}

uint32_t eventBootId() {
  return bootId;
}

const EventLog &eventLogRef() {
  return gLog;
}
