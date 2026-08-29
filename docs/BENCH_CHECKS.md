# Bench checks — read before using the newer features

Twelve features were added in a single development run (PRs #18–#29). **None of
them has been verified on hardware.** Every claim in the code comments, the
README and the PR bodies is either derived from the reference manual/datasheets
or covered by host-side unit tests — neither of which can catch a wrong pin, a
mis-scaled divider, or a timing assumption that does not survive contact with a
real power stage.

This file lists what to confirm before trusting each feature, roughly in
descending order of what goes wrong if you skip it. Work down as far as the
features you actually intend to use.

> **Release gate, 2026-08-01: NO-SHIP.** The confirmed software blockers in
> [REVIEW_2026-08-01.md](REVIEW_2026-08-01.md) are remediated on the hardening
> branch. This checklist is the remaining evidence gate: validate the corrected
> build with gate drivers and the power stage disconnected.

Everything here is in addition to the original checklist (dead-time per
complementary pair, bipolar leg opposition, POD/APOD idle states, fault-trip
latency, dither spectrum).

---

## Preflight — before trusting any waveform

- [ ] Hold every PWM pin in a hardware-safe inactive state from reset until the
      loaded configuration is validated, every fault source is armed and sampled,
      PSRAM is proven healthy, and the watchdog is running. The implementation now
      claims this ordering; prove it on every pin during boot and apply.
- [ ] Boot with the SD card absent, with corrupt live/tmp/backup settings, and with
      an injected first-save failure. `/api/status` must report
      `provisioningInhibit:true`, OUTEN must remain disconnected, and explicit fault
      clear must not release a pin. Restore storage, let the verified deferred save
      finish, and confirm only then that the interlock can clear.
- [ ] Boot/apply with the GPIO fault input already active. Outputs must remain dark;
      an edge-only ISR is insufficient.
- [ ] Force a direct TEMPMON reset, allow the board to cool, and prove PWM remains
      inhibited until an authenticated explicit operator acknowledgement.
- [ ] Fit and identify an 8 MiB PSRAM device. Test the full used range, not the
      small boot probe alone, and fail dark on absence/corruption. The image reserves
      7,341,056 EXTMEM bytes; test each ring/store through its normal access path.
- [ ] Reject every conflict among PWM pins, Wire/Wire2, ADC, OneWire, GPIO fault,
      trigger and PowerMon pins before any `pinMode()` or peripheral mux change.
- [ ] Make `SpwmCarrierFrequency` and all four FlexPWM2 submodule frequencies one
      validated value. Reject DDS fundamentals at or above half the actual carrier,
      hardware PWM below the representable ~18 Hz boundary, and asymmetric edges
      outside `INIT..VAL1`.
- [ ] Keep affected outputs inhibited while staging all timing/topology changes;
      load buffered registers together and verify immediate `OCTRL`, `OUTEN` and
      `CTRL2[INDEP]` transitions on a scope. Do not use `SyncPwm` as a safety claim;
      its PIT/external-sync path is incomplete.

## 0a. VERIFY POLARITY-INVERTING SCHEMES BEFORE ANY REAL POWER STAGE

**Schemes 2 (bipolar), 5 (phase-shifted), and 4 (level-shifted) with POD or APOD
had unsafe protective states.** Found by adversarial review 2026-07-31 and
independently reported by five separate lenses; the software fix is unverified.

Those schemes realise a 180° opposition by inverting a cell's output polarity
(`OCTRL[POLA]`/`[POLB]` = 1). Both mechanisms that force outputs off act **before**
polarity is applied:

- `MASK` — RM 55.8.45.4, p.3191: forces the output "to logic 0 prior to consideration
  of the output polarity"
- the fault state `PWMAFS`/`PWMBFS` — RM 55.8.18.3, p.3157: "forced to logic 0 state
  prior to consideration of output polarity control"

So on an inverted cell, "off" is rendered as a **HIGH pin** — the gate is commanded
**on** by the very mechanism meant to shut it down.

**Both halves are now fixed (2026-07-31), and neither is bench-verified.**

- The **fault state** is set per cell — `PWMAFS`/`PWMBFS` = state 1 on inverted cells —
  covering the FlexPWM fault path and the ACMP hardware over-current trip.
- The **mask path** cannot be made polarity-aware directly, because `MASK` has no
  per-cell state. `maskAllOutputsSafely()` therefore clears `OCTRL[POLA]/[POLB]` on the
  inverted cells first, then masks. All four sites use it: the software fault trip, the
  OTA safe state, the ACMP fault ISR, and the refused-clear re-mask in
  `applyPwmConfig()`. `clearFaultTrip()` restores polarity before unmasking.

### Confirm these on a scope, on an inverted cell

Run scheme 2 (bipolar) with two cells, so cell 1 is polarity-inverted, power stage
disconnected.

- [ ] **Trip a software fault.** Both pins of the inverted cell must go **LOW** and stay
      there. If either latches high, stop — that is the gate commanded on.
- [ ] **Trip the hardware current limit** (`FTST0[FTEST]=1` is the safe injector) and
      confirm the same.
- [ ] In an unsafe-lab OTA build, **start an OTA upload** and confirm the same. In a
      normal release, confirm the upload is refused before any output-state change.
- [ ] **Catch the transition edge.** Output drivers should disconnect before polarity
      changes and remain dark through mask/restore. Any wrong-level pulse is a stop.
- [ ] **Clear the fault and confirm the opposition comes back.** The two legs must be
      180° apart again. If they run in phase, `restoreCellPolarity()` did not take.

**Even with this fixed, prefer scheme 1, 3 or level-shifted PD** for anything that
matters. They never invert a cell, so none of the above can apply — the durable fix is
to stop using polarity for carrier inversion entirely.

## 0. Complementary pairs — implemented, never bench-verified. Do this first.

**`INDEP` is now cleared** for any submodule configured as a complementary pair, so
channel B *is* the dead-time-separated complement of channel A and the configured dead
time reaches the hardware. That was not true before 2026-07-30; if you have read an
older copy of this file saying complementary operation "does not work", it is stale.

**Nothing in this path has run on hardware.** It writes FlexPWM registers directly on
every settings apply, and host tests cannot see FlexPWM registers.

### The one that would have damaged hardware

A multi-agent adversarial review on 2026-07-30 found this after the code was merged. A
leg configured as `HalfBridge` but **refused at apply time** — because the modulation
scheme needs polarity inversion, or because `CTRL[COMPMODE]` is set — used to fall back
to independent channels **carrying their static configured duties**. Channel B defaults
to `32768`, and both channels are centre-aligned on the same instant, so the two pulses
are concentric: **both switches of a wired half-bridge commanded on together for up to
half of every carrier period.**

A refused pair now holds both duties at zero and logs an error. Masking would *not*
have been safe — `MASKA`/`MASKB` force the output to logic 0 *before* polarity
(RM 55.8.45.4), so masking a cell that `configureModule2()` set `LowTrue` would drive
both its pins **high** and hold them there.

### Confirm on a scope, power stage disconnected

- [ ] **Channel B is the inverse of channel A**, not a static square wave. Probe pins
      4 and 33 together (and 6/9).
- [ ] **Measure dead time on both edges** against the configured `DeadTime`. This is
      the number the hardware previously ignored entirely.
- [ ] **A refused pair must be dark.** Set a submodule to `HalfBridge`, then select an
      inverting scheme (2 bipolar, 5 phase-shifted, or 4 with POD/APOD). Both pins must
      sit **inactive**, and the event log must show *"Pair mode refused for a
      submodule: outputs held off"*. **If either pin carries a pulse, stop** — that is
      the hardware-damaging case above.
- [ ] **Both pins go low on a fault trip**, not high.
- [ ] **Sm21, Sm40 and Sm41 have no channel-B pin** on a Teensy 4.1 and are forced
      independent. **Sm42 stays independent by design** — asymmetric induction mode
      drives A and B from independent start/stop values.
- [ ] The dead-time floor works: set `DeadTime` to 0, save, and confirm the log reports
      it was raised and the scope agrees.
- [ ] Boot with **no SD card** and confirm the compiled defaults are fail-dark
      (zero duty, modulation/asymmetric mode off) and **no output driver is released**.

### Hardened path to verify

The implementation now disconnects every output driver before immediate polarity,
enable and pair-topology writes, stages the FlexPWM2 submodules, issues one
module-wide LDOK, waits with a carrier-derived deadline, arms and samples protections,
and reconnects only on success. Verify each step electrically: host tests cannot see
OUTEN, OCTRL, MCTRL or the pin mux, and a timeout must leave every pin dark.

## 0b. Before anything else

- [ ] **Build reproducibility.** `platformio.ini` pins the platform, framework
      and toolchain (`teensy@5.2.0`, `framework-arduinoteensy@1.162.0`,
      `toolchain-gccarmnoneeabi-teensy@1.150201.0`). The pins are load-bearing,
      not tidiness — see the comment in that file before bumping anything.
- [ ] **Free RAM.** `pio run -e teensy41` reports
      `RAM1: … free for local variables: N`. **That figure is the stack.**
      Note it before and after any change that adds code.
- [ ] **ISR starvation.** `GET /api/status` reports `missedIsrCycles` — carrier
      cycles the modulation ISR was too late to serve. It should stay at **0**.
      Anything else means something is blocking or preempting the ISR, and the
      modulation, capture, metering and current-limit polling all live there.
      A known candidate is the OneWire temperature harvest, which masks interrupts
      for 65–70 µs — longer than a whole period at a 20 kHz carrier. The count
      resets whenever the carrier is reconfigured.
- [ ] **Attribute the OneWire share.** `thermalMissedCycles` reports what the last
      harvest alone cost. Probe addresses are cached rather than re-searched, so
      this should now be a small fraction of what it was — but "should" is the
      word doing the work, and this is the number that settles it. If it is large
      enough to matter for your output quality, the next lever is the harvest
      request: 4 s when the carrier is ≥ 10 kHz while PWM is inhibited; the
      800 ms wait is kept. That is not a generic 2 s harvest interval.

## 1. USB MTP — stack headroom (do this one first if MTP is enabled)

> **Largely resolved 2026-07-30.** Compiling out the opt-in CMSIS FFT engine
> returned **~77 KB of DTCM**. The current hardened O2/LTO release reports
> **90,752 bytes** free for locals; the exact figure changes with code generation.
> The stack is no longer the tightest constraint in this build, and this
> section drops well down the priority order. It is kept because the checks are
> still worth running once, and because defining `TEG_ENABLE_CMSIS_FFT` puts the
> whole 77 KB back.

Enabling USB MTP dropped RAM1 free-for-locals from **~55 KB to ~18.4 KB**,
because the library's code lands in ITCM. That number *is* the available
stack, and it was the single most likely way this build bites you.

- [ ] Confirm the figure on your build: `pio run -e teensy41`, read
      `free for local variables`.
- [ ] **Cross-check it against the device.** `GET /api/status` now reports
      `dtcmFree` (headroom now) and `stackLowWater` (the smallest seen since boot).
      At idle `dtcmFree` should sit close to the build figure; if it does not, the
      measurement is wrong and nothing below it can be trusted. Watch
      `stackLowWater` rather than `dtcmFree` while exercising the deep paths — an
      overflow happens at peak call depth, not when a 1 Hz task happens to sample.
- [ ] Exercise the deepest call paths **with MTP mounted and browsing**, and
      watch for a crash/reboot: load the stats page (FFT + spectrum), download
      a raw capture, upload a large waveform, and save settings — ideally at
      the same time as a host directory listing.
- [ ] Check the crash report after any unexplained reset: `GET /api/crash`.
      A stack overflow typically shows as a hard fault with a nonsensical PC.
- [ ] If headroom is uncomfortable, the mitigation is `FLASHMEM` attributes on
      the vendored library's cold functions (moves them out of ITCM), or simply
      not compiling MTP in.

Also for MTP:

- [ ] Enter a deliberate maintenance state first: inhibit every PWM output and stop
      control ownership before browsing. Confirm the automatic gate never starts or
      services MTP during SPWM, fixed duty, resident playback or other-module output.
- [ ] **It is read-only by design.** Confirm the host cannot delete, rename or
      write — that refusal is a safety property, not a bug (see
      `lib/MTP_Teensy/PATCHES.md`).
- [ ] The USB PID changes to `0x04D5`, so the COM port may move once. Serial
      still works (`USB_MTPDISK_SERIAL`), but **eject the device before
      flashing** — an open MTP session can block Teensy Loader's auto-reboot.
- [ ] File timestamps are wrong on the host for even-numbered years after
      February (upstream leap-year bug) — cosmetic, but do not trust them.
- [ ] Copy a known-good file and verify its checksum: the library discards a
      USB send-timeout return, so a stalled host can silently truncate.

## 2. OTA firmware updates — recovery plan

**Test on a bench board with USB access before ever using this remotely.**

- [ ] Confirm a normal release build's OTA upload/status routes are unregistered
      and return HTTP 404. The destructive checks below apply only to a
      deliberately compiled `TEG_ENABLE_UNSAFE_LAB_OTA` sacrificial build.

- [ ] From the first erase of the commit until the reset (**~10–60 s**), a power
      loss leaves the board unbootable and needing physical recovery
      (pushbutton + Teensy Loader over USB). There is no A/B partition on the
      RT1062. Verify-before-commit narrows the window to the copy itself; it
      cannot remove it.
- [ ] **Time the commit copy** on your card/board and compare against the 8 s
      watchdog. The copy loops feed the watchdog explicitly, but the duration
      itself is derived from datasheet timings, not measured.
- [ ] Confirm a deliberately-corrupt upload is rejected (flip a byte in a `.hex`
      and check the error), and that a Teensy 4.0 build is refused.
- [ ] **Know what a failed commit actually does.** After three failed read-backs the
      base image may already be damaged and the commit path returns with ENET, USB and
      other interrupts disabled. It is not a credible remotely retryable state.
      Keep outputs off, do not power-cycle, and use the physical Program button plus
      Teensy Loader. Verify this recovery procedure on a sacrificial board.
- [ ] Prove signature and rollback policy before calling OTA deployable. The current
      CRC/structure checks accept any compatible unsigned image and any older build
      from a holder of the cleartext PIN.
- [ ] Confirm the outputs really are dead from the moment an upload starts, and
      that only a reboot restores them.

## 3. Hardware current limit (ACMP)

- [ ] **Calibrate the absolute threshold.** Feed a known DC level into the
      sense pin, sweep `ThresholdMillivolts`, and watch `ocActive` in
      `/api/status` flip. The DAC reference is the 3.3 V rail, so accuracy
      tracks that rail.
- [ ] Submit non-zero filter count/period values and confirm validation restores both
      to zero. Measure pin-to-FlexPWM-off latency in the enforced continuous,
      high-speed comparator mode; do not quote the comparator's ~25 ns alone.
- [ ] Confirm the comparator output reaches **both** private FAULT0 selectors:
      PWM1 SM3 on pins 8/7 and PWM2 SM0-3. Trigger one threshold crossing and
      scope every connected output; none may remain switching. If neither route
      trips, investigate the comparator `OPE` bit — the RM block diagram says the
      XBAR branch is ungated, but the one known-working community example set it.
- [ ] With the source quiet, clear a latched trip repeatedly at an asynchronous
      phase. Verify both FFLAG0 latches clear, both groups stay off if the source
      reasserts during the clear, and PWM2's sole fault IRQ reports the re-trip.
- [ ] Watch a cycle-by-cycle limit event on the scope (item 4's triggered
      capture is ideal): expect both PWM groups to chop at the threshold and
      each to re-enable at its own next cycle boundary. Deliberately use unequal
      PWM1/PWM2 rates and treat the reported count as sampled telemetry only.
- [ ] Confirm PWM1 SM3's OCTRL polarity/fault-state combination drives pins 8/7
      low during FAULT0. Then repeat through reconfiguration; a readback mismatch
      must leave OUTEN disconnected. Hardware input pull-downs are still required
      because the latched ISR disconnects the pins after the combinational trip.
- [ ] **A single comparator only trips on positive excursions.** With a
      mid-rail-biased sensor, negative-half-cycle overcurrent is invisible. A
      window comparator (a second CMP on FAULT1) is the future extension.
- [ ] Exercise `FTST0[FTEST]=1` on **each module** without the comparator.
      PWM2 validates the sole IRQ/software-notification path; PWM1 validates
      that its independent latch blocks release even without a PWM1 fault IRQ.
      Clear the test bit before attempting the latched clear flow.
- [ ] Read back `FFILT0` on both modules: expect only `GSTR=1`, with `FILT_PER=0`
      and `FILT_CNT=0`. Inject the shortest practical comparator pulse and prove
      both modules set `FFLAG0`, not merely a momentary output chop. A non-zero
      shared filter configuration must refuse to arm and leave all outputs dark.
- [ ] Confirm no other feature enables PWM2 `FIE1`–`FIE3`: the PWM2 fault vector
      is exclusively owned by this path. After a clean latched clear, verify the
      stale NVIC pending bit is gone and does not cause a false re-trip.
- [ ] Pin 40 is shared between the meter's ADC channel and the comparator.
      Check for added ADC noise with both enabled.

## 4. Power metering (everything else depends on it)

MPPT maximises *meter-reported* power, and the MQTT/Influx energy figures come
from here, so calibration errors propagate.

- [ ] **Calibrate both zero offsets with the output disabled.** Defaults assume
      mid-rail bias (1650 mV) for both channels.
- [ ] Check `VoltageRatioMilli` and `CurrentMilliampPerVolt` against your actual
      divider and sensor, then verify `vrmsMv`/`irmsMa` against a meter.
- [ ] Reject calibration extremes that can drive computed power outside `int32_t`;
      confirm finite/saturating behavior before MPPT is enabled.
- [ ] Confirm the power sign convention matches your wiring (negative = export).
- [ ] V and I are sampled one ADC conversion apart (~1–2 µs) — negligible at
      50 Hz, but relevant if you push the fundamental much higher.
- [ ] Disable/freeze capture for a known interval and reconnect at a different load.
      Energy must expose the gap counter and resume from a fresh timestamp without
      backfilling the interval with the first recovered instantaneous power.

### 4a. Driver-board power monitor

- [ ] Verify the isolated bus is **Wire2** on pins 24 (SCL2) / 25 (SDA2), with
      pull-ups present at this end, and prove none of its configurable GPIO/ADC pins
      conflicts with a PWM or protection pin.
- [ ] Read back INA226 register 0x00: expect `0x4527` (16 averages and about 35.2 ms
      per refreshed shunt+bus result at 1.1 ms conversion times).
- [ ] Configure a threshold beyond the INA226 positive shunt range and verify the SOL
      register saturates at signed `0x7FFF`, never wraps to `0x8000` or above.
- [ ] Confirm the physical ALERT pin toggles and that its ISR counter/event is retained
      even if Wire2 is wedged. Then recover I²C and verify the reset/gap counters.
- [ ] Hot-unplug the INA226, wait, reconnect under a changed load, and verify both
      status recovery and energy-gap handling.
- [ ] Compare TPS25983 IMON with INA226 only inside the eFuse datasheet's specified
      IMON current range. Below it, treat IMON as qualitative.
- [ ] `PowerMon.IntervalMs` default and validate floor are 250.

## 5. Grid / reference PLL

- [ ] **Calibrate `PhaseOffsetCentiDeg` against a scope.** The sensing chain
      (divider RC, ADC sample-and-hold, the one-carrier-cycle conversion
      latency) contributes a fixed phase shift that this field exists to absorb.
- [ ] Expect a small static detune error (~1.4° per Hz of reference offset from
      nominal) from the fixed-centre SOGI. It is a calibration quantity, not a
      fault.
- [ ] **Wiring hazard the firmware cannot detect:** if the capture pin senses
      the inverter's *own* output rather than an external reference, the loop
      closes on itself and locks to nothing meaningful.
- [ ] Note that arming the triggered scope freezes the shared capture ring and
      therefore coasts the PLL — surfaced as `pllState`, but surprising.
- [ ] Reject low carrier/sample rates that make `Ts/20 ms >= 2` or dwell counters
      zero. The accepted 1 Hz nominal / 18 Hz carrier case is unstable in the current
      build; test the exact supported minimum after fixing validation.
- [ ] Stall foreground processing long enough to build 4,097+ samples of backlog at
      200 kHz, then verify phase history. The current multi-pass drain reconstructs
      old samples with a newer DDS increment.

## 6. MPPT

- [ ] Start with the defaults (3 s interval, 20-milli step) and watch
      `mpptDeltaMw` in the status line. If it sits inside the deadband at steady
      state, your deadband is above the real measurement noise and can come down.
- [ ] Confirm the interval covers your soft-start ramp — validation floors it at
      ramp + 2.1 s, but verify the tracker is not evaluating mid-ramp.
- [ ] Watch behaviour on a plateau (e.g. output saturated by the current limit):
      the tracker should wander a few steps and turn around, not sweep.

## 7. Triggered scope and raw capture

- [ ] `levelMv` is millivolts **at the pin**, not at the sensed output — check
      it against your divider.
- [ ] Freeze before downloading (trigger or fault): a rolling ring can have its
      oldest samples overwritten mid-transfer.

## 8. MQTT / Home Assistant

- [ ] **No TLS.** Use a LAN broker or a plaintext listener; port 8883 will
      connect at TCP level and then fail (bounded, but pointless).
- [ ] Confirm MQTT discovery entities appear on a bench broker. This is a bench
      integration, not an energy-dashboard product claim.
- [ ] Discovery configs are retained, so entities survive an HA restart —
      turn discovery off if you manage entities by hand.

## 9. Event log and RTC

- [ ] Fit a **CR2032 on VBAT** if you want the clock to survive power cycles.
      Without one, entries before the first NTP reply show uptime instead of a
      wall-clock time (never a misleading 1970 date).
- [ ] Times are **UTC**.
- [ ] Before treating time as evidence, inject replies from the wrong IP/port and with
      mismatched originate timestamp, invalid mode/leap/stratum, and zero transmit
      seconds. All must be rejected by the implemented RFC 5905 association/sanity
      checks. NTP remains unauthenticated, so this is robustness rather than proof.
- [ ] Confirm the log survives and explains a real reset: trip a fault, reboot,
      and check `/api/log` plus the "device restarted" marker in the viewer.

## 10. Presets, export and import

- [ ] Requires an SD card; the list endpoint reports `available: false` without
      one.
- [ ] Confirm a preset load applies immediately. The write PIN is always
      restored. MQTT password and Influx token are restored only if endpoint
      identity matches (MQTT: Host/Port/Username; Influx: Host/Port/Org/Bucket);
      otherwise they are cleared and disabled (`Mqtt.Enabled = false`,
      `Influx.IntervalSeconds = 0`). Import can drop a broker password when
      host/port/user change.
- [ ] Note preset files still contain host names, topics and usernames. Treat
      them as configuration, not public data.

## 11. Spectrum and THD

- [ ] Portable radix-2 is the default. Check `GET /api/spectrum?format=bin`
      (TEGS; HTTP 200 + flags=0 if unavailable). The JSON default has no
      `"engine"` field. Do not compare two FFT engines.
- [ ] Sanity-check the reported THD against a known-clean and a known-distorted
      output before trusting absolute numbers.
- [ ] Use a deliberately non-coherent fundamental and inject a known high-order
      harmonic. The current rounded-fundamental ±1-bin search can miss it almost
      completely (for example 50 Hz / 20 kSPS / 4096 points with H17).

---

## What the tests already cover

Host suites exercise headers; they do not prove ISR or OUTEN. The last
verified host-safe count is the six-suite **91/91** gate
(`test_config_serde`, `test_features`, `test_ota`, `test_spectrum`,
`test_thermal_math`, `test_waveform`).

- Modulation maths for all nine schemes, including FFT-verified harmonic
  behaviour (carrier cancellation, triplen-free line-line, the 4/π six-step
  series, dither spreading). That is header-level, not ISR/OUTEN proof.
- Metering conversions, MPPT convergence on synthetic TEG curves, PLL lock
  behaviour (acquisition, phase jumps, THD immunity, clamping, noise rejection).
- Intel-HEX parsing and OTA image verification, including rejection of
  wrong-board images, truncation and single-bit corruption — validated against
  the real `firmware.hex`.
- Preset name validation (a security boundary: path traversal, every byte
  0x00–0xFF), config round-tripping, secret redaction, event-log sequencing and
  ISO-8601 conversion.
