# Adversarial review fixes — slice 5 — 29 August 2026

Fifth implementation pass of [docs/REVIEW_2026-08-28.md](REVIEW_2026-08-28.md)
after [REVIEW_FIXES_2026-08-28-4.md](REVIEW_FIXES_2026-08-28-4.md). Plan:
[plan/refactor-adversarial-fixes-5.md](../plan/refactor-adversarial-fixes-5.md).

Slice 4 cleaned README and `docs/BENCH_CHECKS.md` but left SECURITY.md, the
settings MQTT hint, the stats `(portable)` label, and four comment/test names.
This slice rewrites those only. Discovery JSON fields and MPPT defaults are
unchanged.

Native Unity (`pio test -e native`): six-suite gate **91/91**;
`test_mqtt_discovery` **8/8** (renamed payload-shape test). That is not a new
census and is not ISR/OUTEN proof. NO-SHIP until `docs/BENCH_CHECKS.md`
disconnected checklist still passes.

This leftover class (operator-doc / comment honesty) is now empty. Remaining
review items need a bench or are hard stay-offs.

Canvas (open beside the chat):
`C:\Users\lee\.cursor\projects\d-git-teg\canvases\review-fixes-2026-08-28-5.canvas.tsx`

---

## Done this pass

| ID | Change |
|---|---|
| DOC-SEC | `docs/SECURITY.md`: production `/api/ota*` is unregistered → HTTP 404, not “reports disabled”. |
| DOC-MQTT | Settings MQTT hint is bench telemetry + HA discovery. Dropped “energy for the HA dashboard”. Preset hint now matches identity-gated `restoreSecrets`. |
| UI-SPEC | Stats spec-info drops `(portable)`. TEGS poll, 2 s floor, and poll order stay. |
| CMT-DISC | `mqtt_discovery.h` energy comment is payload shape only. `MqttSensors[]` unchanged. |
| TST-DISC | `test_energy_sensor_payload_shape` keeps energy / total_increasing / Wh assertions. |
| CMT-MPPT | `config_json.h` MPPT comment is bench pairing, not grid-tie topology. Defaults unchanged. |
| CMT-FFT | `web_handlers.cpp` CMSIS comment no longer says JSON reports `"portable"`. |

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
