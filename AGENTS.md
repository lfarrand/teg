## Learned User Preferences

- Use Context7 for library, PlatformIO, and code-review lookups.
- Land `main` through a pull request; squash-merge and delete the feature branch.
- Review Codex PR comments and resolve them after applicable fixes.
- Do not invent product claims; host tests and README text are not bench proof. Target ISR/OUTEN is not host-proven.
- Run local work in PowerShell; translate Ubuntu CI bash before executing it here.
- Keep Serena project memories after layout, build, or convention changes.
- PWM/thermal/import invariants also live in `.github/instructions/teg-pwm-memory.instructions.md`. Do not add a `teensy41-pwm` env or skip `applyPwmConfig` capture/thermal/PLL.
- Keep `delay(1)` in the control loop (QNEthernet yield); do not replace it with bare `yield()`.

## Learned Workspace Facts

- Teensy 4.1 TEG PWM inverter firmware; bench instrument; NO-SHIP until `docs/BENCH_CHECKS.md` disconnected checklist passes. Adversarial review 2026-08-28: slice 1 `docs/REVIEW_FIXES_2026-08-28.md` (`plan/refactor-adversarial-fixes-1.md` Completed); slice 2 `docs/REVIEW_FIXES_2026-08-28-2.md` (`plan/refactor-adversarial-fixes-2.md` Completed); slice 3 `docs/REVIEW_FIXES_2026-08-28-3.md` (`plan/refactor-adversarial-fixes-3.md` Completed).
- USB composite flag must be bare `-DUSB_MTPDISK_SERIAL` (never `=1`); PID becomes `0x04D5`. MTP is patched Teensyduino 1.62 in `scripts/mtp_core162/`; `lib/MTP_Teensy` is `mtp_wdog.h` only; write opcodes stay refused. `MTP.begin()` must `MTP.loop()` while still inhibited; `mtpAllowsPwmRelease()` gates OUTEN. GetObjectHandles / Storage2Store store `< get_FSCount()`. `TEG_WITH_MTP_SERVICE=0` later skips begin/loop only; USB stays bare `-DUSB_MTPDISK_SERIAL`.
- Framework is copy-on-write to `.pio/framework-arduinoteensy-teg`; do not write the global PlatformIO package. Do not restore old KurtE MTP headers on the include path. Teensy platform, framework, toolchain, and `tool-teensy` must move together (currently 1.62 / GCC 15.2.1); the auto-updater skips them. QNEthernet registry versions are `0.x`, not `3.x`.
- Portable radix-2 FFT is the tested default; do not enable `TEG_ENABLE_CMSIS_FFT` or global `-O3` (RAM1 floor). Spectrum default 1024 / JSON bins ≤128 / `SpectrumMaxPoints` 4096; capture clamp 32768. Binary GET `/api/spectrum?format=bin` uses a 32-byte LE TEGS header (`src/spectrum_wire.h`); unavailable is HTTP 200 with flags=0; JSON mag uses `spectrumWireQuantize` / 10000. Stats floors spectrum GETs at 2 s and polls status → log → spectrum. CMSIS UI picker and DTC checkbox are gone.
- `releaseOutputInhibit()` commits OUTEN IRQ-off only if `vFaultGeneration` is unchanged; remask if a fault arrives after connect. ACMP hardware gates FlexPWM1 SM3 + FlexPWM2; Tm3/Tm4 are software-only.
- Preset/import must disable PWM IRQ via `pwmInterruptRequired()` (not only `spwmActive()`) before memcpy of `MainConfig`.
- `restoreSecrets` always restores the write PIN; MQTT/Influx secrets only when endpoint identity matches, otherwise clear and disable.
- Thermal: OneWire only while PWM is inhibited (skipped while OUTEN live). Harvest request 4 s when carrier ≥ 10 kHz while inhibited; 800 ms wait kept. Enabled + no DS18B20 sample → derate 0 and release refused (die-only is not enough).
- `native` tests include host-testable headers only (`test_build_src = no`); firmware `.cpp` is target-only. PlatformIO discovers `test/test_*` as independent mains. Host suites cover serde/ota/spectrum/thermal_math/waveform/features (91/91), not ISR/OUTEN.
- 8 MiB PSRAM is mandatory; WDOG1 is 8 s; MTP runs only while PWM is globally inhibited. `Feedback.LoopHz` default 250. `PowerMon.IntervalMs` default and validate floor 250. RAM Serial 30 s; Serial only if `Pwm.Verbose`. `include/defines.h`, `printDigits`, and `enableXbar` are deleted.
- Production `TEG_ENABLE_UNSAFE_LAB_OTA` is undefined: `ota.h` stubs, empty `ota.cpp`/`flash_ota.cpp`, no `/api/ota` routes. `configToJson` omits SyncPwm, CurrentLimit FilterCount/Period, Tm2 cell PwmFrequency, and Tm2.DeadTimeCompensation; reads and validate clamps stay. SchemaVersion stays 1. `teg_features.h` defaults `TEG_WITH_OLED/THERMAL/MQTT/INFLUX/SPECTRUM/POWERMON/MTP_SERVICE` to 1; do not `lib_ignore` from a flag or `#if` call sites yet.
- Settings: `/api/status?lite=1` (no analogRead); presets/waveform GET on first panel open; export `/api/config?download=1`. `pico.min.css` is a ~2 KB token sheet at the same URL. OLED paints newest 5 EventLog lines; `logs[5]` deleted.
