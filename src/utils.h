#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include "event_log.h" // EventLevel for writeLogLevel

constexpr int16_t LogSize = 5;
extern String logs[];

void writeLog(const String &msg); // records at info level
void writeLogLevel(uint8_t level, const String &msg); // EventLevel from event_log.h
void setStatusLine(const String &line);
void flushDisplay();
int getFreeMemory();
int freeram();
void printDigits(int digits);
// Non-blocking NTP client: call from loop(). Sends a request and collects the
// reply on later passes, so no path ever waits on DNS or the network.
void ntpTask();
void writeInfluxDb(const String &data);

#endif