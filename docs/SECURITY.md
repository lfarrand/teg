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

> **A PIN is generated at first boot (2026-07-30), but treat this as unproven.**
> `writePinEnsure()` draws 8 symbols from the hardware TRNG, persists them to
> `/settings.cfg`, and shows the PIN on the OLED and serial console. It runs before
> the network comes up.
>
> **Two defects were found in it by adversarial review after it was merged, and both
> are fixed — but neither the feature nor the fixes have run on hardware:**
>
> 1. It **logged the PIN into the event log**, which `GET /api/log` serves *without
>    authentication*. Any unauthenticated client could read the credential and then
>    use it to POST an unsigned OTA image. That is a total defeat of the feature, and
>    it made things worse than before, because this document had already recorded the
>    hole as closed. The log now records only that a PIN was generated.
> 2. It drove the **TRNG registers directly with the clock gate off**. The RT1062 boot
>    ROM leaves `CCM_CCGR6[CG6]` disabled and Teensy's startup never enables it —
>    QNEthernet does, from `Ethernet.begin()`, *two lines after* `writePinEnsure()`
>    ran. So the accesses hit an unclocked peripheral, before the watchdog is armed.
>    Best case no PIN was generated and the device booted fully unauthenticated while
>    claiming otherwise; worst case the access does not terminate and recovery is the
>    physical bootloader button. It now calls QNEthernet's own TRNG driver, which owns
>    the gate, checks `MCTL[ERR]`, and applies the documented ENT0 workaround.
>
> **Any board that ran the 2026-07-30 firmware before these fixes must be treated as
> having disclosed its PIN**, and as possibly never having had one.
>
> **The rest of this section still stands: there is no TLS, no rate limiting, no
> failed-auth logging and no Origin checking, and every GET is unauthenticated.**

`writeAuthorized()` returns `true` unconditionally when the write PIN is empty,
which was the compiled default. Out of the box every mutating endpoint —
including OTA — was unauthenticated to anyone who could reach the device.

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

- ~~A slow-loris header dribble deterministically trips the 8 s watchdog.~~ —
  **fixed 2026-07-30** (#42): the header phase is bounded at 4 s and the wait
  services the control tasks. See `lib/aWOT/PATCHES.md`.
- `GET /api/capture/raw` freezes every control loop for seconds — **partially fixed
  2026-07-30, and review found the fix incomplete.** The chunk loop now calls
  `serviceControlTasks()` instead of a bare `kickWatchdog()`, but the **response write
  itself is still unbounded and unserviced**, so a slow reader can still stall the loop
  past the 8 s watchdog on an unauthenticated GET. Do not treat this path as closed. That helper runs the
  interval-gated control tasks (feedback, PLL, thermal, waveform stream, meter,
  MPPT, ACMP) and deliberately excludes everything touching the network or USB —
  re-entering the stack mid-response is the one thing it must never do. The same
  upgrade was applied to the aWOT service callback and to the 401 body-drain in
  `api_ota_post`.

  **Still outstanding here:** the waveform upload (`waveformApplyStream`) keeps a
  bare watchdog kick. Servicing the control loops there would run
  `waveformStreamTask()`, which reads the waveform store for playback, while the
  upload is rewriting it. That upload-versus-playback interaction needs settling
  before the same change is safe — it is a correctness question about the waveform
  store, not a security one.
- ~~`POST /api/ota` with a single junk byte latches `enterOtaSafeState()`~~ —
  **this was wrong.** `api_ota_post()` calls `writeAuthorized()` first, drains the
  body and returns 401 before `otaIngestStream()` (and therefore
  `enterOtaSafeState()`) is reached. The ordering was already correct; the path
  was only reachable because authentication defaulted to none, which §2 now fixes.
  Corrected 2026-07-30 after checking the code rather than the finding.

**Fix for the remaining two:** a total-request deadline with watchdog service in
the header loop (this lives in the vendored `lib/aWOT`, which has its own timeout
machinery worth reading first); chunk the raw download with control-task service
between chunks — the OTA path already demonstrates the pattern.

## Everything else found

| Severity | Issue | Fix effort |
|---|---|---|
| High | Secrets (Influx token, MQTT password, write PIN) in plaintext JSON on a removable SD card, also readable over USB MTP | Medium |
| Medium | No rate limiting or lockout; failed auth never logged | Small |
| **High** | **Every GET unauthenticated** — capture waveforms, crash reports, logs, full config topology and the MQTT username. Upgraded from Medium: this is the channel that leaked the generated write PIN, and it composes with anything that ever logs a secret. `api_log`, `api_crash`, `api_config` and `api_config/export` all need an auth check. | Small |
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
   obligation attaches to whatever ends up in the binary. Enquiry sent
   2026-07-29, awaiting a reply.
2. ~~Non-empty PIN enforced~~ (done 2026-07-30 — generated at first boot). Rate
   limiting, failed-auth logging and Origin checks are still outstanding.
3. Ed25519-signed OTA with anti-rollback. **Needs a decision first:** where the
   signing key lives and whether CI signs automatically.
4. Close the two remaining pre-auth DoS paths (slow-loris deadline, chunked raw
   capture download).
5. Encrypt secrets at rest, or move them off the removable card.
6. For a product: custom RT1062 board with HAB, and safety functions on their
   own MCU (see [PRODUCT_READINESS.md](PRODUCT_READINESS.md)).
