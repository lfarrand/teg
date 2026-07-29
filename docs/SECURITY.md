# Security posture

Audited 2026-07-28 against the current `main`. This firmware was built as a
personal bench instrument and its security model reflects that: it is
appropriate for an isolated lab LAN and **not** appropriate, as it stands, for
anything reachable from an untrusted network or sold as a product.

This file states the position honestly so the gap is a decision rather than a
surprise.

## Threat model today

**Assumed:** a trusted, isolated LAN; physical access only by the owner; no
adversary on the network path; no fleet.

**Not defended against:** anyone who can reach TCP/80, anyone who can get the
operator's browser to issue a request, anyone with physical access, anyone who
can remove the SD card, and anyone on the network path between browser and
device.

## The three that matter

### 1. OTA accepts unsigned firmware — remote, permanent code execution

`otaVerifyImage()` checks image *structure* (FlexSPI config block, IVT, board
marker, boot data), a project marker string, and a CRC-32 **computed over the
bytes the uploader supplied**. That proves the flash write did not corrupt the
upload. It proves nothing about who wrote it. Every check is trivially
satisfiable by an attacker who compiles their own Teensy 4.1 image with the
marker string in it.

Consequence: anyone who can reach port 80 can permanently install arbitrary
firmware on hardware that drives a switching bridge — disabling the comparator
overcurrent path, driving arbitrary modulation, and bricking the update path so
there is no remote recovery.

**Fix (days, not weeks):** an application-level Ed25519 signature over the image
body, verified before `verified` is ever set, with the public key compiled in
and a monotonic version counter to block rollback to an older signed build.
Verification costs ~15–25 ms on this core against a multi-second flash copy.

**Platform ceiling, worth knowing before it costs you a board spin:** ROM-level
secure boot (HAB) is *unreachable on a Teensy 4.1*. Closing the part (burning
SRK_HASH, `SEC_CONFIG=1`) stops PJRC's unsigned HalfKay bootloader from working
and leaves no recovery path. A signed-boot product needs a custom RT1062 board.
Until then, an application-level signature stops the *remote* attack; it cannot
stop someone with physical access from flashing a build with the check removed.
Do not describe this as "secure boot".

### 2. No TLS, and authentication defaults to none

`writeAuthorized()` returns `true` unconditionally when the write PIN is empty,
which is the compiled default. Out of the box every mutating endpoint —
including OTA — is unauthenticated to anyone who can reach the device.

With a PIN set it is a single shared ≤15-character bearer secret, sent in
cleartext on every write, with no rate limiting, no lockout, and no logging of
failures. There is no Origin or Host validation, so a web page the operator
merely visits can issue writes.

**Fix:** refuse to start network writes with an empty PIN (or generate one at
first boot and show it on the OLED); add failed-attempt rate limiting and log
failures to the event log; validate `Origin`/`Host` on mutating requests. TLS on
this stack is a larger piece of work — the realistic near-term answer is to keep
the device off untrusted networks and put a reverse proxy in front if remote
access is needed.

### 3. Unauthenticated denial of service against a *generating* inverter

Three independent one-request kills, all pre-auth:

- A slow-loris header dribble deterministically trips the 8 s watchdog.
- `GET /api/capture/raw` freezes every control loop for seconds *while kicking
  the watchdog*, so nothing detects the stall.
- `POST /api/ota` with a single junk byte latches `enterOtaSafeState()` — outputs
  off until a manual reboot.

**Fix:** a total-request deadline with watchdog service in the header loop; chunk
the raw download with control-task service between chunks (the OTA path already
demonstrates the pattern); require authentication *before* entering the OTA safe
state.

## Everything else found

| Severity | Issue | Fix effort |
|---|---|---|
| High | Secrets (Influx token, MQTT password, write PIN) in plaintext JSON on a removable SD card, also readable over USB MTP | Medium |
| Medium | No rate limiting or lockout; failed auth never logged | Small |
| Medium | Every GET unauthenticated — capture waveforms, crash reports, logs, full config topology | Small |
| Medium | Physical access unrestricted: bootloader button, open SWD, USB serial, removable card | Redesign |
| Medium | NTP replies accepted with no source/transaction validation — log timestamps are forgeable | Trivial |
| Medium | The write API does not distinguish operational settings from safety interlocks (`FaultProtection`, `CurrentLimit` are just config) | Medium |
| Medium | Hardcoded MAC address shared by every unit built from this source | Trivial |
| Low | An InfluxDB organisation ID is a committed default | Trivial |
| Low | Crash log grows without bound on the SD card | Trivial |

## Supply chain

Only four libraries are vendored and reviewable in-tree (`lib/aWOT`,
`lib/eFlexPwm`, `lib/miniz`, `lib/MTP_Teensy`). The rest come from the PlatformIO
registry.

**Fixed 2026-07-29.** Those registry libraries previously used caret ranges
(`^4.0.6`, `^0.36.0`, …) and so floated within their major version: the same
commit could build against different library code on different days, with nothing
in the repo to show it. They are now pinned to exact versions in `platformio.ini`,
including the two transitive dependencies (`Adafruit BusIO` via SSD1306, `OneWire`
via DallasTemperature) — an unpinned transitive dependency floats just as freely
as a direct one. Verified: firmware builds and all 231 native tests pass against
the pinned set.

The platform, framework and toolchain were already pinned exactly. A commit now
means one binary.

### Licence blocker for any commercial use

**QNEthernet is AGPL-3.0-or-later** (verified: `LICENSE`, `library.json`, and
SPDX headers throughout its source). Linking it makes the whole firmware a
derivative work under AGPL, which for a shipped product means offering the
complete corresponding source — including any proprietary algorithm in the same
binary — to every recipient.

The author states other licence options may be available on request. The
alternatives are: buy a commercial licence, replace the stack (the Teensy
`NativeEthernet`/FNET route is `Apache-2.0 OR GPL-2.0-or-later`; lwIP alone is
BSD), or accept AGPL and publish. **Decide this before writing any algorithm you
intend to keep**, because the obligation attaches to whatever is in the binary.

## Recommended order

1. ~~Pin library versions~~ (done 2026-07-29); **decide the QNEthernet licence
   question** — this is the one that gets harder with time, because the AGPL
   obligation attaches to whatever ends up in the binary.
2. Ed25519-signed OTA with anti-rollback.
3. Close the pre-auth DoS trio; require auth before the OTA safe state.
4. Non-empty PIN enforced, rate limiting, failed-auth logging, Origin checks.
5. Encrypt secrets at rest, or move them off the removable card.
6. For a product: custom RT1062 board with HAB, and safety functions on their
   own MCU (see [PRODUCT_READINESS.md](PRODUCT_READINESS.md)).
