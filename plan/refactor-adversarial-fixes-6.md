---
goal: Close remaining host-safe restoreSecrets comments, MQTT entity census, and unused duty constexprs
version: 1.0
date_created: 2026-08-29
last_updated: 2026-08-29
owner: teg
status: 'Completed'
tags: [refactor, docs]
---

# Introduction

![Status: Completed](https://img.shields.io/badge/status-Completed-brightgreen)

Sixth slice after `plan/refactor-adversarial-fixes-5.md`. Slice 5 closed its named files but left a false presets header, an import JS comment, a SECURITY.md import-contract gap, a stale README MQTT census (13 vs 17), and three unused duty constexprs. Docs and dead chrome only. Do not change USB, `delay(1)`, `applyPwmConfig`, FlexPWM geometry, or `restoreSecrets` logic.

## 1. Requirements & Constraints

- **REQ-001**: `src/presets.cpp` file header must describe `restoreSecrets` (PIN always; MQTT/Influx identity-gated). It must not say `preserveSecrets()` or “never wipes credentials”.
- **REQ-002**: `web/index.html` import-handler comment must match identity-gated restore, not “secret preservation”.
- **REQ-003**: `docs/SECURITY.md` must keep empty-field preserve for `POST /api/config` and add identity-gated import/preset restore.
- **REQ-004**: `README.md` MQTT device census must be 17 entities: 16 `MqttSensors[]` plus fault. Include the four aux sensors. Do not add `aux_alert`. Do not add HA energy-dashboard language.
- **REQ-005**: Delete `MinDutyCycle`, `MidDutyCycle`, and `MaxDutyCycle` from `src/pwm_utils.h`. Keep `MAX_COUNTER_VALUE` and `SpwmMidDuty`.
- **CON-001**: Bare `-DUSB_MTPDISK_SERIAL`. Keep `delay(1)`. Do not skip `applyPwmConfig` capture/thermal/PLL.
- **CON-002**: Do not merge `preserveSecrets` and `restoreSecrets`. Empty-field POST preserve stays.
- **GUD-001**: One writer per file. Comment/doc only except the three unused constexpr deletes.
- **GUD-002**: Do not invent a new test census. Six-suite gate stays 91/91.
- **PAT-001**: Secret comments copy the landed `restoreSecrets` contract in `src/config_serde.h`.
- **SEC-001**: Import/preset cannot set secrets from the file. PIN always restored.

## 2. Implementation Steps

### Implementation Phase 1

- GOAL-001: Five file-exclusive honesty/dead-chrome edits in parallel.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-001 | Rewrite `src/presets.cpp` lines 5–7 to `restoreSecrets`. Keep “secrets blanked on write”. Do not change the load path. | ✅ | 2026-08-29 |
| TASK-002 | Rewrite `web/index.html` lines 779–780 import comment to identity-gated restore. Keep “rejects incomplete documents”. | ✅ | 2026-08-29 |
| TASK-003 | In `docs/SECURITY.md` after the empty-field preserve sentence, add import/preset identity-gated `restoreSecrets`. | ✅ | 2026-08-29 |
| TASK-004 | In `README.md` MQTT section, change 13 entities to 17 and name the four aux sensors. | ✅ | 2026-08-29 |
| TASK-005 | Delete `MinDutyCycle`, `MidDutyCycle`, and `MaxDutyCycle` from `src/pwm_utils.h` only. Keep `MAX_COUNTER_VALUE`. | ✅ | 2026-08-29 |

### Implementation Phase 2

- GOAL-002: Verify host suites and write the slice artifact.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-006 | Run `pio test -e native` for `test_config_serde`, `test_mqtt_discovery`, `test_spectrum`, `test_features`, `test_ota`, `test_thermal_math`, `test_waveform`. | ✅ | 2026-08-29 |
| TASK-007 | Write `docs/REVIEW_FIXES_2026-08-28-6.md` and mark this plan Completed. | ✅ | 2026-08-29 |

## 3. Alternatives

- **ALT-001**: Merge `preserveSecrets` into `restoreSecrets` — rejected; they are different contracts (POST empty-field vs file import).
- **ALT-002**: Count `aux_alert` as an entity — rejected; it is a state key only.
- **ALT-003**: Delete `MAX_COUNTER_VALUE` with the unused duty constexprs — rejected; it is live in `src/pwm_utils.cpp`.

## 4. Dependencies

- **DEP-001**: `restoreSecrets` already implemented in `src/config_serde.h` and used by `src/presets.cpp` load.
- **DEP-002**: Settings hint at `web/index.html` line 285 already matches identity-gated restore.
- **DEP-003**: `MqttSensors[]` has 16 rows; `mqttBuildFaultDiscovery` adds the 17th entity.

## 5. Files

- **FILE-001**: `src/presets.cpp` — header comment.
- **FILE-002**: `web/index.html` — import JS comment.
- **FILE-003**: `docs/SECURITY.md` — import identity-gate sentence.
- **FILE-004**: `README.md` — MQTT entity census.
- **FILE-005**: `src/pwm_utils.h` — unused duty constexprs.
- **FILE-006**: `docs/REVIEW_FIXES_2026-08-28-6.md` — result artifact.

## 6. Testing

- **TEST-001**: `pio test -e native -f test_config_serde` — `restoreSecrets` tests unchanged.
- **TEST-002**: `pio test -e native -f test_mqtt_discovery` — 8/8; 16 sensors + fault still published.
- **TEST-003**: `pio test -e native -f test_spectrum` — TEGS unchanged.
- **TEST-004**: `pio test -e native -f test_features` — flags still default 1.
- **TEST-005**: `pio test -e native -f test_ota` — ingest/verify headers still compile.
- **TEST-006**: `pio test -e native -f test_thermal_math` — derate math unchanged.
- **TEST-007**: `pio test -e native -f test_waveform` — parse helpers unchanged.

## 7. Risks & Assumptions

- **RISK-001**: Counting MQTT entities from comments instead of `MqttSensors[]` will stale again if aux sensors change.
- **ASSUMPTION-001**: The three duty constexprs have zero call sites in firmware and tests.
- **ASSUMPTION-002**: After these five land, no further host-safe leftover from the 28 August honesty pass remains.

## 8. Related Specifications / Further Reading

- [docs/REVIEW_FIXES_2026-08-28-5.md](../docs/REVIEW_FIXES_2026-08-28-5.md)
- [plan/refactor-adversarial-fixes-5.md](refactor-adversarial-fixes-5.md)
- [src/config_serde.h](../src/config_serde.h)
- [src/mqtt_discovery.h](../src/mqtt_discovery.h)
