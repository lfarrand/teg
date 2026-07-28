#ifndef MEMORY_UTILS_H
#define MEMORY_UTILS_H

void initMemory();
void reportMemoryUsage();
bool testPsram();
bool flashFSAvailable(); // QSPI LittleFS mounted successfully

#endif