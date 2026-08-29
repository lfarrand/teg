# Adversarial review fixes — slice 4 — 29 August 2026

Fourth implementation pass of [docs/REVIEW_2026-08-28.md](REVIEW_2026-08-28.md)
after [REVIEW_FIXES_2026-08-28-3.md](REVIEW_FIXES_2026-08-28-3.md). Plan:
[plan/refactor-adversarial-fixes-4.md](../plan/refactor-adversarial-fixes-4.md).

No ranked P0/P1 item remained that was host-safe. This slice aligns operator
docs to landed contracts and deletes unused chrome. Native Unity
(`pio test -e native`) on `test_config_serde`, `test_features`, `test_ota`,
`test_spectrum`, `test_thermal_math`, `test_waveform` — **91/91 passed**.
Target ISR/OUTEN is not host-proven. NO-SHIP until `docs/BENCH_CHECKS.md`
disconnected checklist still passes.

This is the last honest host-safe leftover from the 28 August review.
Remaining items need a bench or are hard stay-offs.

Canvas (open beside the chat):
`C:\Users\lee\.cursor\projects\d-git-teg\canvases\review-fixes-2026-08-28-4.canvas.tsx`

---

## Done this pass

| ID | Change |
|---|---|
| DOC-001 | `README.md` and `docs/BENCH_CHECKS.md` match landed facts: LoopHz 250, TEGS `?format=bin`, production OTA 404, identity-gated import secrets, export `?download=1`, `?lite=1`, PowerMon 250, pico token sheet. Dropped the 319-test census and grid-tie / HA energy-dashboard product language. |
| P2-VEC | Deleted unused `attachInterruptVectors` and `calculateBestPrescaler`. `attachModule2PwmInterruptVectors` and `bestPrescalerIndex` stay. |
| P2-ENGINE | JSON `/api/spectrum` no longer emits `"engine"`. TEGS path and CMSIS `#ifdef` blocks unchanged. |

---

## Still deferred (needs a bench or a contested decision)

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

## What to bench first (unchanged from slice 1)

1. Fault pin assert during an authenticated clear — outputs must stay off.
2. Enable thermal, confirm release waits for a probe, then generate.
3. Enable MTP, then clear a fault — 20 Hz timer must not stay armed.
4. Import a config with a different MQTT host — password must not follow the host.

Plus later slices: PowerMon I2C at 250 ms, production `/api/ota` is 404, and
`/api/spectrum?format=bin` updates the stats plot at no more than 2 Hz.
