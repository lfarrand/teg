# Adversarial review fixes — slice 6 — 29 August 2026

Sixth implementation pass of [docs/REVIEW_2026-08-28.md](REVIEW_2026-08-28.md)
after [REVIEW_FIXES_2026-08-28-5.md](REVIEW_FIXES_2026-08-28-5.md). Plan:
[plan/refactor-adversarial-fixes-6.md](../plan/refactor-adversarial-fixes-6.md).

Slice 5 claimed the leftover class was empty. It was not. This slice rewrites
the remaining `restoreSecrets` comments, the README MQTT census, and deletes
three unused duty constexprs. Explorers then found the same stale contract in
`presets.h`, `mqtt.cpp`, and `web_handlers.cpp`; those comments landed in this
pass too. No firmware behavior change. Discovery JSON, `restoreSecrets` logic,
and POST empty-field `preserveSecrets` are unchanged.

Native Unity (`pio test -e native`): six-suite gate **91/91**;
`test_mqtt_discovery` **8/8**. That is not a new census and is not ISR/OUTEN
proof. NO-SHIP until `docs/BENCH_CHECKS.md` disconnected checklist still
passes.

Host-safe leftover class (operator-doc / comment honesty) is now empty.
No further host-testable errors were found. No further host-safe performance
work remains; remaining perf needs a bench or is a stay-off.

Canvas (open beside the chat):
`C:\Users\lee\.cursor\projects\d-git-teg\canvases\review-fixes-2026-08-28-6.canvas.tsx`

---

## Done this pass (priority: close the leftover class)

| ID | Change |
|---|---|
| CMT-PRE | `src/presets.cpp` header and `src/presets.h` now describe identity-gated `restoreSecrets` (PIN always; MQTT/Influx only on matching endpoint). |
| CMT-IMP | `web/index.html` import comment and `src/web_handlers.cpp` export/import comments match that contract. POST empty-field `preserveSecrets` stays. |
| DOC-SEC | `docs/SECURITY.md` keeps empty-field preserve and adds the import/preset identity gate. |
| DOC-MQTT | `README.md` MQTT device census is 17: 16 `MqttSensors[]` plus fault. Four aux sensors named. No `aux_alert`. No HA dashboard language. `src/mqtt.cpp` burst comment is 17. |
| DEAD-DUTY | Deleted unused `MinDutyCycle` / `MidDutyCycle` / `MaxDutyCycle` from `src/pwm_utils.h`. `MAX_COUNTER_VALUE` and `SpwmMidDuty` stay. |

---

## More fixes / enhancements / performance (ranked)

### Host-safe leftovers

None. Explorers covered comments, serde/MQTT/spectrum/thermal tests, and
`stats.html` TEGS parse. The leftover class is empty.

### Host-testable errors

None outside this slice. `restoreSecrets` tests already encode the identity
gate. MQTT tests walk `MqttSensorCount`. Spectrum wire and thermal math match
their suites.

### Host-safe performance

None. LoopHz 250, lite status, TEGS / 2 s spectrum floor, PowerMon 250, RAM
Serial 30 s, OLED 1 s, Influx 20 ms drain, pico token sheet, and deferred
presets/waveform GET already landed. Remaining perf is a stay-off or needs a
scope.

### Still deferred (needs a bench or a contested decision)

Ranked by operator risk. Do not implement from the host.

| Priority | ID | Why |
|---|---|---|
| P0 | Polarity 2 / 5 / 4-POD | MASK/fault software path is unscoped on hardware (`docs/BENCH_CHECKS.md` §0a). Highest live-stage risk if those schemes are energized. |
| P0 | P0-3 FAULT0 | CMP→XBARA1→FAULT0 unconfirmed. Tm3/Tm4 stay software-only. |
| P0 | Bench #1 | Fault pin during authenticated clear — outputs must stay off. |
| P0 | P0-2 WCET | 200 kHz ISR + dual-ADC is a budget, not a measurement. |
| P0 | P0-4 / P1-8 | Kitchen-sink vs lean USB. Bare `-DUSB_MTPDISK_SERIAL` stays. |
| P1 | Thermal bench | Enable, wait for a probe, then generate. OneWire stays inhibited-only. |
| P1 | MTP then fault-clear | 20 Hz timer must not stay armed. |
| P1 | Import different MQTT host | Password must not follow the host. |
| P1 | P1-11 | `applyPwmConfig` still runs capture / thermal / PLL on every apply. |
| P1 | ALT-002 | Full 7.3 MiB PSRAM March. 1 KiB complementary prove already landed. |
| P1 | ALT-003 / ALT-004 | Bare `yield()` instead of `delay(1)`. QNEthernet still needs the yield contract. |
| P1 | Program 5–7 | MTP `FLASHMEM`, FFT/gzip/stream to EXTMEM, default-image. |
| P1 | ALT-001 wrap | `lib_ignore` / call-site `#if` when `TEG_WITH_*=0`. Flags stay unused and ON. |

---

## What to bench first (unchanged from slice 1)

1. Fault pin assert during an authenticated clear — outputs must stay off.
2. Enable thermal, confirm release waits for a probe, then generate.
3. Enable MTP, then clear a fault — 20 Hz timer must not stay armed.
4. Import a config with a different MQTT host — password must not follow the host.
