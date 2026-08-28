---
goal: Apply 2026-08-28 adversarial review P0/P1 safety and lean I/O fixes
version: 1.0
date_created: 2026-08-28
last_updated: 2026-08-28
owner: teg
status: 'Completed'
tags: [bug, refactor, safety, lean]
---

# Introduction

![Status: Completed](https://img.shields.io/badge/status-Completed-brightgreen)

Implement the ranked adversarial-review items that can land without a live-stage bench: fault-release race, preset IRQ/secrets, thermal fail-closed, integrity checks, poll honesty, and dead chrome. Do not change FlexPWM LDOK/VAL geometry, the bare `USB_MTPDISK_SERIAL` token, CMSIS-on, or global `-O3`.

## 1. Requirements & Constraints

- **REQ-001**: `releaseOutputInhibit()` must not reconnect OUTEN after a concurrent fault ISR has masked outputs.
- **REQ-002**: Preset/import must disable the PWM2 IRQ with `pwmInterruptRequired()` before publishing `MainConfig`.
- **REQ-003**: `restoreSecrets` keeps MQTT/Influx credentials only when endpoint identity is unchanged; PIN is always restored.
- **REQ-004**: Thermal enabled implies fail-closed: no full-scale derate on missing probes; no OneWire bit slots while OUTEN is live; release waits for a valid sample.
- **REQ-005**: API and import reject out-of-range `CurrentLimit.ThresholdMillivolts` with HTTP 422; boot load may still sanitize.
- **REQ-006**: Waveform SD staging checks every write/seek/flush and verifies more than file length.
- **REQ-007**: MTP store indexes are bounds-checked; `MTP.begin()` timer is torn down before PWM release.
- **REQ-008**: PSRAM prove uses volatile all-byte complementary reads, not two endpoints after `memset`.
- **REQ-009**: Stats poll is serialized, spectrum auto defaults off, default FFT query is 1024 points, capture JSON `count` is clamped.
- **REQ-010**: MQTT connect/CONNACK only while outputs are inhibited.
- **CON-001**: Bare `-DUSB_MTPDISK_SERIAL` (never `=1`).
- **CON-002**: `test_build_src = no`; header `constexpr` defaults that native tests hard-code must stay valid or tests update.
- **CON-003**: QNEthernet on Teensy still needs `yield()` / `delay()` / `Ethernet.loop()`; do not delete the loop yield contract.
- **CON-004**: Production keeps `TEG_ENABLE_CMSIS_FFT` and `TEG_ENABLE_UNSAFE_LAB_OTA` undefined.
- **GUD-001**: One owner per file in a parallel slice.
- **GUD-002**: Host-test safety field changes in `test/test_config_serde`.
- **PAT-001**: Web config already uses `disablePwmInterrupts()` then `pwmInterruptRequired()` to re-enable; presets must match.
- **SEC-001**: Imported files must not send stored MQTT/Influx secrets to a new host.

## 2. Implementation Steps

### Implementation Phase 1

- GOAL-001: Close the fault-release and publication races before any other behaviour change.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-001 | Add a fault generation counter in `maskAllOutputsSafely`; `releaseOutputInhibit` in `src/pwm_utils.cpp` prepares polarity/enable with OUTEN clear, then IRQ-off final sample + `connectConfiguredOutputDrivers`, then remask if `vFaultTripped` or live/latched faults. | ✅ | 2026-08-28 |
| TASK-002 | In `src/presets.cpp` `configApplyDocument`, disable IRQ when `pwmInterruptRequired()` (not only `spwmActive()`), memcpy, `applyPwmConfig`, re-enable only via `pwmInterruptRequired() && !vFaultTripped`. | ✅ | 2026-08-28 |
| TASK-003 | In `src/mtp_service.cpp`, call `MTP.loop()` immediately after `MTP.begin()` while inhibited; expose `mtpAllowsPwmRelease()`; refuse release until `firstLoopDone`. | ✅ | 2026-08-28 |

### Implementation Phase 2

- GOAL-002: Fail closed on thermal, secrets, current-limit, PSRAM, and waveform media.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-004 | `src/thermal.cpp`: skip OneWire request/harvest while `!pwmOutputInhibited()`; if enabled and no valid probe+die sample, derate 0 not 1000; `thermalAllowsPwmRelease()` false until first valid harvest. | ✅ | 2026-08-28 |
| TASK-005 | `restoreSecrets` in `src/config_serde.h`: always restore PIN; restore MQTT password only if Host/Port/Username match; restore Influx token only if Host/Port/Org/Bucket match; otherwise clear secret and set that integration `Enabled = false`. | ✅ | 2026-08-28 |
| TASK-006 | Add `configApiRejectReason(const MainConfig &)` in `src/config_serde.h`; if `CurrentLimit.ThresholdMillivolts` not in 100..3300 return a static reason. `api_config_post` and `configApplyDocument` 422/fail before sanitize. Boot `loadConfiguration` still sanitizes. Tests in `test/test_config_serde/test_main.cpp`. | ✅ | 2026-08-28 |
| TASK-007 | `src/memory_utils.cpp` `testPsram`: two complementary patterns over all 1024 bytes via `volatile` reads; kick WDOG between patterns. | ✅ | 2026-08-28 |
| TASK-008 | `src/waveform.cpp` SD staging: check write/seek/flush return values; after close, reopen and parse header+payload with existing `waveBinary` helpers, not length-only. | ✅ | 2026-08-28 |

### Implementation Phase 3

- GOAL-003: Cut generating-loop I/O load without new product features.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-009 | `web/stats.html`: in-flight lock; default spectrum auto unchecked; remove 250 ms interval (floor 1000 ms); hide CMSIS engine unless needed. | ✅ | 2026-08-28 |
| TASK-010 | `src/web_handlers.cpp`: default spectrum `points = 1024`; cap JSON bins at 128; clamp `api_capture` count to 32768; `#include <arm_math.h>` only under `TEG_ENABLE_CMSIS_FFT`. | ✅ | 2026-08-28 |
| TASK-011 | `src/mqtt.cpp` `mqttConnect`: return false unless `pwmOutputInhibited()`. | ✅ | 2026-08-28 |
| TASK-012 | `src/utils.cpp`: Influx drain cap 20 ms; `DisplayFlushIntervalMs = 1000`; skip `flushDisplay` I2C while `!pwmOutputInhibited()`; delete `printDigits`. | ✅ | 2026-08-28 |
| TASK-013 | `src/config_json.h` and `src/config_serde.h` Feedback.LoopHz default 250. | ✅ | 2026-08-28 |
| TASK-014 | `scripts/mtp_core162/MTP_Teensy.cpp`: before `isMediaPresent(store)`, require `store < storage_.get_FSCount()` (or equivalent) on GetObjectHandles and every `Storage2Store` site in that file. | ✅ | 2026-08-28 |

### Implementation Phase 4

- GOAL-004: Remove proven-dead chrome and publish the result artifact.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-015 | Delete unused `include/defines.h`; remove `enableXbar` from `src/pwm_utils.cpp` and `src/pwm_utils.h`; remove dead-time-compensation checkbox from `web/index.html`. | ✅ | 2026-08-28 |
| TASK-016 | Run `pio test -e native` for config_serde, waveform, thermal_math, spectrum. | ✅ | 2026-08-28 |
| TASK-017 | Write `docs/REVIEW_FIXES_2026-08-28.md` and update the Canvas with done/deferred. | ✅ | 2026-08-28 |

## 3. Alternatives

- **ALT-001**: Compile-time `teensy41-pwm` / `USB_SERIAL` default — deferred; contested and changes PID/COM.
- **ALT-002**: Full 7.3 MiB PSRAM March at boot — deferred; 1 KiB complementary volatile prove is the minimum that defeats O2/LTO DSE.
- **ALT-003**: `#ifdef` OTA compile-out and Pico CSS replacement — deferred; flash/layout risk, not a generating-loop race.
- **ALT-004**: Replace `delay(1)` with bare `yield()` — deferred this slice; Teensy QNEthernet already pumps from `yield()` inside `delay()`, and LoopHz 250 removes the structural PI miss.

## 4. Dependencies

- **DEP-001**: Existing `pwmInterruptRequired()`, `maskAllOutputsSafely()`, `waveBinary` parse helpers, Dallas `setWaitForConversion(false)`.
- **DEP-002**: Native Unity suites under `test/test_*` (`test_build_src = no`).

## 5. Files

- **FILE-001**: `src/pwm_utils.cpp` / `src/pwm_utils.h` — release race, enableXbar removal, thermal/MTP release gates.
- **FILE-002**: `src/presets.cpp` — IRQ gate, API reject, secrets (via serde).
- **FILE-003**: `src/mtp_service.cpp` / `src/mtp_service.h` — first `MTP.loop()`, `mtpAllowsPwmRelease`.
- **FILE-004**: `scripts/mtp_core162/MTP_Teensy.cpp` — store bounds.
- **FILE-005**: `src/thermal.cpp` / `src/thermal.h` — fail-closed, OneWire inhibit, `thermalAllowsPwmRelease`.
- **FILE-006**: `src/config_serde.h` / `src/config_json.h` — secrets, 422 reason, LoopHz 250.
- **FILE-007**: `src/memory_utils.cpp` — PSRAM prove.
- **FILE-008**: `src/waveform.cpp` — staging verify.
- **FILE-009**: `src/web_handlers.cpp` / `src/mqtt.cpp` / `src/utils.cpp` — spectrum, capture clamp, MQTT, Influx, OLED.
- **FILE-010**: `web/stats.html` / `web/index.html` — poll lock, spectrum default, DTC checkbox.
- **FILE-011**: `test/test_config_serde/test_main.cpp` — reject reason + secrets identity.
- **FILE-012**: `docs/REVIEW_FIXES_2026-08-28.md` — result artifact.
- **FILE-013**: `include/defines.h` — delete.

## 6. Testing

- **TEST-001**: `pio test -e native -f test_config_serde` — reject out-of-range threshold; secrets cleared on host change; PIN always restored.
- **TEST-002**: `pio test -e native -f test_waveform` — existing parse tests still pass after staging verify uses the same helpers.
- **TEST-003**: `pio test -e native -f test_thermal_math` — derate math unchanged.
- **TEST-004**: `pio test -e native -f test_spectrum` — 1024 default does not change `SpectrumMaxPoints` (stays 4096).

## 7. Risks & Assumptions

- **RISK-001**: IRQ-off window during OUTEN write delays the GPIO fault ISR by microseconds; hardware ACMP still gates PWM1/2. Post-check remask covers Tm3/Tm4.
- **RISK-002**: Skipping OneWire while generating leaves derate stale; fail-closed before first sample and last-good derate after that is the accepted trade.
- **RISK-003**: MQTT will not reconnect while generating; operator must inhibit to recover a dropped broker.
- **ASSUMPTION-001**: Native tests do not compile `pwm_utils.cpp`; release-race proof is review + later bench.
- **ASSUMPTION-002**: SdFat `write`/`seekSet`/`flush` return values are trustworthy enough for staging reject.

## 8. Related Specifications / Further Reading

- [docs/REVIEW_2026-08-28.md](../docs/REVIEW_2026-08-28.md)
- [docs/BENCH_CHECKS.md](../docs/BENCH_CHECKS.md)
- [docs/SECURITY.md](../docs/SECURITY.md)
- QNEthernet: `yield()` / `delay()` / `Ethernet.loop()` keep lwIP moving (Teensy EventResponder)
