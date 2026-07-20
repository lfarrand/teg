# True N-cell phase-shifted PWM and the RT1170 migration path

## The limitation on the Teensy 4.1 (i.MX RT1062)

Phase-shifted PWM for N cells wants carriers offset by 360°/N — 90° for four
cascaded H-bridge cells. On FlexPWM the clean way to do that is the
`SM[n].PHASEDLY` register: each submodule delays its counter start by a fixed
tick count from submodule 0's master sync.

**PHASEDLY does not exist on RT1062 silicon.** It was added on the i.MX RT1170
(FlexPWM driver feature flag `FSL_FEATURE_PWM_HAS_PHASE_DELAY`). This is why:

- The back-ported `PWM_SetPhaseDelay()` / `SubModule::setPhaseDelay()` in the
  eFlexPwm fork is compile-gated off (`FSL_FEATURE_PWM_HAS_PHASE_DELAY 0` in
  `fsl_compat.h`). Do not enable it on a Teensy 4.1 — the register offset
  (0x58) reads as reserved on RT1062.
- `ModSchemePhaseShifted` (scheme 5) implements 180° alternation only, which
  is *exact* for 2 interleaved cells and pairs-of-cells for N=4. On FlexPWM a
  180° shift of a centre-aligned carrier is exactly "inverted output polarity
  + complemented duty", needing no counter tricks.

Alternatives investigated and rejected for RT1062:

- **INIT offsets**: the counter reloads to INIT every period, so a one-time
  INIT offset changes the period, not the phase.
- **Staggered RUN bits**: sub-microsecond software timing between MCTRL
  writes; imprecise and unmaintainable.
- **Multiple staggered EXT_SYNC pulses via XBAR**: needs N phase-locked
  trigger sources; the PIT provides one.

## Migration checklist (RT1170 / Teensy 4.1-successor)

1. Set `FSL_FEATURE_PWM_HAS_PHASE_DELAY` to `1` in
   `lib/eFlexPwm/src/nxp/drivers/fsl_compat.h`.
2. In `configureModule2()`, for scheme 5 with N cells set
   `CellSm[k]->setPhaseDelay(ChanA, k * periodTicks / N)` instead of the
   180°-alternation cell plans, and set `CTRL2[INIT_SEL]` to master sync.
3. Remove the 180° `polarityInverted`/`dutyComplement` plan for scheme 5
   (keep it for POD/APOD, which genuinely are 180° dispositions).
4. Verify on silicon: PHASEDLY is loaded on LDOK like the VALx registers.

## Why there is no eDMA zero-CPU modulation (decision record)

An earlier roadmap item proposed feeding VALx registers from eDMA circular
buffers so sine generation costs zero CPU. This was deliberately **not**
implemented once closed-loop regulation, soft-start, and dead-time
compensation landed: all three modify the duty pattern per carrier cycle from
live state (measured feedback, ramp position, current polarity), which a
precomputed DMA buffer cannot express without the CPU regenerating the buffer
— at which point the ISR is doing the same work with less machinery. The
measured ISR cost (`SPWM ISR: n cycles` in the serial report, typically well
under 1 µs of the 50 µs carrier period at 20 kHz) makes the trade a clear win
for the ISR. Revisit only if the carrier frequency needs to rise above
~100 kHz with modulation active.
