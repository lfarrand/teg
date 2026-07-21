#ifndef WAVEFORM_H
#define WAVEFORM_H

// Uploaded custom waveform storage. Reference samples live full-resolution in
// a 4MB PSRAM store (up to 2,097,152 Q15 samples); sequences are up to 64
// level/duration segments. Uploads stream straight into the store (text or
// TEGW binary, auto-detected) and persist to SD as /waveform.bin.

#include <stdint.h>
#include <Arduino.h>

// Parse + adopt + persist from a byte stream (an HTTP request body). Calls
// progress() periodically during long transfers (watchdog kicking). On
// failure returns false, sets *errorOut, and leaves the previous waveform
// in place unless the new one had already partially replaced it (in which
// case the type resets to none).
bool waveformApplyStream(Stream &in, const char **errorOut, void (*progress)());

void waveformLoadFromSd(); // call once at boot, after the SD card is up

uint8_t waveformType(); // WaveType* from waveform_parse.h
uint32_t waveformCount();
const int16_t *waveformSamples(); // PSRAM store; valid when type==Reference
uint32_t waveformSegments(const int16_t **levelsQ15, const uint32_t **micros);

#endif
