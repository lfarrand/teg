PR #68 and #69 squash-merged to main (2026-08-30); #69 was the docs/comments/memories-only operator-copy alignment of #67/#68 contracts (do not commit that class of refresh on `main`). Host Unity is not ISR/OUTEN proof. Do not create `plan/refactor-adversarial-fixes-7.md`.

- thermalConfigure: keep haveValidExternalSample when thermal stays enabled on the same OneWire pin. Pin change or first enable fail-closes, setThermalDerateMilli(0), and if OUTEN is live trip + mask *before* cacheProbeAddresses().
- Lite /api/status emits last-window meterActive (no analogRead). Power/vrms/irms/pf/energy stay full-status only.
- Settings presets/waveform: GET on first panel open; clear loaded flag if fetch fails so the next open retries.
- restoreSecrets: PIN always; MQTT/Influx only if endpoint identity matches. Do not merge with preserveSecrets.
- Operator README, BENCH_CHECKS, SECURITY, CI_SECURITY, PRODUCT_READINESS, web/index.html hints, and teg-pwm-memory now name landed #67/#68 contracts: thermal fail-closed / keep-sample / mask-before-scan; lite last-window meterActive; MTP always-composite + mtpAllowsPwmRelease + no-reboot enable; PLL bench reference lock not grid-tie; MQTT 17 with energy device_class and no aux_alert entity; SchemaVersion 1 + configToJson omissions; six suites plus test_spectrum_wire_quantize_saturates, and test_mqtt_discovery separately. No numeric total. Dated REVIEW_FIXES/plan 91/91 lines stay as snapshots.
- spectrumWireQuantize saturates at 65535 (host-tested).
- setup() flushDisplay() while still inhibited before clearFaultTrip(false); flushDisplay() skips I2C while OUTEN is live.
