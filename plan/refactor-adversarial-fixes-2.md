---
goal: Land leftover 2026-08-28 review poll honesty, schema emit slims, settings-load deferral, OTA compile-out, and Pico tokens
version: 1.0
date_created: 2026-08-28
last_updated: 2026-08-28
owner: teg
status: 'Completed'
tags: [bug, refactor, lean]
---

# Introduction

![Status: Completed](https://img.shields.io/badge/status-Completed-brightgreen)

Second slice after `plan/refactor-adversarial-fixes-1.md`. Close leftover poll honesty, stop emitting dead schema keys, defer settings-page I/O, compile OTA out unless the lab flag is set, replace Pico with a token sheet, and paint the OLED from EventLog. Do not change USB type, `delay(1)`, FlexPWM geometry, `applyPwmConfig` fan-out, CMSIS-on, or global `-O3`.

## 1. Requirements & Constraints

- **REQ-001**: `thermalTask` request interval is 4000 ms when `config.Pwm.Tm2.SpwmCarrierFrequency >= 10000`, else 2000 ms; keep the 800 ms conversion wait and the inhibit skip.
- **REQ-002**: `loop()` RAM report interval is 30000 ms; `reportMemoryUsage` prints Serial only when `config.Pwm.Verbose`; OLED compact line always updates.
- **REQ-003**: `PowerMonConfig::IntervalMs` default and `validateConfig` floor are 250; `configFromJson` fallback is 250.
- **REQ-004**: `configToJson` must not emit `Pwm.SyncPwm`, `CurrentLimit.FilterCount`, `CurrentLimit.FilterPeriod`, or Tm2 Sm20–Sm23 `PwmFrequency`. Keep `configFromJson` reads and `validateConfig` clamps.
- **REQ-005**: Settings boot must not `GET /api/ota`, `/api/presets`, or `/api/waveform`. Those GETs run on first open of their panels. Export uses `/api/config?download=1`. Status poll uses `/api/status?lite=1`.
- **REQ-006**: `GET /api/status?lite=1` omits `analogRead`, `getStackLowWater`, capture/meter/aux/feedback/stream/missed-cycle/version/hostname/crash blocks. Full `/api/status` unchanged plus `otaEnabled`.
- **REQ-007**: `GET /api/config?download=1` matches today's export headers. Keep `/api/config/export` as an alias.
- **REQ-008**: When `TEG_ENABLE_UNSAFE_LAB_OTA` is undefined, `ota.cpp` and `flash_ota.cpp` are empty translation units; `ota.h` supplies inline stubs; OTA routes are not registered.
- **REQ-009**: Overwrite `web/pico.min.css` with a token sheet at most 2048 bytes uncompressed. Keep the `/pico.min.css` URL.
- **REQ-010**: Delete `logs[]` / `LogSize`. `flushDisplay` paints up to 5 newest `EventLog` texts. Keep 1 s / inhibit gates.
- **CON-001**: Bare `-DUSB_MTPDISK_SERIAL` (never `=1`). No `teensy41-pwm` env this pass.
- **CON-002**: Keep `delay(1)` in `loop()`. QNEthernet on Teensy services lwIP from `yield()` inside `delay()`.
- **CON-003**: `test_build_src = no`. Do not compile firmware `.cpp` into native suites.
- **CON-004**: `TEG_ENABLE_UNSAFE_LAB_OTA` stays undefined on `teensy41` and `native`. Do not add `TEG_WITH_*` flags this pass.
- **CON-005**: Do not change FlexPWM LDOK/VAL/MASK, `applyPwmConfig` capture/thermal/PLL fan-out, PLL 250 ms lock-drop, or MPPT ≥ 2.1 s floor.
- **GUD-001**: One writer per file. Wave A and Wave B file sets do not overlap.
- **GUD-002**: SchemaVersion stays 1. This is the compat release: omit on write, accept on read.
- **PAT-001**: aWOT query parse is `req.query("name", buf, sizeof(buf))` as in `api_spectrum`.
- **SEC-001**: Export and lite status still go through `requireApiAuthorization`. Secrets stay redacted on config GET.

## 2. Implementation Steps

### Implementation Phase 1

- GOAL-001: Land leftover poll honesty and PowerMon 250 ms defaults.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-001 | In `src/thermal.cpp` `thermalTask`, replace the fixed `sinceRequest >= 2000` request gate with `requestMs = config.Pwm.Tm2.SpwmCarrierFrequency >= 10000U ? 4000U : 2000U`. Keep inhibit skip and 800 ms harvest wait. | ✅ | 2026-08-28 |
| TASK-002 | In `src/main.cpp` `loop`, change `lastRamCheck` interval from 5000 to 30000. Keep the `setup()` `reportMemoryUsage()` call. | ✅ | 2026-08-28 |
| TASK-003 | In `src/memory_utils.cpp` `reportMemoryUsage`, wrap `Serial.println` in `if (config.Pwm.Verbose)`. Always call `getStackLowWater` and `setStatusLine`. Include `config_json.h` if `config` is not already visible. | ✅ | 2026-08-28 |
| TASK-004 | In `src/config_json.h` set `PowerMonConfig::IntervalMs = 250`. In `src/config_serde.h` change `configFromJson` fallback `| 100` to `| 250` and replace `validateConfig` `< 50` → 100 with `< 250` → 250. | ✅ | 2026-08-28 |

### Implementation Phase 2

- GOAL-002: Slim schema emit and settings-page boot I/O.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-005 | In `src/config_serde.h` `configToJson`, delete writes of `Config_Pwm["SyncPwm"]`, `Config_CurrentLimit["FilterCount"]`, `Config_CurrentLimit["FilterPeriod"]`, and Tm2 Sm20/Sm21/Sm22/Sm23 `["PwmFrequency"]`. Keep Tm2 `SpwmCarrierFrequency` and Tm1/Tm3/Tm4 frequencies. | ✅ | 2026-08-28 |
| TASK-006 | Update `test/test_config_serde/test_main.cpp`: PowerMon clamp 10 → 250; add `test_dead_schema_keys_omitted` and `test_legacy_dead_keys_still_accepted`. Keep `validateConfig` SyncPwm/filter/mirror tests. | ✅ | 2026-08-28 |
| TASK-007 | In `web/index.html`: wrap named-preset list/load/save/delete in `<details id="preset-panel">` (Export/Import stay outside); wrap Custom Waveform article in `<details id="wave-panel">`; give OTA article `id="ota-article"`; delete boot `refreshPresets()`, `refreshWaveform()`, and the `/api/ota` IIFE; first `toggle` open loads presets/waveform; `pollStatus` uses `/api/status?lite=1` and hides `#ota-article` unless `s.otaEnabled === true`; export URL `/api/config?download=1`; PowerMon `min="250"`; `buildSubmoduleCards` omits Frequency input when `path` starts with `Tm2.`. | ✅ | 2026-08-28 |

### Implementation Phase 3

- GOAL-003: Cheap status/export I/O, OTA compile-out, Pico tokens, OLED EventLog.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-008 | In `src/web_handlers.cpp` / `src/web_handlers.h`: parse `lite=1` in `api_status`; omit analogRead, stackLowWater, capture/meter/aux/feedback/stream/missed/version/hostname/crash; add `otaEnabled = otaReleaseEnabled()` on full and lite. Extract `writeConfigJson(Response&, bool attach)` for `api_config_get` (`download=1`) and `api_config_export` alias. Register OTA routes only under `TEG_ENABLE_UNSAFE_LAB_OTA`. | ✅ | 2026-08-28 |
| TASK-009 | Wrap `src/ota.cpp` and `src/flash_ota.cpp` translation units in `#ifdef TEG_ENABLE_UNSAFE_LAB_OTA`. When undefined, `src/ota.h` provides inline stubs: in-progress/verified/release false; sizes 0; last error `""`; ingest/commit false; abort/loop no-op. Do not edit `ota_ingest.h`, `ota_verify.h`, or `test/test_ota`. | ✅ | 2026-08-28 |
| TASK-010 | Overwrite `web/pico.min.css` with a token sheet ≤2048 bytes covering `:root` / `[data-theme=light]` / `[data-theme=dark]`, Pico variables used by both pages, `body`, `.container`, `article`+header, buttons, `.secondary.outline`, inputs/selects/labels, `[role=switch]`, `details`/`summary`. Keep filename and `/pico.min.css` route. | ✅ | 2026-08-28 |
| TASK-011 | In `src/utils.h` / `src/utils.cpp`: delete `LogSize` and `logs[]`. `writeLogLevel` keeps Serial + `eventLogRecord` + `displayDirty`. `flushDisplay` paints newest 5 EventLog texts via `eventLogRef` / `eventLogNewestSeq` / `eventLogAt`. Do not change 1 s / inhibit gates or add `getStackLowWater` callers. | ✅ | 2026-08-28 |
| TASK-012 | Run `pio test -e native` for `test_config_serde`, `test_thermal_math`, `test_waveform`, `test_spectrum`, `test_ota`. Write `docs/REVIEW_FIXES_2026-08-28-2.md` and the Canvas. | ✅ | 2026-08-28 |

## 3. Alternatives

- **ALT-001**: Add `teg_features.h` / `TEG_WITH_*` and a `teensy41-pwm` env this pass — deferred; library-gating and USB type are a later CI-graph / product choice.
- **ALT-002**: Full 7.3 MiB PSRAM March — deferred; 8 s WDOG; 1 KiB complementary prove already landed.
- **ALT-003**: Replace `delay(1)` with bare `yield()` — deferred; QNEthernet Teensy path still pumps from `yield()` inside `delay()`.
- **ALT-004**: Change `applyPwmConfig` to skip capture/thermal/PLL on telemetry edits — deferred; needs a bench.
- **ALT-005**: Binary spectrum body — deferred; lite status and 1024/128 already cut the recurring load.

## 4. Dependencies

- **DEP-001**: aWOT `Request::query(name, buf, size)` already used by `api_spectrum`.
- **DEP-002**: `eventLogRef`, `eventLogNewestSeq`, `eventLogAt` in `src/event_log.h` / `src/event_log_api.h`.
- **DEP-003**: Native Unity suites under `test/test_*` with `test_build_src = no`.

## 5. Files

- **FILE-001**: `src/thermal.cpp` — harvest request interval vs carrier.
- **FILE-002**: `src/main.cpp` — RAM report 30 s.
- **FILE-003**: `src/memory_utils.cpp` — Verbose-gated Serial.
- **FILE-004**: `src/config_json.h` / `src/config_serde.h` — PowerMon 250 and emit slims.
- **FILE-005**: `test/test_config_serde/test_main.cpp` — omitted keys, legacy accept, IntervalMs 250.
- **FILE-006**: `web/index.html` — P1-13 deferral, export URL, lite poll, Tm2 Hz hide, PowerMon min.
- **FILE-007**: `src/web_handlers.cpp` / `src/web_handlers.h` — lite status, download alias, OTA routes.
- **FILE-008**: `src/ota.h` / `src/ota.cpp` / `src/flash_ota.cpp` — compile-out stubs.
- **FILE-009**: `web/pico.min.css` — token sheet.
- **FILE-010**: `src/utils.cpp` / `src/utils.h` — OLED from EventLog.
- **FILE-011**: `docs/REVIEW_FIXES_2026-08-28-2.md` — result artifact.

## 6. Testing

- **TEST-001**: `pio test -e native -f test_config_serde` — IntervalMs floor 250; dead keys omitted; legacy keys still parse; completeness still generated from `configToJson`.
- **TEST-002**: `pio test -e native -f test_thermal_math` — derate math unchanged.
- **TEST-003**: `pio test -e native -f test_waveform` — parse helpers unchanged.
- **TEST-004**: `pio test -e native -f test_spectrum` — `SpectrumMaxPoints` stays 4096.
- **TEST-005**: `pio test -e native -f test_ota` — ingest/verify headers still compile (not ifdeffed).

## 7. Risks & Assumptions

- **RISK-001**: Pico token sheet can break unstyled form controls. Mitigate by covering the selectors both pages already use; verify in the browser.
- **RISK-002**: Production OTA POST becomes 404 instead of 501 because routes are unregistered. Settings UI never fetches them.
- **RISK-003**: Raising the PowerMon validate floor to 250 changes saved 100 ms configs on the next `validateConfig` (boot/apply). That is the review intent.
- **ASSUMPTION-001**: `configDocComplete` is derived from `configToJson` of a default document, so omitted keys drop out of the completeness set automatically.
- **ASSUMPTION-002**: Host tests do not prove ISR/OUTEN, OLED paint, or HTTP routing.

## 8. Related Specifications / Further Reading

- [docs/REVIEW_2026-08-28.md](../docs/REVIEW_2026-08-28.md)
- [docs/REVIEW_FIXES_2026-08-28.md](../docs/REVIEW_FIXES_2026-08-28.md)
- [plan/refactor-adversarial-fixes-1.md](refactor-adversarial-fixes-1.md)
- [docs/BENCH_CHECKS.md](../docs/BENCH_CHECKS.md)
- QNEthernet: Teensy services lwIP from `yield()` (EventResponder); `delay(1)` stays
