#pragma once

// Bootstrap for the API write PIN.
//
// writeAuthorized() returns true unconditionally when the PIN is empty, and empty is
// the compiled default - so an unconfigured board accepts every mutating request from
// anyone who can reach port 80, including a firmware upload. That single default is
// what makes the unsigned-OTA hole remotely reachable and what makes the OTA
// safe-state a one-request denial of service.
//
// Rather than refuse to operate until an operator sets one - which would need the
// very API being refused - the device generates a PIN on first boot, persists it, and
// displays it. Fails closed without locking anyone out, which is how consumer network
// equipment has settled this. The trade it accepts: anyone with physical sight of the
// display learns the PIN. That is consistent with the existing threat model, where
// physical access is already out of scope (see docs/SECURITY.md).
//
// The rendering half lives here as a pure function so it can be tested on the host;
// the entropy and persistence live in write_pin.cpp.

#include <stdint.h>
#include <stddef.h>

// Crockford-style base32 with I, L, O and U removed - the characters an operator
// misreads off a small OLED or mistypes from a photograph. Exactly 32 symbols, so a
// uniform byte maps to a uniform symbol through its low 5 bits: no modulo bias, no
// rejection sampling, no way for the mapping itself to weaken the PIN.
constexpr char WritePinAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
constexpr uint8_t WritePinAlphabetSize = 32;

// 8 symbols = 32^8, about 1.1e12 combinations. Hopeless to guess over a network even
// with no rate limiting, and still short enough to read off a display and type once.
constexpr uint8_t WritePinLength = 8;

// Render entropy as a PIN. Needs WritePinLength bytes in, and a buffer of at least
// WritePinLength + 1. Truncates rather than overruns if given a shorter buffer, and
// always terminates.
inline void writePinFromEntropy(const uint8_t *entropy, char *out, size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  size_t n = outSize - 1;
  if (n > WritePinLength) {
    n = WritePinLength;
  }
  for (size_t i = 0; i < n; i++) {
    out[i] = WritePinAlphabet[entropy[i] & 0x1F];
  }
  out[n] = '\0';
}

// True if every character is in the alphabet and the length is what we generate.
// Used to tell a device-generated PIN from an operator-chosen one.
inline bool writePinLooksGenerated(const char *pin) {
  if (pin == nullptr) {
    return false;
  }
  size_t n = 0;
  while (pin[n] != '\0') {
    bool found = false;
    for (uint8_t a = 0; a < WritePinAlphabetSize; a++) {
      if (pin[n] == WritePinAlphabet[a]) {
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
    n++;
    if (n > WritePinLength) {
      return false;
    }
  }
  return n == WritePinLength;
}

// Generate, persist and display a PIN if none is configured. Call once at boot, after
// the configuration has been loaded. No-op when a PIN is already set.
void writePinEnsure(const char *settingsFile);
