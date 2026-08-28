# Adversarial review fixes — slice 2 — 28 August 2026

Second implementation pass of [docs/REVIEW_2026-08-28.md](REVIEW_2026-08-28.md)
after [REVIEW_FIXES_2026-08-28.md](REVIEW_FIXES_2026-08-28.md). Plan:
[plan/refactor-adversarial-fixes-2.md](../plan/refactor-adversarial-fixes-2.md).

Native Unity (`pio test -e native`): `test_config_serde`, `test_ota`,
`test_spectrum`, `test_thermal_math`, `test_waveform` — **80/80 passed**.
Target ISR/OUTEN is not host-proven. NO-SHIP until `docs/BENCH_CHECKS.md`
disconnected checklist still passes.

Settings and stats HTML were opened against a local static server (APIs
absent). Token CSS, Tm2 frequency hide, and deferred `<details>` panels
render. Lite-status hide of the OTA article needs the device (`otaEnabled`).

Canvas (open beside the chat):
`C:\Users\lee\.cursor\projects\d-git-teg\canvases\review-fixes-2026-08-28-2.canvas.tsx`

---

## Done this pass

| ID | Change |
|---|---|
| Poll leftover | Thermal harvest request 4 s when carrier ≥ 10 kHz (inhibited path). RAM Serial 30 s; `Serial` only if `Pwm.Verbose`. |
| PowerMon | `IntervalMs` default and `validateConfig` floor **250**. Saved configs below 250 become 250 on next validate. |
| P2 schema | `configToJson` omits `SyncPwm`, FilterCount/Period, Tm2 cell `PwmFrequency`. Reads and clamps stay. |
| P1-13 | Settings boot skips `/api/ota`, `/api/presets`, `/api/waveform`. Those GETs run on first panel open. |
| Lite / export | `/api/status?lite=1` omits `analogRead` and heavy blocks. `/api/config?download=1` is the export; `/export` stays as alias. |
| P1-9 | `TEG_ENABLE_UNSAFE_LAB_OTA` undefined: header stubs, empty TUs, routes unregistered (404). |
| P1-12 | `web/pico.min.css` is a 2046-byte token sheet. Same URL. |
| P2 OLED | `logs[5]` deleted. `flushDisplay` paints the newest 5 EventLog lines. Inhibit / 1 s gates kept. |

---

## Still deferred

| ID | Why |
|---|---|
| P0-2 / P0-3 | Need a scope: 200 kHz WCET and FAULT0 routing. |
| P0-4 / P1-8 | Kitchen-sink vs `teensy41-pwm` / `USB_SERIAL`. Bare `-DUSB_MTPDISK_SERIAL` stays. |
| P1-11 | `applyPwmConfig` fan-out — hands-off without a bench. |
| ALT-001 | `teg_features.h` / `TEG_WITH_*` and a lean env. |
| ALT-002 | Full 7.3 MiB PSRAM March. |
| ALT-003 / ALT-004 | Bare `yield()` instead of `delay(1)`. QNEthernet still needs the yield contract. |
| ALT-005 | Binary spectrum body. |
| Program 5–7 | MTP `FLASHMEM` move, FFT/gzip/stream to EXTMEM, default-image decision. |

---

## What to bench first (unchanged from slice 1)

1. Fault pin assert during an authenticated clear — outputs must stay off.
2. Enable thermal, confirm release waits for a probe, then generate.
3. Enable MTP, then clear a fault — 20 Hz timer must not stay armed.
4. Import a config with a different MQTT host — password must not follow the host.

Plus this slice: confirm PowerMon I2C at 250 ms, and that production `/api/ota` is 404.
