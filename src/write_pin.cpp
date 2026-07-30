#include "write_pin.h"

#include <Arduino.h>

#include <qnethernet/entropy/entropy.h>

#include "config_json.h"
#include "config_serde.h" // copyConfigString
#include "event_log_api.h"
#include "utils.h"

extern MainConfig config;

// Entropy comes from QNEthernet's TRNG driver, not a hand-rolled one.
//
// The first version of this file drove the TRNG registers directly and was wrong in
// three ways that only hardware would have revealed. The RT1062 boot ROM leaves the
// TRNG clock gate OFF (CCM_CCGR6[CG6]); Teensy's startup never enables it, so the
// register accesses would have hit an unclocked peripheral - returning garbage at
// best, and at worst not terminating, at a point in boot before the watchdog is armed
// and with no recovery but the physical bootloader button. It also never tested
// MCTL[ERR], so a failed statistical self-test would have produced an all-zero
// entropy buffer that maps to the PIN "00000000" and reports success. And resetting
// MCTL to silicon defaults would have destroyed the configuration QNEthernet installs
// before setup() - values it deliberately loosens because the NXP defaults make this
// part fail its own self-tests - leaving TCP initial sequence numbers weakened too.
//
// QNEthernet is already linked, already owns this peripheral, enables the gate,
// clears ERR, reads all sixteen entropy words and applies the documented ENT0
// dummy-read workaround. Using two drivers for one peripheral was the mistake; there
// is no version of the private one worth keeping.
static bool trngBytes(uint8_t *out, size_t count) {
  return qindesign::entropy::trng_data(out, count) == count;
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

  // NOT the PIN itself. The event log is served by GET /api/log, which is
  // unauthenticated - logging the value would publish the credential to anyone who
  // can reach the device, defeating the entire point of generating one. The serial
  // console and the OLED are physical-access channels, which is the trade this
  // feature already accepts; the network is not.
  writeLogLevel(EventWarn,
                "Write PIN generated (shown on the display and serial console). "
                "Change it via the Security settings.");

  // Hold the display long enough to actually be read - the 1Hz memory report would
  // otherwise overwrite it within a second.
  setStatusNotice(String("PIN ") + pin, 120000);
}
