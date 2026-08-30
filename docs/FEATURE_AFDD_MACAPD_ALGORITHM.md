# MACAPD algorithm — detailed explanation

**Status:** research / claim-safe. **NO-SHIP.** Host Unity tests of this math are **not** ISR/OUTEN proof and are **not** UL 1699B / IEC AFDD evidence.  
**Code:** `src/afdd_macapd.h` (header-only, host-testable). **Tests:** `test/test_afdd_macapd/`.  
**Architecture:** `docs/FEATURE_AFDD_RESEARCH_2026-08-30.md`.  
**Deep research (literature + edge cases + WARP wavelet precursor):** `docs/FEATURE_AFDD_MACAPD_RESEARCH_2026-08-30.md`.  
Teensy image **must not** drive OUTEN from MACAPD / WARP scores.

---

## 1. Why this algorithm exists

DC PV and battery strings have **no AC line frequency**. Classical AFCI tricks that wait for current zero-crossings or 50/60 Hz envelopes do not transfer. Series and parallel arcs still radiate / conduct as **intermittent broadband high-frequency** disturbance (literature band often discussed ~1–100 kHz).

On TEG, the PWM ISR path (`captureTick`) takes **one sample per carrier**. At 20 kHz that is ~20 kSPS — Nyquist ~10 kHz — so it **cannot** see the arc HF band. Carrier dither **smears** switching energy across that same band. Therefore MACAPD assumes:

1. A **separate** HF acquisition path (on-chip ADC_ETC lab mode **M1**, or preferred external 16-bit **M3**).
2. Knowledge of the **local carrier** so switching edges can be blanked.
3. A **research score** with persistence — never a hair-trigger energy comparator, and never a Teensy trip.

---

## 2. Name and intent

**MACAPD** = **M**asking-**A**ware **C**arrier-blanked **A**rc **P**recursor **D**etector.

| Word | Meaning in this design |
|------|-------------------------|
| Masking-aware | Explicit `maskingPenalty` for series L / C-to-ground that hide arcs (Sandia-class honesty) |
| Carrier-blanked | Zero / ignore samples near FlexPWM switching edges |
| Arc | Broadband, impulsive HF candidate — not a product listing |
| Precursor | Watch **slopes** of energy and kurtosis before sustained energy alone would trip a naive detector |
| Detector | Outputs `Quiet` / `CandidateLow` / `CandidateHigh` / `Inhibited` for logs — **not** OUTEN |

---

## 3. Signal chain (assumed inputs)

```text
HF CT / shunt AFE → anti-alias (~Fs/2) → ADC (M3 preferred) → float samples i[n]
Optional HF voltage channel → v[n]
FlexPWM carrier frequency fc, blank half-width Tb
Config: dither / AFE fault / masking penalty
        │
        ▼
  Build blank mask from fc, Fs, Tb
        │
        ▼
  Apply blank → i_b[n], optional v_b[n]
        │
        ▼
  Features F1–F7 (per frame)
        │
        ▼
  Weighted score − tonal − masking
        │
        ▼
  Persistence state machine → sense enum
```

Default research frame: **Fs = 250 kHz**, **N = 512** (~2.05 ms). The header caps `AFDD_MACAPD_MAX_N` at 512 for stack temps in `afddMacapdProcessRaw`.

---

## 4. Stage A — Carrier blanking

Switching edges inject known impulsive EMI. Because this firmware **owns** FlexPWM, MACAPD builds a mask:

- Carrier period in samples: `P = Fs / fc`
- For sample index `i`, distance to nearest period edge: `min(i mod P, P − (i mod P))`
- If distance ≤ `Tb · Fs`, mark **blank** (invalid); else **keep**

Blanked samples are set to **0** before feature extraction (conservative: remove energy rather than interpolate).

If `blankingAvailable == false`, or `ditherActive`, or `afeFault`, the state machine forces **`Inhibited`** and returns a zero score. Dither must stay mutexed off whenever HF sense is armed (see research note).

---

## 5. Stage B — Feature extraction (per frame)

All features are computed on blanked current `i_b` (and optional `v_b`).

### F1 — Band energies `eL`, `eM`, `eH`

FFT-free approximation: sum of **Goertzel** powers at three center frequencies inside each band:

| Symbol | Band (Hz) | Role |
|--------|-----------|------|
| `eL` | 5k–20k | Lower HF / near carrier region |
| `eM` | 20k–50k | Primary arc-interest mid band |
| `eH` | 50k–100k | Upper HF (parallel / EMI class hints) |

Goertzel gives a cheap single-bin energy estimate without CMSIS FFT (production stay-off).

### F2 — Tonal residual `rTonal`

\[
r_{\mathrm{tonal}} = \frac{\sum_{k=1}^{4} G(k\cdot f_c)}{e_L+e_M+e_H+\varepsilon}
\]

where `G(f)` is Goertzel power at frequency `f`. **High** residual ⇒ energy locked to inverter harmonics ⇒ penalize. **Low** residual with high mid-band ⇒ broadband candidate.

### F3 — Excess kurtosis `kurtosis`

Population excess kurtosis of `i_b`. Gaussian noise ≈ 0; **impulsive intermittent arcs** inflate the fourth moment. Used as the “impulsiveness” feature.

### F4 — Burst duty `dBurst`

Maintain a circular history (`AFDD_MACAPD_BURST_HIST` frames) of binary flags:

- `burst = 1` if `eM > max(2 · EWMA(eM), ε)`
- `dBurst` = mean of history

Arcs **chatter**; steady EMI is stickier. Duty separates intermittent from continuous aggressors.

### F5 — Precursor slopes `slopeEm`, `slopeSk`

\[
\Delta e_M = e_M[t] - e_M[t-1],\quad \Delta \kappa = \kappa[t] - \kappa[t-1]
\]

Rising kurtosis and/or mid-band energy **before** a long HIGH persistence window is the “ahead of time” research hook for DC (no line cycle to wait for).

### F6 — Coherence (optional)

If `v_b` is present, `|corr(i_b, v_b)|`. Common-mode EMI often shows high coherence; series-arc current-dominant patterns may differ. Exposed for datasets; not heavily weighted in the default score.

### F7 — Masking penalty (config)

Operator / later impedance probe supplies `maskingPenalty ∈ [0,1]`. High series inductance or C-to-ground **reduces trust** in a clean detection claim — the score is reduced rather than pretending the arc is always visible.

---

## 6. Stage C — Score

Soft (non-z) feature scaling for host stability:

\[
\begin{aligned}
z_{\mathrm{band}} &= e_M / \mathrm{EWMA}(e_M) \\
z_{\mathrm{sk}} &= \max(\kappa, 0) \\
z_{\mathrm{burst}} &= d_{\mathrm{burst}} \\
z_{\mathrm{slope}} &= \max(\Delta e_M,0)/\mathrm{EWMA}(e_M) + 0.25\max(\Delta\kappa,0)
\end{aligned}
\]

\[
S = w_b z_{\mathrm{band}} + w_k z_{\mathrm{sk}} + w_d z_{\mathrm{burst}} + w_s z_{\mathrm{slope}}
  - w_t r_{\mathrm{tonal}} - w_m \cdot \mathrm{maskingPenalty}
\]

Default weights live in `afddMacapdDefaultConfig()` (`wBand=1`, `wKurtosis=0.75`, `wBurst=0.5`, `wSlope=0.5`, `wTonal=1`, `wMask=1`).

---

## 7. Stage D — Persistence state machine

| Condition | Sense |
|-----------|--------|
| dither / no blanking / AFE fault | `Inhibited` |
| `S > tHi` for `nPersist` consecutive frames | `CandidateHigh` |
| else `S > tLo` | `CandidateLow` |
| else | `Quiet` |

Defaults: `tLo=1.0`, `tHi=2.5`, `nPersist=3`. Persistence exists specifically to reject single-frame EMI spikes (UL-style unwanted-trip mindset), **not** to authorize a product trip on this MCU.

**Binding rule:** firmware that includes this header must **not** call `releaseOutputInhibit` / fault / OUTEN paths from MACAPD sense. Research EventLog / offline analysis only until a dual-MCU interrupter program exists.

---

## 8. API map (`src/afdd_macapd.h`)

| Function | Role |
|----------|------|
| `afddMacapdDefaultConfig` | Research defaults |
| `afddMacapdReset` | Clear EWMA / burst / persist |
| `afddMacapdBuildBlankMask` | Carrier-edge mask |
| `afddMacapdApplyBlank` | Zero invalid samples |
| `afddMacapdGoertzelPower` | Single-bin energy |
| `afddMacapdExcessKurtosis` | Impulsiveness |
| `afddMacapdProcessFrame` | Features + score + state (pre-blanked) |
| `afddMacapdProcessRaw` | Blank + process convenience |

---

## 9. What the host tests prove (and do not)

`pio test -e native --filter test_afdd_macapd` checks:

- Defaults leave sense-friendly flags clear of inhibit (dither off, blanking on).
- Dither and AFE fault force `Inhibited`.
- Blank mask clears samples near carrier edges.
- Pure carrier tone yields **higher** `rTonal` than impulsive bursts.
- Impulsive synthetic bursts have **higher** excess kurtosis than a sine.
- Strong bursts can reach Low/High candidates under loosened thresholds.

They do **not** prove: field arcs, AFE SNR, DMA timing, WCET with Ethernet, or any listing.

---

## 10. Relationship to M3 hardware

MACAPD is **ADC-agnostic** once samples are float frames. Preferred front-end for serious work is **M3** (external 16-bit) — see §5.1.1 in `docs/FEATURE_AFDD_RESEARCH_2026-08-30.md` (ADS8860 / AD7380 recommendation). On-chip 12-bit M1 remains exploratory bring-up only.

---

## 11. Explicit non-claims

- Not an AFDD / AFCI product.  
- Not UL 1699B or IEC 62606 compliant.  
- Not grid-tie protection.  
- Does not trip OUTEN on the Teensy 4.1 image.  
- Host math ≠ bench detection.  
- “Precursor” is a research hypothesis, not a certified early-warning claim.
