# Adversarial review fixes — 28 August 2026

Implementation of [docs/REVIEW_2026-08-28.md](REVIEW_2026-08-28.md) in priority
order. Plan: [plan/refactor-adversarial-fixes-1.md](../plan/refactor-adversarial-fixes-1.md).

Native Unity (`pio test -e native`): `test_config_serde`, `test_spectrum`,
`test_thermal_math`, `test_waveform` — **58/58 passed** after the
code-simplifier pass. Target ISR/OUTEN behaviour is not host-proven. NO-SHIP
until `docs/BENCH_CHECKS.md` disconnected checklist still passes.

Canvas (open beside the chat):
`C:\Users\lee\.cursor\projects\d-git-teg\canvases\review-fixes-2026-08-28.canvas.tsx`

---

## Done

| ID | Change |
|---|---|
| P0-5 | `releaseOutputInhibit` uses `vFaultGeneration`, IRQ-off OUTEN commit, and remask if a fault appears after connect. |
| P0-1 / P1-15 | OneWire harvest only while inhibited. Thermal enabled + no DS18B20 sample → derate 0 and release refused. |
| P1-14 | Preset/import uses `pwmInterruptRequired()` around `MainConfig` memcpy. |
| P1-18 | `restoreSecrets` keeps MQTT/Influx secrets only when endpoint identity matches; else clear and disable. PIN always restored. |
| P1-17 | `configApiRejectReason` → HTTP 422 / import fail. Boot `validateConfig` still sanitizes. |
| P1-20 | `MTP.begin()` runs `MTP.loop()` immediately while inhibited. `mtpAllowsPwmRelease()` gates OUTEN. |
| P1-19 | Host StorageID → store index bounds-checked (`store < get_FSCount()`). |
| P1-7 | PSRAM prove: 0xAA and 0x55 over all 1024 bytes via volatile reads. |
| P1-16 | Waveform staging checks write/seek/sync; promotion parses TEGW header, not length only. |
| P1-1 / P1-2 | Stats: in-flight lock, 1 s floor, spectrum auto off, default 1024 points, 128 JSON bins. |
| P1-3 | MQTT TCP/CONNACK only while inhibited. |
| P1-4 / P1-5 | OLED flush 1 s and skipped while generating; Influx drain 20 ms. |
| P1-6 | `Feedback.LoopHz` default **250**. `delay(1)` kept (QNEthernet yield contract). |
| P1-10 | `/api/capture` count clamped at 32768. |
| P2 | Deleted `include/defines.h`, `printDigits`, `enableXbar`; CMSIS dropdown and DTC checkbox removed. |

---

## Deferred after slice 1 (slice 2 landed the I/O leftovers)

P1-9, P1-12, P1-13, PowerMon 250, thermal 4 s, RAM Serial, schema emit slims,
lite status, export alias, and OLED EventLog: see
[REVIEW_FIXES_2026-08-28-2.md](REVIEW_FIXES_2026-08-28-2.md).

Still open: P0-2 / P0-3 (scope), P0-4 / P1-8 (USB / lean env), P1-11
(`applyPwmConfig`), ALT-002 (7.3 MiB March), ALT-004 (`delay(1)`).

---

## What to bench first

1. Fault pin assert during an authenticated clear — outputs must stay off.
2. Enable thermal, confirm release waits for a probe, then generate: `thermalMissedCycles` must not climb from OneWire.
3. Enable MTP, then clear a fault — host must not keep the 20 Hz timer after OUTEN.
4. Import a config with a different MQTT host — password must not go to the new broker.
