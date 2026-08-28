---
description: TEG PWM release, thermal, and import-safety invariants
applyTo: "src/**/*.{cpp,h},web/**/*.{html,js,css},scripts/mtp_core162/**"
---

# TEG PWM Memory

Fail-closed release, identity-gated secrets, and no OneWire while OUTEN is live.

## Release commit

`releaseOutputInhibit()` prepares polarity and timers with OUTEN clear, then commits OUTEN only if `vFaultGeneration` is unchanged. A concurrent fault ISR must win. ACMP hardware gates FlexPWM1 SM3 and FlexPWM2; Tm3/Tm4 stay software-masked after the post-check.

## Publication

Preset/import disables the PWM2 IRQ with `pwmInterruptRequired()` before memcpy of `MainConfig`, and re-enables only through that helper after apply.

## Secrets

`restoreSecrets` always restores the write PIN. MQTT/Influx secrets restore only when the endpoint identity is unchanged; otherwise clear the secret and disable that integration.

## Thermal

When thermal is enabled, PWM release waits for a valid DS18B20 sample. OneWire bit slots do not run while outputs are connected. Missing probes fail closed (derate 0), not full output. While inhibited, request conversions every 4 s when the carrier is ≥ 10 kHz (else 2 s); keep the 800 ms harvest wait.

## MTP

`MTP.begin()` must run `MTP.loop()` while still inhibited so the 20 Hz filesystem timer cannot stay armed across a release.

## Schema emit

`configToJson` omits `Pwm.SyncPwm`, `CurrentLimit.FilterCount` / `FilterPeriod`, Tm2 cell `PwmFrequency`, and `Tm2.DeadTimeCompensation`. Keep reading those keys and keep `validateConfig` clamps. `PowerMon.IntervalMs` default and floor are 250.

## Spectrum wire

`/api/spectrum?format=bin` is a 32-byte little-endian `TEGS` header plus `u16` bins on the existing PIN route. Unavailable is HTTP 200 with flags=0, not 404. JSON `mag[]` uses the same `spectrumWireQuantize` / 10000 scale. Stats floors spectrum GETs at 2 s and polls status → log → spectrum. Do not wrap the route in `TEG_WITH_SPECTRUM` yet.

## Feature flags

`src/teg_features.h` defaults `TEG_WITH_OLED`, `THERMAL`, `MQTT`, `INFLUX`, `SPECTRUM`, `POWERMON`, and `MTP_SERVICE` to 1. Do not `lib_ignore` from a flag. Do not `#if` call sites until a later compile-graph slice. `TEG_WITH_MTP_SERVICE=0` later skips `MTP.begin()` / `MTP.loop()` only; USB stays bare `-DUSB_MTPDISK_SERIAL`.

## OTA and settings I/O

Production leaves `TEG_ENABLE_UNSAFE_LAB_OTA` undefined: `ota.h` stubs, empty `ota.cpp` / `flash_ota.cpp`, no `/api/ota*` routes. Settings poll `/api/status?lite=1` (no `analogRead`). Named presets and waveform GET only when those panels open. Export is `/api/config?download=1`. Pico lives at `/pico.min.css` as a token sheet, not the 83 KB library.
