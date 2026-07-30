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

## 0. STOP — complementary operation and dead time do not work

**Confirmed by source inspection 2026-07-29. This affects the existing firmware,
not just future plans.**

The Teensy core writes `FLEXPWM_SMCTRL2_INDEP` to every FlexPWM submodule
(`cores/teensy4/pwm.c:309`) during startup, before `setup()`. Clearing `INDEP` is
what enables complementary output *and* the dead-time insertion logic
(RM rev.4 p.3110–3111). Nothing in this firmware ever clears it:
`SubModule::configure()` / `setPairOperation()` / `PWM_Init()` have **no callers
in `src/`**.

Consequences, all of which the host test suite is structurally unable to see
because the ISR pipeline is pure maths:

- **Channel B is not the complement of channel A.** The modulation ISR updates
  channel A only. Channel B outputs its *static configured duty*
  (`Sm2x.ChannelB.DutyCycle`, default `32768` = **50%**), centre-aligned on the
  same instant as A.
- **The configured dead time does nothing.** `setupSubmodule()` programs
  `DTCNT0`/`DTCNT1` and the hardware ignores them.
- **Cold-boot dead time is 0, not 50 ns.** `SubmoduleConfig::DeadTime` is
  zero-initialised, `loadConfiguration()` has early-return paths that skip
  `configFromJson`/`validateConfig` entirely (no SD card, fresh card, truncated
  settings), `validateConfig()` has no dead-time floor, and the web UI accepts
  `min="0"`.

**Do not drive any complementary half-bridge or H-bridge from this firmware
until `INDEP` is cleared and dead time is verified on a scope.** Both switches of
a leg would be commanded on simultaneously every carrier cycle.

Before changing it, note that clearing `INDEP` *changes what the pins do*: a
channel B that is currently parked at a static level starts switching inverted.
Verify on a scope with the power stage disconnected.

Two further defects are currently masked by `INDEP=1` and go live the moment it
is fixed — both only affect the polarity-inverting schemes (2, 5, and 4 with
POD/APOD), not scheme 1:

- Polarity inversion flips **both** `POLA` and `POLB`, which turns dead time into
  overlap time.
- `MASK` and the fault state force outputs to logic 0 *before* polarity, so
  inverted cells go **HIGH** on fault, during OTA, and at boot-mask.

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
      for 65–70 µs — longer than a whole period at a 20 kHz carrier. Check it with
      temperature sensors enabled *and* disabled to attribute the cause. The count
      resets whenever the carrier is reconfigured.

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
