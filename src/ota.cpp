// OTA update flow (item: OTA). Pure ingest/verify logic in ota_ingest.h /
// ota_verify.h (natively tested); flash primitives in flash_ota.cpp;
// endpoints in web_handlers.cpp. This file owns the state machine.
//
// Two-phase contract:
//   POST /api/ota        -> safe state, stream + stage + verify, report
//   POST /api/ota/commit -> requires the verified size echoed back; the
//                           copy-down runs from loop() after the HTTP
//                           response has flushed, then reboots
//   POST /api/ota/abort  -> erases the staged image's first sector (kills
//                           its FCFB magic) and reboots into current fw
// The verified flag lives in RAM only: a stale staged image from a previous
// session can never be committed without a fresh upload.

#include "ota.h"
#include "ota_ingest.h"
#include "ota_verify.h"
#include "flash_ota.h"
#include "pwm_utils.h"
#include "utils.h"
#include "main.h"

extern MainConfig config;

static OtaIngest ing;
static bool active = false;
static bool verified = false;
static uint32_t verifiedSize = 0;
static uint32_t verifiedCrc = 0;

// Embedded so a staged image can be proven to be a build of THIS project
// (used attribute: never let the linker drop it)
__attribute__((used)) static const char otaMarker[] PROGMEM = OTA_PROJECT_MARKER;
static const char *lastError = "";
static volatile bool commitPending = false;
static volatile bool rebootPending = false;
static void (*progressFn)() = nullptr;

bool otaReleaseEnabled() {
#ifdef TEG_ENABLE_UNSAFE_LAB_OTA
  return true;
#else
  return false;
#endif
}

// Kick the watchdog ahead of each staging-sector erase (up to 400ms each)
static void eraseWithKick(uint32_t addr) {
  if (progressFn != nullptr) {
    progressFn();
  }
  otaFlashEraseSectorHW(addr);
}

static const OtaFlashOps hwOps = {&eraseWithKick, &otaFlashWriteHW};

bool otaInProgress() {
  return active;
}

bool otaImageVerified() {
  return verified;
}

uint32_t otaImageSize() {
  return verified ? verifiedSize
                  : (ing.maxAddr >= ing.minAddr && ing.bytesWritten > 0
                       ? ing.maxAddr - OtaFlashBase + 1
                       : 0);
}

uint32_t otaReceivedBytes() {
  return static_cast<uint32_t>(ing.bytesWritten);
}

uint32_t otaLineCount() {
  return ing.lineCount;
}

const char *otaLastError() {
  return lastError;
}

bool otaIngestStream(Stream &in, uint32_t expectedBytes, const char **err,
                     void (*progress)()) {
  if (!otaReleaseEnabled()) {
    lastError = "OTA disabled in production build";
    if (err) *err = lastError;
    return false;
  }
  if (expectedBytes == 0) {
    lastError = "Content-Length required";
    if (err) *err = lastError;
    return false;
  }
  // Safe state first: outputs masked, modulation IRQ off, tasks idled by
  // the loop() gate. Only a reboot leaves this state.
  if (!active) {
    enterOtaSafeState();
    active = true;
    writeLogLevel(EventWarn, "OTA: safe state entered; outputs disabled until reboot");
  }
  verified = false;
  otaIngestInit(ing);
  progressFn = progress;

  uint32_t sinceKick = 0;
  uint32_t received = 0;
  uint32_t lastProgressMs = millis();
  bool streamOk = true;
  while (received < expectedBytes) {
    const int c = in.read();
    if (c < 0) {
      if (progress != nullptr) progress();
      if (millis() - lastProgressMs > 5000U) {
        ing.error = "upload stalled before Content-Length bytes arrived";
        streamOk = false;
        break;
      }
      continue;
    }
    received++;
    lastProgressMs = millis();
    if (!otaFeedByte(ing, static_cast<char>(c), hwOps)) {
      streamOk = false;
      break;
    }
    if (++sinceKick >= 1024) {
      sinceKick = 0;
      if (progress != nullptr) {
        progress();
      }
    }
  }

  bool ok = streamOk && otaIngestFinish(ing, hwOps);
  if (ok) {
    const uint8_t *staged = reinterpret_cast<const uint8_t *>(OtaBufferBase);
    const uint32_t size = ing.maxAddr - OtaFlashBase + 1;
    const char *v = otaVerifyImage(staged, ing.minAddr, ing.maxAddr, ing.bytesWritten,
                                   ing.eofSeen);
    if (v == nullptr) {
      // Read-back: prove the staging flash holds exactly what was streamed
      // (the core's flash primitives report no errors of their own)
      v = otaVerifyStagedCrc(staged, size, otaIngestCrc(ing));
    }
    if (v != nullptr) {
      ing.error = v;
      ok = false;
    } else {
      verified = true;
      verifiedSize = size;
      verifiedCrc = otaIngestCrc(ing);
      char strBuf[LOG_BUF_SIZE];
      snprintf(strBuf, sizeof(strBuf), "OTA: image verified, %lu bytes staged, crc %08lX",
               static_cast<unsigned long>(verifiedSize),
               static_cast<unsigned long>(verifiedCrc));
      writeLog(strBuf);
    }
  }
  lastError = ing.error != nullptr ? ing.error : "";
  *err = lastError;
  return ok;
}

bool otaRequestCommit(uint32_t confirmSize) {
  if (!otaReleaseEnabled() || !verified || confirmSize != verifiedSize) {
    return false;
  }
  commitPending = true;
  return true;
}

void otaRequestAbort() {
  if (!otaReleaseEnabled() || !active) {
    return;
  }
  verified = false;
  otaFlashEraseSectorHW(OtaBufferBase); // kill the staged FCFB magic
  rebootPending = true;
}

void otaLoopTask() {
  if (commitPending) {
    commitPending = false;
    writeLogLevel(EventWarn, "OTA: committing new firmware and rebooting");
    delay(50); // let the log/socket drain
    kickWatchdog();
    // Returns only on failure: the read-back CRC never matched after every retry.
    // The board is still running from ITCM, so it stays up on the firmware it already
    // has - but the base image in flash is now whatever the failed copy left, and a
    // reset from here would boot that. Say so as loudly as the log allows and leave
    // the operator a live device to retry the update or recover over USB from.
    if (!otaFlashCommit(verifiedSize, verifiedCrc)) {
      writeLogLevel(EventError,
                    "OTA COMMIT FAILED: flash read-back never matched after 3 attempts. "
                    "The device is still running the OLD firmware from RAM, but the "
                    "flash image is now damaged - DO NOT REBOOT. Retry the update, or "
                    "recover over USB with the bootloader button.");
      lastError = "commit read-back failed; do not reboot";
      // Outputs stay in the OTA safe state, which only a reboot leaves. That is the
      // right place to be: the power stage is off and the device is reachable.
    }
  }
  if (rebootPending) {
    rebootPending = false;
    writeLog("OTA: aborted; rebooting into current firmware");
    delay(50);
    SCB_AIRCR = 0x05FA0004;
    for (;;) {
    }
  }
}
