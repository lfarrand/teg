// USB MTP file access (item: MTP). Lets the operator copy captures,
// waveforms, presets and settings off the card over USB without pulling it.
//
// Off by default, and deliberately so. Three properties of the library make
// this a maintenance-mode feature rather than an always-on one:
//
//  1. A whole transfer, directory scan or delete runs inside ONE MTP.loop()
//     call. The Teensyduino 1.62 sources we compile (see
//     lib/MTP_Teensy/PATCHES.md) service the watchdog inside those loops so a
//     large copy can no longer reset the
//     board - but the call still does not return for the duration, so every
//     control task in the superloop is stalled meanwhile.
//  2. Because of (1), MTP is withheld entirely while a waveform is being
//     STREAMED from the same card: the playback double buffer holds ~819ms
//     of samples at a 20kHz carrier, and an SD read that outlasts that
//     underruns the ring, which makes the modulation reference hold its last
//     sample - visible distortion on the output. Refusing to serve the host
//     is the honest trade: the transfer stalls, the inverter does not.
//  3. MTP.begin() arms an interval timer whose handler queries the
//     filesystem from INTERRUPT context (upstream issue #41), so it is
//     called last in setup(), after the card is mounted and settings are
//     loaded, leaving no other SD access inside that window.
//
// The MTP index file is steered to the QSPI flash chip, not the SD card, so
// the host's object database never lands next to /settings.cfg and /presets.

#include "mtp_service.h"
#include "sd_fs_adapter.h"
#include "config_json.h"
#include "waveform.h"
#include "capture.h"
#include "pwm_utils.h"
#include "memory_utils.h"
#include "ota.h"
#include "utils.h"
#include "main.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <MTP_Teensy.h>

extern MainConfig config;
extern SdFs sd;
extern bool sdAvailable;
extern LittleFS_QSPIFlash flashFS;

static SdFsAdapter sdAdapter(sd);
static bool started = false;
static bool firstLoopDone = false;
static bool paused = false;
static uint32_t pausedPasses = 0;

void mtpBegin() {
  if (!config.Mtp.Enabled || started) {
    return;
  }
  if (!pwmOutputInhibited()) {
    writeLogLevel(EventWarn, "MTP: waiting for maintenance mode (PWM outputs must be inhibited)");
    return;
  }
  // QSPI first so it becomes store 0 and therefore holds /mtpindex.dat -
  // the host's object database must not land next to /settings.cfg and
  // /presets on the SD card. If the flash chip did not mount, MTP falls
  // back to a RAM index, so say so rather than failing silently.
  if (!flashFSAvailable()) {
    writeLogLevel(EventWarn, "MTP: QSPI flash unavailable; index kept in RAM");
  }
  if (!MTP.addFilesystem(flashFS, "TEG-QSPI")) {
    writeLogLevel(EventWarn, "MTP: QSPI store failed");
  } else if (!MTP.useFilesystemForIndexList(flashFS)) {
    writeLogLevel(EventWarn, "MTP: index not stored on QSPI");
  }
  if (sdAvailable && !MTP.addFilesystem(sdAdapter, "TEG-SD")) {
    writeLogLevel(EventWarn, "MTP: SD store failed");
  }
  MTP.begin();
  started = true;
  writeLog(sdAvailable ? "MTP: USB file access enabled (SD + QSPI)"
                       : "MTP: USB file access enabled (QSPI only - no SD card)");
}

void mtpTask() {
  if (!started) {
    if (config.Mtp.Enabled && pwmOutputInhibited()) {
      mtpBegin();
    }
    return;
  }
  // MTP.begin() arms a 20Hz interval timer that answers the host until the
  // first MTP.loop() call tears it down - and its handler queries the
  // filesystem from INTERRUPT context. Never let the gate below skip that
  // first call, or the timer stays armed forever racing the SD reader.
  if (!firstLoopDone && pwmOutputInhibited() && !otaInProgress()) {
    firstLoopDone = true;
    MTP.loop();
    return;
  }

  // A host transfer runs to completion inside one MTP.loop() call, stalling
  // every control task meanwhile. Withhold service while the inverter is
  // actually generating: streamed playback would underrun its ~819ms buffer
  // (holding the last sample distorts the output), and the thermal derate,
  // feedback and MPPT loops must not be frozen for seconds while the bridge
  // is switching. An OTA owns the flash and must not contend either.
  const bool busy = otaInProgress() || !pwmOutputInhibited();
  if (busy) {
    if (!paused) {
      writeLogLevel(EventWarn, "MTP: paused - USB file access resumes when the inverter is idle");
    }
    paused = true;
    pausedPasses++;
    return;
  }
  if (paused) {
    paused = false;
    writeLog("MTP: resumed");
  }
  MTP.loop();
}

bool mtpEnabled() {
  return started;
}

bool mtpPaused() {
  return paused;
}

uint32_t mtpPausedCount() {
  return pausedPasses;
}
