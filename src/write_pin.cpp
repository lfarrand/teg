#include "write_pin.h"

#include <Arduino.h>

#include "config_json.h"
#include "config_serde.h" // copyConfigString
#include "event_log_api.h"
#include "utils.h"

extern MainConfig config;

// Hardware TRNG (RT1062 §44). A credential must not come from millis(), an ADC noise
// floor or the chip's unique ID: the first two are guessable by anyone who knows the
// boot sequence, and the third is not secret - it is readable over USB and derived
// values are reproducible by anyone with the algorithm.
//
// Bounded everywhere. A boot must not hang because an entropy source is unhappy, so
// every wait has a spin limit and the caller treats failure as "no PIN generated"
// rather than retrying forever.
static bool trngBytes(uint8_t *out, size_t count) {
  constexpr uint32_t MaxSpins = 2000000; // ~ tens of ms at 600MHz, then give up

  TRNG_MCTL = TRNG_MCTL_PRGM | TRNG_MCTL_RST_DEF; // program mode, default config
  TRNG_MCTL = TRNG_MCTL_RST_DEF;                  // leaving PRGM starts generation

  volatile uint32_t *ent = &TRNG_ENT0;
  size_t produced = 0;
  while (produced < count) {
    uint32_t spins = 0;
    while ((TRNG_MCTL & TRNG_MCTL_ENT_VAL) == 0) {
      if (++spins > MaxSpins) {
        return false;
      }
    }
    for (int w = 0; w < 16 && produced < count; w++) {
      const uint32_t v = ent[w];
      for (int b = 0; b < 4 && produced < count; b++) {
        out[produced++] = static_cast<uint8_t>(v >> (8 * b));
      }
    }
    (void)TRNG_ENT15; // reading the last entropy word restarts generation
  }
  return true;
}

void writePinEnsure(const char *settingsFile) {
  if (config.Security.WritePin[0] != '\0') {
    return; // already set, by the operator or by a previous boot
  }

  uint8_t entropy[WritePinLength];
  if (!trngBytes(entropy, sizeof(entropy))) {
    // Say exactly what the consequence is. A quiet "TRNG failed" would leave an
    // operator with no idea their device is accepting unauthenticated writes.
    writeLogLevel(EventError,
                  "TRNG unavailable: no write PIN generated. API writes are UNAUTHENTICATED "
                  "- set Security.WritePin before connecting to any untrusted network.");
    return;
  }

  char pin[WritePinLength + 1];
  writePinFromEntropy(entropy, pin, sizeof(pin));
  copyConfigString(config.Security.WritePin, sizeof(config.Security.WritePin), pin);

  saveConfiguration(settingsFile);

  // Three places, because each fails differently: the OLED needs someone present, the
  // serial log needs someone attached, and the event log needs the device to still be
  // reachable. Without an SD card the save is lost and a new PIN appears next boot -
  // which the message has to make obvious, or the operator will note down a PIN that
  // stops working.
  const bool persisted = config.Security.WritePin[0] != '\0';
  Serial.println();
  Serial.println("========================================");
  Serial.print("  WRITE PIN GENERATED: ");
  Serial.println(pin);
  Serial.println("  Needed for every API write, including");
  Serial.println("  firmware updates. Note it down.");
  if (!persisted) {
    Serial.println("  NOT SAVED - a new PIN will be issued");
    Serial.println("  at the next boot (no SD card?).");
  }
  Serial.println("========================================");
  Serial.println();

  writeLogLevel(EventWarn, String("Write PIN generated: ") + pin +
                               " (shown on display; change it via the Security settings)");

  // Hold the display long enough to actually be read - the 1Hz memory report would
  // otherwise overwrite it within a second.
  setStatusNotice(String("PIN ") + pin, 120000);
}
