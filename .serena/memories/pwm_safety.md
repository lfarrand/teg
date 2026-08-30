# PWM / safety invariants

## Ship gate

NO-SHIP until `docs/BENCH_CHECKS.md` disconnected checklist passes. Host Unity is not ISR/OUTEN proof. Review slices 1–6 Completed (`docs/REVIEW_FIXES_2026-08-28.md` through `-6.md`, plans `refactor-adversarial-fixes-1`–`6`). Dated 91/91 lines in those notes stay as snapshots; operator README/BENCH_CHECKS name six suites plus `test_spectrum_wire_quantize_saturates` without a numeric total. Host-safe leftover class is empty; do not create `plan/refactor-adversarial-fixes-7.md`. Remaining work is disconnected bench evidence in `docs/BENCH_CHECKS.md`. Invariants also in `.github/instructions/teg-pwm-memory.instructions.md`.

## Inhibit / release

- `releaseOutputInhibit()` prepares polarity and timers with OUTEN clear, then commits OUTEN with IRQs off only if `vFaultGeneration` is unchanged. Remask if a fault arrives after connect.
- ACMP hardware gates FlexPWM1 SM3 + FlexPWM2 only. Tm3/Tm4 are software-only.

## Config apply

- Preset/import must disable PWM IRQ via `pwmInterruptRequired()` (not only `spwmActive()`) before memcpy of `MainConfig`.
- `restoreSecrets` always restores the write PIN. MQTT/Influx secrets restore only when endpoint identity matches; otherwise clear the secret and disable that integration. Do not merge with `preserveSecrets` (POST empty-field preserve stays).
- `configToJson` omits SyncPwm, CurrentLimit FilterCount/Period, Tm2 cell PwmFrequency, and Tm2.DeadTimeCompensation. Reads and validate clamps stay.

## Thermal

- OneWire only while PWM is globally inhibited (skipped while OUTEN live).
- `thermalConfigure()` keeps `haveValidExternalSample` when thermal stays enabled on the same OneWire pin. Pin change or first enable fail-closes, `setThermalDerateMilli(0)`, and if OUTEN is live trips + masks *before* `cacheProbeAddresses()`.
- Harvest request 4 s when carrier ≥ 10 kHz while inhibited; 800 ms wait kept.
- Thermal enabled + no valid DS18B20 sample → derate 0 and release refused. Die-only is not enough.

## MTP

- USB is always composite (`-DUSB_MTPDISK_SERIAL`, never `=1`). `Mtp.Enabled` defaults false; enabling does not require a reboot (`mtpTask()` `MTP.begin()` while inhibited). Settings `#mtp-status` waits for the next inhibited pass (do not restore "reboot to start").
- `MTP.begin()` must `MTP.loop()` while still inhibited so the 20 Hz filesystem timer cannot stay armed across a release.
- `mtpAllowsPwmRelease()` gates OUTEN.
- GetObjectHandles / Storage2Store: store index must be `< get_FSCount()`.
- MTP start/service only while globally PWM-inhibited. Write opcodes stay refused.
