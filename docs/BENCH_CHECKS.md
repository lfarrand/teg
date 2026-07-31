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

Everything here is in addition to the original checklist (dead-time per
complementary pair, bipolar leg opposition, POD/APOD idle states, fault-trip
latency, dither spectrum).

---

## 0a. DO NOT USE THE POLARITY-INVERTING SCHEMES ON A REAL POWER STAGE

**Schemes 2 (bipolar), 5 (phase-shifted), and 4 (level-shifted) with POD or APOD
carrier disposition are unsafe on hardware as this firmware stands.** Found by
adversarial review 2026-07-31 and independently reported by five separate lenses.

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
- [ ] **Start an OTA upload** and confirm the same.
- [ ] **Catch the transition edge.** Between un-inverting and the mask landing there is a
      window of a few CPU cycles where the live waveform reaches the pin at the wrong
      level. Expect one short wrong-level edge at the trip instant; measure it. It should
      be well under a microsecond.
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
every settings apply, and the 278 host tests are structurally unable to see registers.

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
- [ ] Boot with **no SD card** and confirm dead time is still present — that path skips
      config validation entirely.

### Still open in this path

The review confirmed two further issues that are **not** fixed:

- A **prescaler change is applied to live outputs** through an `LDMOD=1` + `LDOK` pulse
  with a stale `VAL1`, and nothing masks the pins across the reconfigure.
- `PwmBusClockHz` in `pwm_pair.h` is hard-coded at 150 MHz, while the value that
  actually reaches the register derives from `F_BUS_ACTUAL`. If those differ, the
  dead-time nanosecond conversion is wrong by that ratio.

## 0b. Before anything else

- [ ] **Build reproducibility.** `platformio.ini` pins the platform, framework
      and toolchain (`teensy@5.0.0`, `framework-arduinoteensy@1.159.0`,
      `toolchain-gccarmnoneeabi-teensy@1.110301.0`). The pins are load-bearing,
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
      enough to matter for your output quality, the next lever is lengthening the
      2 s harvest interval, since the cost is per-harvest.

## 1. USB MTP — stack headroom (do this one first if MTP is enabled)

> **Largely resolved 2026-07-30.** Compiling out the opt-in CMSIS FFT engine
> returned **~77 KB of DTCM**, taking free-for-locals from **18,432 to 97,280
> bytes**. The stack is no longer the tightest constraint in this build, and this
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
- [ ] Confirm the outputs really are dead from the moment an upload starts, and
      that only a reboot restores them.

## 3. Hardware current limit (ACMP)

- [ ] **Calibrate the absolute threshold.** Feed a known DC level into the
      sense pin, sweep `ThresholdMillivolts`, and watch `ocActive` in
      `/api/status` flip. The DAC reference is the 3.3 V rail, so accuracy
      tracks that rail.
- [ ] Confirm the comparator output actually reaches the PWM fault input. If it
      never trips, try setting the comparator's `OPE` bit — the RM block diagram
      says the XBAR branch is ungated, but the one known-working community
      example set it.
- [ ] Watch a cycle-by-cycle limit event on the scope (item 4's triggered
      capture is ideal): expect the output to chop at the threshold and
      re-enable at the next cycle boundary.
- [ ] **A single comparator only trips on positive excursions.** With a
      mid-rail-biased sensor, negative-half-cycle overcurrent is invisible. A
      window comparator (a second CMP on FAULT1) is the future extension.
- [ ] `FTST0[FTEST]=1` injects a simulated fault without the comparator —
      the safest way to exercise the latched clear flow.
- [ ] Pin 40 is shared between the meter's ADC channel and the comparator.
      Check for added ADC noise with both enabled.

## 4. Power metering (everything else depends on it)

MPPT maximises *meter-reported* power, and the MQTT/Influx energy figures come
from here, so calibration errors propagate.

- [ ] **Calibrate both zero offsets with the output disabled.** Defaults assume
      mid-rail bias (1650 mV) for both channels.
- [ ] Check `VoltageRatioMilli` and `CurrentMilliampPerVolt` against your actual
      divider and sensor, then verify `vrmsMv`/`irmsMa` against a meter.
- [ ] Confirm the power sign convention matches your wiring (negative = export).
- [ ] V and I are sampled one ADC conversion apart (~1–2 µs) — negligible at
      50 Hz, but relevant if you push the fundamental much higher.

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
- [ ] Confirm the device appears as one "TEG Inverter" with all entities, and
      that the energy sensor is accepted by the HA energy dashboard.
- [ ] Discovery configs are retained, so entities survive an HA restart —
      turn discovery off if you manage entities by hand.

## 9. Event log and RTC

- [ ] Fit a **CR2032 on VBAT** if you want the clock to survive power cycles.
      Without one, entries before the first NTP reply show uptime instead of a
      wall-clock time (never a misleading 1970 date).
- [ ] Times are **UTC**.
- [ ] Confirm the log survives and explains a real reset: trip a fault, reboot,
      and check `/api/log` plus the "device restarted" marker in the viewer.

## 10. Presets, export and import

- [ ] Requires an SD card; the list endpoint reports `available: false` without
      one.
- [ ] Confirm a preset load applies immediately and that **credentials are
      untouched** — presets and exports deliberately omit them, and importing
      can never change the write PIN or a broker password.
- [ ] Note preset files still contain host names, topics and usernames. Treat
      them as configuration, not public data.

## 11. Spectrum and THD

- [ ] Compare the two FFT engines on the same capture: the portable radix-2 is
      the unit-tested reference, CMSIS is the fast path. They should agree.
- [ ] Sanity-check the reported THD against a known-clean and a known-distorted
      output before trusting absolute numbers.

---

## What the tests already cover

Do not re-verify these by hand — they are pinned by the 231 host-side unit
tests and re-run in CI on every push:

- Modulation maths for all nine schemes, including FFT-verified harmonic
  behaviour (carrier cancellation, triplen-free line-line, the 4/π six-step
  series, dither spreading) measured through the *same* code the ISR runs.
- Metering conversions, MPPT convergence on synthetic TEG curves, PLL lock
  behaviour (acquisition, phase jumps, THD immunity, clamping, noise rejection).
- Intel-HEX parsing and OTA image verification, including rejection of
  wrong-board images, truncation and single-bit corruption — validated against
  the real `firmware.hex`.
- Preset name validation (a security boundary: path traversal, every byte
  0x00–0xFF), config round-tripping, secret redaction, event-log sequencing and
  ISO-8601 conversion.
