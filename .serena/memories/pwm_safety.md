# PWM / safety invariants

## Ship gate

NO-SHIP until `docs/BENCH_CHECKS.md` disconnected checklist passes. Target ISR/OUTEN is not host-proven. Review results: `docs/REVIEW_FIXES_2026-08-28.md` and `docs/REVIEW_FIXES_2026-08-28-2.md`. Plans `plan/refactor-adversarial-fixes-1.md` and `plan/refactor-adversarial-fixes-2.md` are Completed. Invariants also in `.github/instructions/teg-pwm-memory.instructions.md`.

## Inhibit / release

- `releaseOutputInhibit()` prepares polarity and timers with OUTEN clear, then commits OUTEN with IRQs off only if `vFaultGeneration` is unchanged. Remask if a fault arrives after connect.
- ACMP hardware gates FlexPWM1 SM3 + FlexPWM2 only. Tm3/Tm4 are software-only.

## Config apply

- Preset/import must disable PWM IRQ via `pwmInterruptRequired()` (not only `spwmActive()`) before memcpy of `MainConfig`.
- `restoreSecrets` always restores the write PIN. MQTT/Influx secrets restore only when endpoint identity matches; otherwise clear the secret and disable that integration.
- `configToJson` omits SyncPwm, CurrentLimit FilterCount/Period, and Tm2 cell PwmFrequency. Reads and validate clamps stay.

## Thermal

- OneWire only while PWM is globally inhibited (skipped while OUTEN live).
- Harvest request 4 s when carrier ≥ 10 kHz while inhibited; 800 ms wait kept.
- Thermal enabled + no valid DS18B20 sample → derate 0 and release refused. Die-only is not enough.

## MTP

- `MTP.begin()` must `MTP.loop()` while still inhibited so the 20 Hz filesystem timer cannot stay armed across a release.
- `mtpAllowsPwmRelease()` gates OUTEN.
- GetObjectHandles / Storage2Store: store index must be `< get_FSCount()`.
- MTP start/service only while globally PWM-inhibited. Write opcodes stay refused.
