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

bool otaIngestStream(Stream &in, const char **err, void (*progress)()) {
  // Safe state first: outputs masked, modulation IRQ off, tasks idled by
  // the loop() gate. Only a reboot leaves this state.
  if (!active) {
    enterOtaSafeState();
    active = true;
    writeLog("OTA: safe state entered; outputs disabled until reboot");
  }
  verified = false;
  otaIngestInit(ing);
  progressFn = progress;

  uint32_t idleSpins = 0;
  uint32_t sinceKick = 0;
  bool streamOk = true;
  for (;;) {
    const int c = in.read();
    if (c < 0) {
      if (++idleSpins < 200000) {
        continue;
      }
      break; // body exhausted
    }
    idleSpins = 0;
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
  if (!verified || confirmSize != verifiedSize) {
    return false;
  }
  commitPending = true;
  return true;
}

void otaRequestAbort() {
  if (!active) {
    return;
  }
  verified = false;
  otaFlashEraseSectorHW(OtaBufferBase); // kill the staged FCFB magic
  rebootPending = true;
}

void otaLoopTask() {
  if (commitPending) {
    commitPending = false;
    writeLog("OTA: committing new firmware and rebooting");
    delay(50); // let the log/socket drain
    kickWatchdog();
    otaFlashCommit(verifiedSize, verifiedCrc); // never returns
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
