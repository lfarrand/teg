#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include "event_log.h" // EventLevel for writeLogLevel

void writeLog(const String &msg); // records at info level
void writeLogLevel(uint8_t level, const String &msg); // EventLevel from event_log.h
void setStatusLine(const String &line);
// Hold a message on the status line for holdMs, ignoring setStatusLine() meanwhile.
// For things an operator must have time to read.
void setStatusNotice(const String &line, uint32_t holdMs);
void flushDisplay();
int getFreeMemory();     // DTCM stack headroom now, bytes
int getStackLowWater();  // smallest headroom seen since boot, bytes
int freeram();           // OCRAM heap free, bytes
// Non-blocking NTP client: call from loop(). Sends a request and collects the
// reply on later passes, so no path ever waits on DNS or the network.
void ntpTask();
bool prepareInfluxEndpoint(); // resolves only while outputs are inhibited
void writeInfluxDb(const char *data);

#endif
