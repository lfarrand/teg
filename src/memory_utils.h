#ifndef MEMORY_UTILS_H
#define MEMORY_UTILS_H

bool initMemory();
void reportMemoryUsage();
bool testPsram();
bool psramAvailable();
bool flashFSAvailable(); // QSPI LittleFS mounted successfully

#endif
