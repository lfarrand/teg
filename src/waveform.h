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
bool waveformApplyStream(Stream &in, uint32_t expectedBytes, const char **errorOut,
                         void (*progress)());

void waveformLoadFromSd(); // call once at boot, after the SD card is up

uint8_t waveformType(); // WaveType* from waveform_parse.h
uint32_t waveformCount();
const int16_t *waveformSamples(); // PSRAM store; valid when resident reference
uint32_t waveformSegments(const int16_t **levelsQ15, const uint32_t **micros);

// SD streaming playback (references larger than the PSRAM store)
bool waveformIsStreaming();
int16_t waveformStreamNext();       // ISR side: next sample (holds last on underrun)
void waveformStreamTask();          // loop() side: refills the double buffer
uint32_t waveformStreamUnderruns();

// Decimated preview for the web UI (works for resident and streaming modes)
bool waveformPreview(int16_t *outSamples, uint32_t points);

#endif
