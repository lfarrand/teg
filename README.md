# TEG Power Generator — PWM Inverter Controller

Firmware for a **Teensy 4.1** (NXP i.MX RT1062, Cortex-M7 @ 600 MHz) that acts as a
network-controlled PWM signal generator and inverter modulator for a thermoelectric
generator (TEG) power system. It drives complementary IGBT/MOSFET gate pairs across
all four FlexPWM timer modules, with a modern web UI, a JSON API, closed-loop
amplitude regulation, and a fast fault trip.

## Highlights

- **9 modulation schemes** — from plain fixed-duty PWM through SPWM, THIPWM, SVPWM,
  discontinuous PWM (DPWM), level-shifted multilevel carriers, and four-leg 3D-SVPWM
- **DDS-exact output frequency** — a 32-bit phase accumulator gives ~4.7 µHz tuning
  resolution at a 20 kHz carrier, with zero drift and seamless wrap
- **Sub-period settings apply** — changes reach the PWM hardware within one carrier
  period of saving; the measured apply latency is reported per change
- **Closed-loop regulation** — PI controller steers the modulation index toward a
  voltage setpoint read from an analog feedback pin
- **Protection** — latched ~1 µs software fault trip that masks every PWM output
- **Modern web UI** — single-page app with automatic dark mode, live telemetry,
  and scheme-aware forms, served gzip-compressed from flash
- **Tested** — the hardware-independent logic (modulation math, config mapping,
  timing calculations) runs as native unit tests in CI, gated at 80% line coverage

## Hardware

| Peripheral | Details |
|---|---|
| Board | Teensy 4.1 (600 MHz Cortex-M7, 512 KB TCM, 512 KB OCRAM, 8 MB PSRAM) |
| PWM | All four FlexPWM modules, 9 submodules (see pin map below) |
| Network | On-board Ethernet (QNEthernet, DHCP), NTP time sync |
| Storage | SD card (settings persistence), QSPI flash (LittleFS available) |
| Display | 128×64 SSD1306 OLED over I²C — rolling 5-line log + status line |
| Scope trigger | Pin 13 (LED) toggles every modulation ISR cycle |

### PWM pin map

| Submodule | Pins (A, B) | Role |
|---|---|---|
| PWM1 SM3 (`Sm13`) | 8, 7 | General complementary pair (timer page control) |
| PWM2 SM0 (`Sm20`) | 4, 33 | **Modulation cell 1** (inverter leg 1 / phase A) |
| PWM2 SM2 (`Sm22`) | 6, 9 | **Modulation cell 2** (inverter leg 2 / phase B) |
| PWM2 SM1 (`Sm21`) | 5 | **Modulation cell 3** (phase C for 3-phase schemes) |
| PWM2 SM3 (`Sm23`) | 36, 37 | **Modulation cell 4** (neutral leg for 3D-SVPWM) |
| PWM3 SM1 (`Sm31`) | 29, 28 | General complementary pair |
| PWM4 SM0 (`Sm40`) | 22 | General single output |
| PWM4 SM1 (`Sm41`) | 23 | General single output |
| PWM4 SM2 (`Sm42`) | 2, 3 | Asymmetric induction mode (custom edge timing) |

Every complementary pair supports hardware dead-time insertion, configured in
**nanoseconds** per submodule.

## Building and flashing

PlatformIO project (also opens in CLion/VS Code):

```
pio run -e teensy41          # build (also regenerates the embedded web UI)
pio test -e native           # run the unit tests on the host
```

Flash `.pio/build/teensy41/firmware.hex` with the Teensy loader. CI builds the
firmware and runs the tests on every PR; the hex is attached as a build artifact.

Settings persist to `/settings.cfg` (JSON) on the SD card; missing keys fall back
to safe defaults, so the file can be edited or deleted freely.

## Web UI and API

Browse to the device's IP (shown on the OLED at boot, DHCP-assigned). The UI is a
single-page app with:

- **Automatic dark mode** (follows the OS) plus a manual Auto/Light/Dark toggle
- A **live status bar** (1 Hz): uptime, actual modulation frequency, live/target
  modulation index, ISR duration, last apply latency, free RAM
- A red **fault banner** within a second of a protection trip
- **Scheme-aware forms** — only the options relevant to the selected modulation
  scheme are shown
- Saves apply immediately (no page reload) and the toast reports the measured
  hardware-apply time

The API underneath is plain JSON:

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/config` | GET | Full configuration document (secrets redacted) |
| `/api/config` | POST | Replace configuration; applies to hardware first, then persists to SD; returns `{"applyMicros": n}` |
| `/api/status` | GET | Live telemetry (uptime, fault, ISR cycles, apply time, actual frequency, index, free RAM) |
| `/api/capture` | GET | Min/max envelope of the waveform capture ring (`?count=&bins=`) |
| `/api/waveform` | GET | Current uploaded custom waveform (type, size, preview) |
| `/api/waveform` | POST | Upload a `teg-wave v1` file (body = raw text); applies live if active |

When a **write PIN** is configured (Security section), POSTs require a matching
`X-Auth-Pin` header — the UI prompts automatically. Secrets (the InfluxDB token
and the PIN itself) are redacted from GET responses; an empty secret in a POST
keeps the stored value.

The device also announces itself via mDNS as **`http://teg.local`**.

## Modes of operation (modulation schemes)

The inverter timer (FlexPWM2) runs a modulation engine driven by a per-carrier-cycle
interrupt. A signed unit-amplitude reference waveform lives in a 2048-entry lookup
table; a fixed-point DDS phase accumulator walks it at the configured modulation
frequency, and the amplitude (modulation index) is applied at runtime — so
closed-loop control and soft-start never pause the output to rebuild tables.

Cells (inverter legs) are driven in the order **2.0, 2.2, 2.1, 2.3** so that
two-leg schemes use the original H-bridge pair.

| # | Scheme | Legs | Description |
|---|---|---|---|
| 0 | **Fixed duty** | — | Plain PWM at the configured per-submodule duty cycles; the modulation ISR is disabled. |
| 1 | **Unipolar SPWM** | 2 | The classic H-bridge baseline: two legs driven with complementary sinusoidal references against the triangular (centre-aligned) carrier. Three-level differential output, doubled effective switching frequency in the output spectrum. |
| 2 | **Bipolar SPWM** | 2 | One reference; leg 2 switches in exact opposition (inverted output polarity). Two-level output that swings the full rail every transition. |
| 3 | **THIPWM** | 2 | Unipolar SPWM with a ⅙-amplitude third harmonic added to the reference. The flattened crest permits modulation indices up to **1.155**, i.e. ~15.5 % better DC-bus utilisation. |
| 4 | **Level-shifted (LSPWM)** | 1–4 | Stacked carrier bands for NPC/diode-clamped multilevel stacks. Each cell PWMs only while the reference is inside its band. Sub-modes via *Carrier Disposition*: **PD** (all carriers in phase), **POD** (carriers below zero in antiphase), **APOD** (alternate carriers in antiphase). Optional **Nearest Level** staircase mode. |
| 5 | **Phase-shifted (PSPWM)** | 1–4 | Every cell carries the full reference with carriers alternating 180° — exact PS-PWM for 2 interleaved cells. (True 90° shifts for 4 cells need the RT1170's PHASEDLY register — see `docs/RT1170_PSPWM.md`.) |
| 6 | **SVPWM** | 3 | Three-phase space-vector modulation implemented as min-max zero-sequence injection (mathematically identical to vector-computed SVM with centred zero vectors). Linear to index 1.1547. |
| 7 | **DPWM** | 3 | Three-phase **discontinuous** PWM: the zero sequence pins one phase to a rail so it stops switching, cutting switching losses ~33 %. Variants below. |
| 8 | **3D-SVPWM** | 4 | Three-phase **four-leg** for four-wire / unbalanced loads: phase legs carry `v + zss`, the fourth leg carries the zero sequence itself, so every phase-to-neutral voltage equals its pure reference and zero-sequence current returns through the neutral leg. |

### DPWM variants (scheme 7)

| Variant | Clamping |
|---|---|
| **DPWMMIN** | Lowest phase clamped to the negative rail (120° per cycle) |
| **DPWMMAX** | Highest phase clamped to the positive rail (120° per cycle) |
| **GDPWM** | Clamps the largest-magnitude phase, with an adjustable **clamp angle**: 0° = DPWM1, −30° = DPWM0, +30° = DPWM2. Set it to the load's power-factor angle for maximum loss reduction. |
| **DPWM3** | Clamps the intermediate-magnitude phase (30° segments) |

### Modulation options

| Option | Meaning |
|---|---|
| **Use SPWM** | Master enable for the modulation engine (scheme 0 also disables it). |
| **Carrier frequency** | PWM switching frequency of the inverter timer (e.g. 20 000 Hz). |
| **Modulation frequency** | Fundamental output frequency (e.g. 50 Hz). DDS-exact for any ratio — no quantisation to integer carrier/fundamental ratios. |
| **Modulation index** (/1000) | Output amplitude. 1000 = 1.0 (full linear SPWM range); THIPWM/SVPWM stay linear to 1155. Values beyond the linear range clamp to the rails (overmodulation). Ignored while closed-loop regulation is active. |
| **Cells** | Number of legs driven (1–4) for level-shifted and phase-shifted schemes. Three-phase schemes force 3; 3D-SVPWM forces 4. |
| **Carrier disposition** | LSPWM only: PD / POD / APOD (see above). |
| **Reference waveform** | Sine (default), **trapezoid** (60° ramps), or **square**. Square + SVPWM produces six-step operation; square + bipolar is the classic square-wave H-bridge. THIPWM always uses sine + third harmonic. |
| **Nearest Level** | LSPWM only: cells snap to the nearest level instead of PWM-ing inside their band — staircase output with fundamental-frequency switching (near-zero switching loss, coarse at low level counts). |
| **Dead-time compensation** | Adds a polarity-signed duty correction of 2·t_d·f_sw to cancel the crossover distortion dead-time causes at low modulation. Applied to full-reference legs and SVPWM phases. |
| **Soft start** (ms) | Ramps the modulation index from its current value to the target over this time; 0 = instant. Also slew-limits closed-loop corrections. |
| **Carrier dither** | Spread-spectrum switching (EMI/acoustic noise reduction): the carrier period is re-selected every cycle from a table spanning ±*percent* (max 30 %), either **randomly** (LFSR) or as a **triangular sweep**. Each period entry carries a matched DDS increment, so the fundamental frequency stays exact regardless of the dither. |

## Custom waveforms (arbitrary references and pulse sequences)

Set *Reference Waveform* to **Custom** or **Sequence** and upload a
`teg-wave v1` file from the web UI (persisted to `/waveform.bin` on SD and
reloaded at boot). References up to **2,097,152 samples** live in PSRAM (the
fast path: bulk-loaded in about a second at boot); **larger references — tens
of MB — stream from the SD card at playback time** through a double buffer
(2 × 16384 samples, ~0.8 s of headroom each against SD latency spikes; an
underrun holds the last sample and is counted in the UI). The file's sample
count selects the path automatically. PSRAM-resident waveforms offer two
playback modes:

- **Period mode** (default): the whole file is one fundamental period at the
  modulation frequency, rendered through the 2048-point DDS table (very long
  files are downsampled for this mode).
- **Sample-step mode**: exactly one stored sample per carrier cycle at full
  resolution, repeated ad infinitum — the repeat period is
  `points ÷ carrier` (2M points at 20 kHz ≈ 105 s), limited only by the
  PSRAM allocation.

Text format: `#` starts a comment; the first content line declares the type:

```
# teg-wave v1 — arbitrary reference (AWG-style)
type=reference
0.0        # one normalized sample (-1..1) per line, 2..4096 points;
0.38       # resampled to the internal 2048-point table and played by the
0.92       # DDS at the modulation frequency, scaled by the index, through
0.38       # whichever scheme is selected — exactly like the built-in sine
0.0
-0.38
-0.92
-0.38
```

```
# teg-wave v1 — on/off pulse train (function-generator burst style)
type=sequence
1.0, 1500   # level (-1..1), duration in microseconds; up to 64 segments
0.0, 500    # played in order and looped; the effective period is the sum
-1.0, 300   # of the durations (modulation frequency is ignored)
```

For bulk uploads there is also a **binary format** (auto-detected, ~4× smaller
and faster than text): a 12-byte header — magic `TEGW`, `u8` version (1),
`u8` type (1 = reference, 2 = sequence), `u16` reserved, `u32` count
(little-endian) — followed by `count` little-endian `int16` Q15 samples.
From Python: `b"TEGW" + bytes([1,1,0,0]) + struct.pack("<I", n) +
samples.astype("<i2").tobytes()`.

Uploads may additionally be **gzip-compressed** (text or binary — detected by
the gzip magic bytes and decompressed on the fly during ingest, CRC-verified):
`gzip.compress(...)` or simply `gzip wave.txt`. The SD copy is always stored
uncompressed, which is what keeps boot loading a single bulk read and the
streaming refill loop a plain seek-and-read.

Notes: sequence edges quantize to the carrier period (50 µs at 20 kHz — raise
the carrier for finer steps). Levels pass through the normal index scaling and
per-cell mapping, so ±1/0 patterns drive complementary pairs with dead-time
intact. Reference waveforms may carry DC deliberately; sequences work with
schemes 0–5 (the three-phase schemes derive their legs from the DDS phase).

## Closed-loop regulation

When enabled, a PI controller (with anti-windup) runs at `LoopHz` and steers the
modulation index target so the **feedback pin voltage tracks the setpoint**. The
feedback input expects a **DC voltage proportional to the regulated quantity** —
e.g. a rectified-and-filtered output sample or the DC bus through a divider.

| Option | Meaning |
|---|---|
| **Setpoint** (mV) | Target voltage at the feedback pin |
| **Full scale** (mV) | Feedback voltage corresponding to full ADC scale (set by your divider) |
| **Kp** (/1000) | Proportional gain — index units per volt of error |
| **Ki** (/1000) | Integral gain — index units per volt-second |
| **Analog pin** | Feedback input (default A17 / pin 41) |
| **Loop Hz** | Control loop rate (default 1000) |

The live status bar shows the index actually applied and the PI's current target,
so convergence is visible in real time.

## Fault protection

A configurable pin is armed as a **fast software trip**: any active transition
fires a highest-priority GPIO interrupt (it preempts even the modulation ISR) that
masks every FlexPWM output — roughly **1 µs pin-to-off** — and latches. The web UI
shows a red banner; outputs stay off until settings are re-applied (Save). Wire an
overcurrent comparator or thermal switch here. The default pin (32) is
XBAR-capable, so a future zero-software hardware fault path (FlexPWM FAULT0) can
reuse the same wiring.

## Asymmetric induction mode (Timer 4 / SM42)

A special mode that programs SM42's edge registers directly for asymmetric A/B
pulse timing: channel B's turn-on is advanced by **Pre-shift** (ns) relative to
channel A's turn-off, and its turn-off pulled back from the period end by
**Post-shift** (ns). Tick values use the prescaler-corrected clock, so low
frequencies fit the 16-bit counters correctly.

## Waveform capture (built-in scope / flight recorder)

When enabled, the modulation ISR samples the feedback pin **once per carrier
cycle at the reload point** (the average-current instant for centre-aligned
PWM, 12-bit) into a 2 MB PSRAM ring — about **52 s of continuous history at a
20 kHz carrier**. The web UI renders a min/max envelope chart (1 s / 5 s / 30 s
windows). On a **fault trip the ring freezes**, preserving the pre-fault
waveform as a flight record until settings are re-applied. While capture runs,
the closed-loop controller uses the synchronous samples instead of `analogRead`.

## Thermal monitoring and derating

DS18B20 probes on a configurable OneWire pin (index 0 = TEG **hot side**,
1 = **cold side**) plus the RT1062 die temperature. The worst of (hot side,
die) linearly derates the modulation index between *Derate start* and *Derate
end* — the derate factor acts as a ceiling on both open-loop settings and the
closed-loop PI output, and the soft-start slew limit shapes recovery. Live
temperatures and the active derate factor appear in the status bar.

## Reliability

- **Degraded-mode boot**: a missing SD card, OLED, or DHCP lease no longer
  halts the firmware — it boots, runs PWM, logs the degradation, and keeps
  retrying the network in the background.
- **Hardware watchdog**: WDOG1 resets the device if the main loop stalls for
  ~8 s, returning to the known-safe boot path instead of leaving PWM
  free-running with stale state.

## Metrics (InfluxDB)

The firmware can post line-protocol metrics to an **InfluxDB v2** instance. The
target is configured in the *InfluxDB Metrics* section of the web UI (or the
`Influx` block of `/settings.cfg`):

| Option | Meaning |
|---|---|
| **Host / Port** | InfluxDB server (default `ub-1.lan:8086`) |
| **Org / Bucket** | InfluxDB v2 organisation ID and target bucket |
| **Token** | API token with write access to the bucket |

Metrics are **disabled until a token is set** — the token is stored only in the
device's config file on the SD card, never in firmware source or this repository.

## Timings

- **Settings apply**: from clicking Save, changes are parsed, applied to the
  (double-buffered) PWM registers, and the hardware loads them at the next PWM
  reload — i.e. **within one carrier period** (50 µs at 20 kHz). The measured
  figure is logged (`Settings applied in Nus`) and shown in the UI toast.
  Untouched timers are never disturbed and keep their phase.
- **Modulation ISR**: integer-only (no FPU exception-entry cost), runs at NVIC
  priority 32 (above USB/Ethernet), self-measured via the DWT cycle counter and
  reported in the status telemetry.
- **SD persistence** happens from the main loop *after* the HTTP response, so
  saving never blocks on the card.

## Architecture notes

- `src/modulation.h`, `spwm_math.h`, `pwm_timing.h`, `config_serde.h`,
  `config_fields.h`, `ntp_utils.h`, `pi_controller.h` are **pure headers** with no
  hardware dependencies — they compile on the host and carry the unit tests
  (`test/`, run with `pio test -e native`, coverage-gated in CI).
- The web UI lives in `web/` and is gzipped into flash at build time by
  `scripts/gzip_web_assets.py` (Pico.css v2 is vendored; no build toolchain).
- `lib/aWOT` and `lib/eFlexPwm` are forks (submodules): aWOT is bound to QNEthernet
  with blocking `writeFully` sends; eFlexPwm adds 16-bit duty resolution and keeps
  its debug logging compiled out (`EFLEXPWM_ENABLE_LOGGING`).
- Hot state (sine LUT, ISR variables) lives in zero-wait-state DTCM; cold code
  (web handlers, config I/O) is marked `FLASHMEM` to keep ITCM for the fast paths.
- True N-cell phase-shifted PWM needs RT1170 silicon — the analysis and migration
  checklist are in `docs/RT1170_PSPWM.md`.

## Bench verification checklist

Items that should be confirmed on a scope before driving a real power stage:

1. Dead-time on each complementary pair matches the configured nanoseconds
2. Bipolar (scheme 2) leg opposition and POD/APOD inverted-cell idle states
3. Fault trip latency and latching with a signal source on the fault pin
4. Carrier-dither spectrum (carrier peak spreads; fundamental unmoved)
5. The `Settings applied in Nus` and `SPWM ISR: n cycles` figures against the
   documented expectations
