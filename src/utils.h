#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>

constexpr int16_t LogSize = 5;
extern String logs[];

void writeLog(const String &msg);
int getFreeMemory();
int freeram();
void printDigits(int digits);
time_t getNtpTime();
void sendNTPpacket(const char *host);
void writeInfluxDb(const String &data);

#endif