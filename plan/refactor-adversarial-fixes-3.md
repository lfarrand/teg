---
goal: Land binary spectrum, omit DeadTimeCompensation emit, and a defaults-on teg_features.h skeleton
version: 1.0
date_created: 2026-08-28
last_updated: 2026-08-28
owner: teg
status: 'Completed'
tags: [refactor, lean]
---

# Introduction

![Status: Completed](https://img.shields.io/badge/status-Completed-brightgreen)

Third slice after `plan/refactor-adversarial-fixes-2.md`. Add a PIN-authed binary `/api/spectrum` body, stop emitting `DeadTimeCompensation`, and introduce `teg_features.h` with every `TEG_WITH_*` defaulting to 1. Do not `lib_ignore`, wrap call sites, change USB, replace `delay(1)`, move MTP/FFT/stream placement, or change `applyPwmConfig`.

## 1. Requirements & Constraints

- **REQ-001**: `src/spectrum_wire.h` defines a 32-byte little-endian `TEGS` header and `spectrumWireQuantize` / `spectrumWireBinCount` / `spectrumWirePack` / `spectrumWireUnpack`. No Arduino types.
- **REQ-002**: `GET /api/spectrum?format=bin` returns `application/octet-stream` using `spectrumWirePack` into a 288-byte stack buffer. JSON remains the default when `format` is omitted. Auth stays `requireApiAuthorization`.
- **REQ-003**: `web/stats.html` `pollSpectrum` uses `?format=bin`, floors spectrum GETs at 2000 ms, and keeps status → log → spectrum order with `specBusy`.
- **REQ-004**: `configToJson` must not emit `Config.Pwm.Tm2.DeadTimeCompensation`. `configFromJson` and `validateConfig` still read and force-false.
- **REQ-005**: `src/teg_features.h` defines `TEG_WITH_OLED`, `TEG_WITH_THERMAL`, `TEG_WITH_MQTT`, `TEG_WITH_INFLUX`, `TEG_WITH_SPECTRUM`, `TEG_WITH_POWERMON`, `TEG_WITH_MTP_SERVICE`, each default 1 when undefined. No call-site `#if`. No `lib_ignore` from a flag.
- **CON-001**: Bare `-DUSB_MTPDISK_SERIAL` (never `=1`). No `teensy41-pwm` env.
- **CON-002**: Keep `delay(1)` in `loop()`.
- **CON-003**: `test_build_src = no`. Do not compile firmware `.cpp` into native.
- **CON-004**: `TEG_ENABLE_UNSAFE_LAB_OTA` and `TEG_ENABLE_CMSIS_FFT` stay undefined. Do not put those in `teg_features.h`.
- **CON-005**: Do not change FlexPWM geometry, `applyPwmConfig` fan-out, stream depth, or MTP timer teardown placement.
- **GUD-001**: One writer per file. Agent B waits for Agent A’s header.
- **GUD-002**: SchemaVersion stays 1. Omit on write, accept on read.
- **PAT-001**: Binary header is field-by-field `memcpy` like `api_capture_raw` `TEGC`, not a packed struct.
- **SEC-001**: `/api/spectrum` stays on `apiRouter` after PIN + subnet + Host/Origin.

## 2. Implementation Steps

### Implementation Phase 1

- GOAL-001: Host-testable wire codec, schema slim, and flags skeleton.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-001 | Create `src/spectrum_wire.h` with `SpectrumWireVersion=1`, `SpectrumWireHeaderSize=32`, `SpectrumJsonMaxBins=128`, `SpectrumWireScale=10000`, `SpectrumWireFields`, `spectrumWireQuantize`, `spectrumWireBinCount`, `spectrumWirePack`, `spectrumWireUnpack`. | ✅ | 2026-08-28 |
| TASK-002 | Add pack/unpack/unavailable/reject tests in `test/test_spectrum/test_main.cpp`. Keep existing FFT/THD tests. `SpectrumMaxPoints` stays 4096. | ✅ | 2026-08-28 |
| TASK-003 | In `src/config_serde.h` `configToJson`, delete `Config_Pwm_Tm2["DeadTimeCompensation"]`. Keep `configFromJson` read and `validateConfig` force-false. | ✅ | 2026-08-28 |
| TASK-004 | Update `test/test_config_serde/test_main.cpp` `test_dead_schema_keys_omitted` to assert `tm2["DeadTimeCompensation"].isNull()`. Extend `test_legacy_dead_keys_still_accepted` with `"DeadTimeCompensation":true`. | ✅ | 2026-08-28 |
| TASK-005 | Create `src/teg_features.h` with the seven `TEG_WITH_*` defaults-on macros. Create `test/test_features/test_main.cpp` asserting each `== 1`. Add a comment in `platformio.ini` near `lib_ignore` that flags live in the header and must not `lib_ignore` yet. | ✅ | 2026-08-28 |

### Implementation Phase 2

- GOAL-002: Dual-emit handler and stats consumer.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-006 | In `src/web_handlers.cpp` `api_spectrum`, include `spectrum_wire.h`. After FFT/THD, if `format=bin`, pack to a 288-byte stack buffer and write `application/octet-stream`. Unavailable still returns HTTP 200 with flags=0. JSON path unchanged except `mag[]` uses `SpectrumJsonMaxBins` and the same quantize. Do not add a second route. | ✅ | 2026-08-28 |
| TASK-007 | In `web/stats.html`, GET `/api/spectrum?format=bin`, parse with `DataView` LE, `mag[i] = bins[i] / 10000`, floor spectrum at 2000 ms, keep `specBusy` and status → log → spectrum. | ✅ | 2026-08-28 |
| TASK-008 | Run `pio test -e native` for `test_spectrum`, `test_config_serde`, `test_features`, `test_thermal_math`, `test_waveform`, `test_ota`. Write `docs/REVIEW_FIXES_2026-08-28-3.md` and the Canvas. | ✅ | 2026-08-28 |

## 3. Alternatives

- **ALT-001**: `lib_ignore` / call-site `#if` when `TEG_WITH_*=0` — deferred; compile-graph risk.
- **ALT-002**: MTP `FLASHMEM` on 1.62 command paths — deferred; wrong attribute on `loop()` reopens P1-20.
- **ALT-003**: FFT/gzip/stream toward EXTMEM — deferred; stream move needs an underrun bench; FFT/gzip change size gates.
- **ALT-004**: `teensy41-pwm` / `USB_SERIAL` / flip `default_envs` — deferred; contested USB/PID.
- **ALT-005**: Replace `delay(1)` with bare `yield()` — deferred; QNEthernet yield contract.

## 4. Dependencies

- **DEP-001**: Existing `api_spectrum` PIN route and `spectrum_math.h` portable FFT.
- **DEP-002**: Slice-2 `test_dead_schema_keys_omitted` / `test_legacy_dead_keys_still_accepted`.
- **DEP-003**: Native Unity discovery of `test/test_*` with `test_build_src = no` and `-I src`.

## 5. Files

- **FILE-001**: `src/spectrum_wire.h` — TEGS codec.
- **FILE-002**: `test/test_spectrum/test_main.cpp` — wire tests.
- **FILE-003**: `src/web_handlers.cpp` — `format=bin` emit.
- **FILE-004**: `web/stats.html` — binary poll + 2 s floor.
- **FILE-005**: `src/config_serde.h` — omit DTC emit.
- **FILE-006**: `test/test_config_serde/test_main.cpp` — DTC omitted / legacy accept.
- **FILE-007**: `src/teg_features.h` — defaults-on flags.
- **FILE-008**: `test/test_features/test_main.cpp` — flag defaults.
- **FILE-009**: `platformio.ini` — comment only.
- **FILE-010**: `docs/REVIEW_FIXES_2026-08-28-3.md` — result artifact.

## 6. Testing

- **TEST-001**: `pio test -e native -f test_spectrum` — existing FFT/THD plus pack/unpack/unavailable/bad magic.
- **TEST-002**: `pio test -e native -f test_config_serde` — DTC omitted; legacy key still parsed.
- **TEST-003**: `pio test -e native -f test_features` — all seven `TEG_WITH_*` == 1.
- **TEST-004**: `pio test -e native -f test_thermal_math` — derate math unchanged.
- **TEST-005**: `pio test -e native -f test_waveform` — parse helpers unchanged.
- **TEST-006**: `pio test -e native -f test_ota` — ingest/verify headers still compile.

## 7. Risks & Assumptions

- **RISK-001**: Stats parse of a live binary body is target-only. Host proves the codec; the device must serve `format=bin`.
- **RISK-002**: A later wrap/`lib_ignore` slice can still break `teensy41` if flags default off. This pass leaves them 1 and unused.
- **ASSUMPTION-001**: `configDocComplete` is generated from `configToJson` of defaults, so omitting DTC drops it from the required shape.
- **ASSUMPTION-002**: PlatformIO discovers `test/test_features` automatically (`test_build_src = no`, `-I src`).

## 8. Related Specifications / Further Reading

- [docs/REVIEW_2026-08-28.md](../docs/REVIEW_2026-08-28.md)
- [docs/REVIEW_FIXES_2026-08-28-2.md](../docs/REVIEW_FIXES_2026-08-28-2.md)
- [plan/refactor-adversarial-fixes-2.md](refactor-adversarial-fixes-2.md)
- PlatformIO Unity: `test/test_*` folders are discovered; this repo uses headers via `-I src` rather than `lib/` shared components
