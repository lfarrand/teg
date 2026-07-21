#ifndef WAVEFORM_H
#define WAVEFORM_H

// Uploaded custom waveform storage: parses teg-wave v1 text (see
// waveform_parse.h), keeps the parsed form in RAM for the modulation engine,
// and persists the raw text to SD (/waveform.txt) across reboots.

#include <stdint.h>
#include <stddef.h>

// Buffer the caller reads uploaded text into (null-terminate before applying)
char *waveformTextBuffer(size_t *capacity);

// Parse + adopt + persist. On failure returns false and sets *errorOut.
bool waveformApplyText(const char *text, const char **errorOut);

void waveformLoadFromSd(); // call once at boot, after the SD card is up

uint8_t waveformType(); // WaveType* from waveform_parse.h
uint32_t waveformCount();
const int16_t *waveformReferenceLut(); // SpwmLutSize entries; valid when type==Reference
uint32_t waveformSegments(const int16_t **levelsQ15, const uint32_t **micros);

#endif
