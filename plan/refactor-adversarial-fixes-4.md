---
goal: Align operator docs to landed contracts and delete unused PWM wrappers plus the JSON engine key
version: 1.0
date_created: 2026-08-29
last_updated: 2026-08-29
owner: teg
status: 'Completed'
tags: [refactor, docs]
---

# Introduction

![Status: Completed](https://img.shields.io/badge/status-Completed-brightgreen)

Fourth slice after `plan/refactor-adversarial-fixes-3.md`. No ranked P0/P1 item remains that is host-safe. Correct stale README and bench text, delete unused `attachInterruptVectors` and `calculateBestPrescaler`, and drop the unused JSON `"engine"` field. Do not change USB, `delay(1)`, `applyPwmConfig`, FlexPWM geometry, flags wrap, or placement.

## 1. Requirements & Constraints

- **REQ-001**: `README.md` must describe LoopHz 250, `/api/spectrum?format=bin` (TEGS), production `/api/ota*` as HTTP 404, identity-gated `restoreSecrets`, export `/api/config?download=1` with `/export` alias, and `/api/status?lite=1`.
- **REQ-002**: `docs/BENCH_CHECKS.md` must expect OTA 404 (not 501), identity-gated import secrets, portable FFT only, and `PowerMon.IntervalMs` floor 250.
- **REQ-003**: Delete unused `attachInterruptVectors` and `calculateBestPrescaler` from `src/pwm_utils.h` and `src/pwm_utils.cpp`. Keep `attachModule2PwmInterruptVectors` and `bestPrescalerIndex`.
- **REQ-004**: Delete only `doc["engine"] = useCmsis ? "cmsis" : "portable";` from `api_spectrum` in `src/web_handlers.cpp`.
- **CON-001**: Bare `-DUSB_MTPDISK_SERIAL` (never `=1`). No `teensy41-pwm` env.
- **CON-002**: Keep `delay(1)` in `loop()`.
- **CON-003**: Do not change FlexPWM LDOK/`LDMOD=0`, VAL geometry, MASK polarity, or `applyPwmConfig` capture/thermal/PLL fan-out.
- **CON-004**: Do not enable `TEG_ENABLE_CMSIS_FFT` or `TEG_ENABLE_UNSAFE_LAB_OTA`. Do not `lib_ignore` from a `TEG_WITH_*` flag.
- **GUD-001**: One writer per file. Do not invent product claims or a new test census.
- **GUD-002**: Host Unity 91/91 is not ISR/OUTEN proof. NO-SHIP until the disconnected checklist passes.
- **PAT-001**: Bench-instrument language from `AGENTS.md`. Rewrite grid-tie / HA energy-dashboard sentences; do not replace them with new product claims.
- **SEC-001**: Import docs must state PIN always restored and MQTT/Influx only when endpoint identity matches.

## 2. Implementation Steps

### Implementation Phase 1

- GOAL-001: Docs honesty and dead chrome in parallel.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-001 | Edit `README.md` API table, Loop Hz, spectrum, PLL, MQTT, OTA, presets, PowerMon, architecture, and highlights to match landed contracts. Drop the 319-test census. Do not invent a replacement count beyond the last verified 91/91 six-suite gate. | ✅ | 2026-08-29 |
| TASK-002 | Edit `docs/BENCH_CHECKS.md` §0, §0b, §2, §4a, §8, §10, §11, and “What the tests already cover” to 404 / identity-gated secrets / portable FFT / 4 s harvest when carrier ≥ 10 kHz while inhibited. | ✅ | 2026-08-29 |
| TASK-003 | In `src/pwm_utils.h` and `src/pwm_utils.cpp`, delete `attachInterruptVectors` and `calculateBestPrescaler` only. Keep `attachModule2PwmInterruptVectors`. Do not edit `applyPwmConfig`. | ✅ | 2026-08-29 |
| TASK-004 | In `src/web_handlers.cpp` `api_spectrum`, delete only the `doc["engine"]` assignment. Leave `#ifdef TEG_ENABLE_CMSIS_FFT` and the TEGS path untouched. | ✅ | 2026-08-29 |

### Implementation Phase 2

- GOAL-002: Verify host suites and write the slice artifact.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-005 | Run `pio test -e native` for `test_config_serde`, `test_features`, `test_ota`, `test_spectrum`, `test_thermal_math`, `test_waveform`. Expect 91/91. | ✅ | 2026-08-29 |
| TASK-006 | Write `docs/REVIEW_FIXES_2026-08-28-4.md` and the Canvas. Mark this plan Completed. | ✅ | 2026-08-29 |

## 3. Alternatives

- **ALT-001**: `lib_ignore` / call-site `#if` when `TEG_WITH_*=0` — deferred; compile-graph risk.
- **ALT-002**: Lean USB / `teensy41-pwm` — deferred; contested PID/COM.
- **ALT-003**: Replace `delay(1)` with bare `yield()` — deferred; QNEthernet yield contract.
- **ALT-004**: Skip capture/thermal/PLL inside `applyPwmConfig` — deferred; needs a bench.
- **ALT-005**: Add `featureBits` or a DWT control-loop split — rejected; not a ranked leftover and not host-provable.

## 4. Dependencies

- **DEP-001**: Slices 1–3 landed contracts in `docs/REVIEW_FIXES_2026-08-28.md` through `-3.md`.
- **DEP-002**: `bestPrescalerIndex` in `src/spwm_math.h` / `src/pwm_timing.h` remains the only prescaler helper in use.
- **DEP-003**: Native Unity discovery of `test/test_*` with `test_build_src = no`.

## 5. Files

- **FILE-001**: `README.md` — operator API and defaults.
- **FILE-002**: `docs/BENCH_CHECKS.md` — disconnected checklist facts.
- **FILE-003**: `src/pwm_utils.h` — drop unused declarations.
- **FILE-004**: `src/pwm_utils.cpp` — drop unused wrappers.
- **FILE-005**: `src/web_handlers.cpp` — drop JSON `engine`.
- **FILE-006**: `docs/REVIEW_FIXES_2026-08-28-4.md` — result artifact.

## 6. Testing

- **TEST-001**: `pio test -e native -f test_config_serde` — serde unchanged.
- **TEST-002**: `pio test -e native -f test_features` — flags still default 1.
- **TEST-003**: `pio test -e native -f test_ota` — ingest/verify headers still compile.
- **TEST-004**: `pio test -e native -f test_spectrum` — FFT/THD and TEGS wire unchanged.
- **TEST-005**: `pio test -e native -f test_thermal_math` — derate math unchanged.
- **TEST-006**: `pio test -e native -f test_waveform` — parse helpers unchanged.

## 7. Risks & Assumptions

- **RISK-001**: Dropping JSON `"engine"` can break an external client that still reads it. Stats already uses TEGS and does not read the field.
- **RISK-002**: Doc rewrites can invent product claims if they replace grid-tie language with a new census or HA acceptance claim.
- **ASSUMPTION-001**: `attachInterruptVectors` and `calculateBestPrescaler` have zero call sites.
- **ASSUMPTION-002**: Production `-Wextra` unused-but-set on `useCmsis` is acceptable; do not move the declaration into `#ifdef` to silence it.

## 8. Related Specifications / Further Reading

- [docs/REVIEW_2026-08-28.md](../docs/REVIEW_2026-08-28.md)
- [docs/REVIEW_FIXES_2026-08-28-3.md](../docs/REVIEW_FIXES_2026-08-28-3.md)
- [plan/refactor-adversarial-fixes-3.md](refactor-adversarial-fixes-3.md)
- [AGENTS.md](../AGENTS.md)
