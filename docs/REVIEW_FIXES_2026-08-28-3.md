# Adversarial review fixes — slice 3 — 28 August 2026

Third implementation pass of [docs/REVIEW_2026-08-28.md](REVIEW_2026-08-28.md)
after [REVIEW_FIXES_2026-08-28-2.md](REVIEW_FIXES_2026-08-28-2.md). Plan:
[plan/refactor-adversarial-fixes-3.md](../plan/refactor-adversarial-fixes-3.md).

Native Unity (`pio test -e native`): `test_config_serde`, `test_features`,
`test_ota`, `test_spectrum`, `test_thermal_math`, `test_waveform` —
**91/91 passed**. Target ISR/OUTEN is not host-proven. NO-SHIP until
`docs/BENCH_CHECKS.md` disconnected checklist still passes.

Stats HTML was opened against a local static server (APIs absent). The page
parses; binary spectrum needs the device (`?format=bin`).

Canvas (open beside the chat):
`C:\Users\lee\.cursor\projects\d-git-teg\canvases\review-fixes-2026-08-28-3.canvas.tsx`

---

## Done this pass

| ID | Change |
|---|---|
| ALT-005 | 32-byte little-endian `TEGS` body. `GET /api/spectrum?format=bin` is `application/octet-stream` on the existing PIN route. Unavailable is HTTP 200 with flags=0. JSON `mag[]` uses the same `spectrumWireQuantize` / 10000 scale. |
| Stats poll | `pollSpectrum` GETs `?format=bin`, floors at 2000 ms, keeps `specBusy`. Poll order is status → log → spectrum. |
| P2-DTC | `configToJson` omits `Tm2.DeadTimeCompensation`. Read and `validateConfig` force-false stay. SchemaVersion stays 1. |
| ALT-001 skeleton | `src/teg_features.h` defaults seven `TEG_WITH_*` flags to 1. No call-site `#if`. No `lib_ignore` from a flag. |

---

## Still deferred

| ID | Why |
|---|---|
| P0-2 / P0-3 | Need a scope: 200 kHz WCET and FAULT0 routing. |
| P0-4 / P1-8 | Kitchen-sink vs `teensy41-pwm` / `USB_SERIAL`. Bare `-DUSB_MTPDISK_SERIAL` stays. |
| P1-11 | `applyPwmConfig` fan-out — hands-off without a bench. |
| ALT-001 wrap | `lib_ignore` / call-site `#if` when `TEG_WITH_*=0`. Flags stay unused and ON. |
| ALT-002 | Full 7.3 MiB PSRAM March. |
| ALT-003 / ALT-004 | Bare `yield()` instead of `delay(1)`. QNEthernet still needs the yield contract. |
| Program 5–7 | MTP `FLASHMEM` move, FFT/gzip/stream to EXTMEM, default-image decision. |

---

## TEGS layout (host-proven)

| Offset | Type | Field |
|---|---|---|
| 0 | 4 ASCII | `TEGS` |
| 4 | u8 | version `1` |
| 5 | u8 | flags bit0 = available |
| 6 | u16 LE | binCount (0 if unavailable; else `min(points/2, 128)`) |
| 8 | u32 LE | sampleHz |
| 12 | u32 LE | points |
| 16 | u32 LE | computeMicros |
| 20 | f32 LE | binHz |
| 24 | f32 LE | fundamentalHz |
| 28 | f32 LE | thdPercent |
| 32+ | u16 LE | `round(mag/fund * 10000)` |

Stack body cap is `SpectrumWireMaxBody` (288 bytes). Field-by-field `memcpy`, not a packed struct.

---

## What to bench first (unchanged from slice 1)

1. Fault pin assert during an authenticated clear — outputs must stay off.
2. Enable thermal, confirm release waits for a probe, then generate.
3. Enable MTP, then clear a fault — 20 Hz timer must not stay armed.
4. Import a config with a different MQTT host — password must not follow the host.

Plus this slice: with capture running, confirm `/api/spectrum?format=bin` is 200 and the stats plot updates at no more than 2 Hz.
