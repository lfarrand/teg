---
goal: Close remaining host-safe operator-doc and comment leftovers after slice 4
version: 1.0
date_created: 2026-08-29
last_updated: 2026-08-29
owner: teg
status: 'Completed'
tags: [refactor, docs]
---

# Introduction

![Status: Completed](https://img.shields.io/badge/status-Completed-brightgreen)

Fifth slice after `plan/refactor-adversarial-fixes-4.md`. Slice 4 cleaned README and `docs/BENCH_CHECKS.md` but left SECURITY.md, settings MQTT hint, stats `(portable)`, and four comment/test names. Rewrite those only. Do not change USB, `delay(1)`, `applyPwmConfig`, FlexPWM geometry, discovery JSON fields, or MPPT defaults.

## 1. Requirements & Constraints

- **REQ-001**: `docs/SECURITY.md` must say production `/api/ota*` routes are unregistered and return HTTP 404, not “reports disabled” or 501.
- **REQ-002**: `web/index.html` MQTT hint must not claim HA energy-dashboard acceptance.
- **REQ-003**: `web/stats.html` spec-info must drop the `(portable)` parenthetical. Keep TEGS poll, 2 s floor, `specBusy`, and status → log → spectrum.
- **REQ-004**: `src/mqtt_discovery.h` energy comment must describe payload shape only (`device_class=energy`, `state_class=total_increasing`, `Wh`). Do not change `MqttSensors[]`.
- **REQ-005**: Rename `test_energy_sensor_feeds_ha_dashboard` to `test_energy_sensor_payload_shape`. Keep the three assertions.
- **REQ-006**: `src/config_json.h` MPPT comment must say bench pairing, not grid-tie topology. Do not change defaults.
- **REQ-007**: `src/web_handlers.cpp` CMSIS comment must not say the JSON response reports `"portable"`. Leave `#ifdef` and TEGS untouched.
- **CON-001**: Bare `-DUSB_MTPDISK_SERIAL`. No `teensy41-pwm`. Keep `delay(1)`.
- **CON-002**: Do not enable `TEG_ENABLE_CMSIS_FFT` or `TEG_ENABLE_UNSAFE_LAB_OTA`.
- **CON-003**: Do not change FlexPWM geometry or `applyPwmConfig` fan-out.
- **GUD-001**: One writer per file. Comment/operator-text only in hot files.
- **GUD-002**: Do not invent a new test census. Six-suite gate stays 91/91; report `test_mqtt_discovery` separately.
- **PAT-001**: Bench-instrument language. Payload shape and bench pairing only.
- **SEC-001**: Production OTA contract is unregistered routes → 404.

## 2. Implementation Steps

### Implementation Phase 1

- GOAL-001: Seven file-exclusive comment/doc rewrites in parallel.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-001 | Rewrite `docs/SECURITY.md` OTA paragraph to production 404. | ✅ | 2026-08-29 |
| TASK-002 | Rewrite `web/index.html` MQTT hint: drop “for the HA dashboard”; add “Bench MQTT”. | ✅ | 2026-08-29 |
| TASK-003 | In `web/stats.html`, change `` `${computeMicros} µs (portable)` `` to `` `${computeMicros} µs` ``. | ✅ | 2026-08-29 |
| TASK-004 | Rewrite `src/mqtt_discovery.h` energy comment to payload shape only. | ✅ | 2026-08-29 |
| TASK-005 | In `test/test_mqtt_discovery/test_main.cpp`, rename the test and comment; keep assertions; update `RUN_TEST`. | ✅ | 2026-08-29 |
| TASK-006 | Rewrite `src/config_json.h` MPPT comment to bench pairing. | ✅ | 2026-08-29 |
| TASK-007 | In `src/web_handlers.cpp`, drop “and the response reports `"portable"`” from the CMSIS comment only. | ✅ | 2026-08-29 |

### Implementation Phase 2

- GOAL-002: Verify host suites and write the slice artifact.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-008 | Run `pio test -e native` for `test_mqtt_discovery` plus the six-suite 91/91 gate. | ✅ | 2026-08-29 |
| TASK-009 | Write `docs/REVIEW_FIXES_2026-08-28-5.md` and the Canvas. Mark this plan Completed. | ✅ | 2026-08-29 |

## 3. Alternatives

- **ALT-001**: Change production OTA to emit 501 “disabled” — rejected; unregistered 404 is the landed contract.
- **ALT-002**: Change energy discovery fields — rejected; payload shape is already correct.
- **ALT-003**: `lib_ignore` / `#if TEG_WITH_*` — deferred; compile-graph risk.
- **ALT-004**: USB / `teensy41-pwm` / `delay(1)` / `applyPwmConfig` — deferred stay-offs.

## 4. Dependencies

- **DEP-001**: Slice 4 dropped JSON `"engine"` and cleaned README / `BENCH_CHECKS.md`.
- **DEP-002**: Production OTA routes are already behind `#ifdef TEG_ENABLE_UNSAFE_LAB_OTA` in `src/web_handlers.cpp`.
- **DEP-003**: Native discovers `test/test_mqtt_discovery` with `test_build_src = no`.

## 5. Files

- **FILE-001**: `docs/SECURITY.md` — OTA 404.
- **FILE-002**: `web/index.html` — MQTT hint.
- **FILE-003**: `web/stats.html` — drop `(portable)`.
- **FILE-004**: `src/mqtt_discovery.h` — energy comment.
- **FILE-005**: `test/test_mqtt_discovery/test_main.cpp` — test name.
- **FILE-006**: `src/config_json.h` — MPPT comment.
- **FILE-007**: `src/web_handlers.cpp` — CMSIS comment clause.
- **FILE-008**: `docs/REVIEW_FIXES_2026-08-28-5.md` — result artifact.

## 6. Testing

- **TEST-001**: `pio test -e native -f test_mqtt_discovery` — 8/8; renamed test still asserts energy / total_increasing / Wh.
- **TEST-002**: `pio test -e native -f test_config_serde` — MPPT defaults unchanged.
- **TEST-003**: `pio test -e native -f test_spectrum` — TEGS wire unchanged.
- **TEST-004**: `pio test -e native -f test_features` — flags still default 1.
- **TEST-005**: `pio test -e native -f test_ota` — ingest/verify headers still compile.
- **TEST-006**: `pio test -e native -f test_thermal_math` — derate math unchanged.
- **TEST-007**: `pio test -e native -f test_waveform` — parse helpers unchanged.

## 7. Risks & Assumptions

- **RISK-001**: A settings user may still infer HA energy-dashboard support from remaining “energy” wording. The rewrite drops acceptance language only.
- **ASSUMPTION-001**: Unregistered aWOT routes return 404. That is the production OTA contract.
- **ASSUMPTION-002**: Renaming one Unity test does not change the six-suite 91/91 count.

## 8. Related Specifications / Further Reading

- [docs/REVIEW_FIXES_2026-08-28-4.md](../docs/REVIEW_FIXES_2026-08-28-4.md)
- [plan/refactor-adversarial-fixes-4.md](refactor-adversarial-fixes-4.md)
- [docs/SECURITY.md](../docs/SECURITY.md)
- [AGENTS.md](../AGENTS.md)
