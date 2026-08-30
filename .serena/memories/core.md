# TEG

Teensy 4.1 (i.MX RT1062) PWM inverter controller for a TEG power stage. PlatformIO firmware in `src/`; gzip-embedded SPA in `web/`; host-testable math in `src/*_math.h` and related headers; CI/SBOM helpers in `scripts/`.

## Ship posture

NO-SHIP for an energised or unattended power stage until `docs/BENCH_CHECKS.md` disconnected checklist passes. Target ISR/OUTEN is not host-proven. PRs #18–#29 remain bench-unverified. PWM inhibit/release (`vFaultGeneration` + IRQ-off OUTEN), ACMP vs software gates, thermal/OneWire, preset IRQ, and `restoreSecrets`: `mem:pwm_safety`. Software findings from `docs/REVIEW_2026-08-01.md` and `docs/REVIEW_FIXES_2026-08-28.md` through `-6.md` are remediated in-tree; plans `plan/refactor-adversarial-fixes-1.md` through `-6.md` are Completed. Dated 91/91 lines in those notes stay as snapshots. FlexPWM, ACMP/XBAR, ADC/ISR budgets, PSRAM, and MTP are not host-provable.

This is a bench instrument: HTTP/MQTT/Influx have no TLS; the PIN is cleartext. PLL is a bench reference lock, not grid-tie. Operator README/BENCH_CHECKS/SECURITY/CI_SECURITY/PRODUCT_READINESS/`web/index.html`/teg-pwm-memory name landed #67/#68 contracts; they are not bench proof. That operator-copy alignment is docs/comments/memories-only PR #69 (do not commit those refreshes on `main`). Do not create `plan/refactor-adversarial-fixes-7.md`; remaining work is disconnected `docs/BENCH_CHECKS.md` evidence. Production `TEG_ENABLE_UNSAFE_LAB_OTA` is undefined (`ota.h` stubs, empty `ota.cpp`/`flash_ota.cpp`, no `/api/ota` routes). Isolation assumptions: `docs/SECURITY.md`. Product-vs-instrument gap: `docs/PRODUCT_READINESS.md`.

## Tree

- `src/` — firmware. `*_math.h` / selected headers are host-tested; `*.cpp` is target-only (`native` builds no firmware `.cpp`).
- `web/` — `index.html`, `pico.min.css` (~2 KB token sheet at the same URL), `stats.html`; `scripts/gzip_web_assets.py` emits gitignored `src/web_assets.h`.
- `lib/aWOT`, `lib/eFlexPwm` — gitlink forks. Commit and push in the submodule repo before moving the parent gitlink. Patches: each `PATCHES.md`.
- `lib/MTP_Teensy` — `mtp_wdog.h` plus patches applied to `scripts/mtp_core162/` (Teensyduino 1.62 MTP). Details: `lib/MTP_Teensy/PATCHES.md`.
- `lib/miniz` — vendored inflate-only.
- `test/test_*/test_main.cpp` — Unity suites for `pio test -e native` (six-suite serde/ota/spectrum/thermal_math/waveform/features plus `test_spectrum_wire_quantize_saturates`; also `test_mqtt_discovery`). Host Unity is not ISR/OUTEN proof.
- `fuzz/`, `benchmark/` — parser libFuzzer and Google Benchmark (CI jobs, not Cortex-M7 evidence).
- `compile_commands.json` — present at repo root for the C++ language server.

## Hard invariants

- 8 MiB PSRAM is mandatory. Unproven/failed PSRAM keeps PWM drivers disconnected. Large capture/waveform buffers are `EXTMEM`.
- Thermal enabled + no valid DS18B20 sample → derate 0 and release refused (die-only is not enough). OneWire only while PWM is inhibited (skipped while OUTEN live). `thermalConfigure()` keeps `haveValidExternalSample` on the same OneWire pin; pin change or first enable fail-closes and, if OUTEN is live, trips + masks *before* `cacheProbeAddresses()`. Harvest request 4 s when carrier ≥ 10 kHz while inhibited; 800 ms wait kept.
- Missing/invalid settings or failed PIN persist → provisioning interlock; fault-clear cannot bypass it.
- Pair modes that cannot be honoured hold **both** outputs off (never silently revert to independent).
- Polarity-inverting schemes (2, 5, 4+POD/APOD): MASK/fault act before polarity; firmware un-inverts before mask. Scope before trusting.
- WDOG1 is 8 s and cannot be widened once enabled. Long loops must `kickWatchdog()` / `serviceControlTasks()`; do not kick unbounded host-controlled walks (MTP delete/move).
- MTP is maintenance-only: start/service only while globally PWM-inhibited. `MTP.begin()` must `MTP.loop()` while still inhibited; `mtpAllowsPwmRelease()` gates OUTEN. Index on QSPI, not SD next to `/settings.cfg`.
- Canonical FlexPWM2 carrier drives cells + DSP. Apply path: disconnect drivers → program → one LDOK → arm/sample protections → reconnect.
- N-cell arbitrary phase-shift needs RT1170 (`docs/RT1170_PSPWM.md`); RT1062 phase-shifted mode is 180° alternation only.
- `PowerMon.IntervalMs` default and validate floor 250. RAM Serial 30 s; Serial only if `Pwm.Verbose`. OLED paints newest 5 EventLog lines; `logs[5]` deleted.

## Follow-on memories

Pins, languages, and CI tool versions: `mem:tech_stack`.
Local/Windows commands: `mem:suggested_commands`.
Header/test/build-flag landmines: `mem:conventions`.
What to run before calling a change done: `mem:task_completion`.
PWM inhibit/release races, ACMP vs software gates, thermal/OneWire, preset IRQ, restoreSecrets: `mem:pwm_safety`.
