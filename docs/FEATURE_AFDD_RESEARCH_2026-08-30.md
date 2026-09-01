# AFDD-class arc research — HF signature detection on i.MX RT1060

**Status:** research / claim-safe architecture only. **NO-SHIP.** Host Unity is not ISR/OUTEN proof.  
**Not a listed AFDD.** This document invents **no** UL 1699B, IEC 62606, or “arc protection” product claims for the Teensy 4.1 image.

**Companion:** multi-role framing in `docs/FEATURE_ROADMAP_2026-08-30.md` (role **R-AFDD-R**) when that doc is on the branch; hard gates in `docs/PRODUCT_READINESS.md` §1–§2; algorithm detail in `docs/FEATURE_AFDD_MACAPD_ALGORITHM.md`; literature / edge-case / **WARP** wavelet-precursor deep dive in `docs/FEATURE_AFDD_MACAPD_RESEARCH_2026-08-30.md`.  
**Canvas:** `feature-afdd-research-2026-08-30.canvas.tsx` (IDE canvas).  
**MCU sources (operator local PDFs):** i.MX RT1060 RM Rev. 3 (ADC Ch.66, ADC_ETC Ch.67, eDMA, DMAMUX, XBARA), CEC Rev.4, errata Rev.11, Cortex-M7 TRM, ARMv7-M ARM. Product page: [NXP i.MX RT1060](https://www.nxp.com/products/i.MX-RT1060).

---

## 1. Problem statement

DC PV / battery strings have **no line-frequency fundamental** and no reliable current zero-crossing to hang an AC-AFCI-style detector on. Series and parallel arcs still produce a **broadband, intermittent high-frequency current (and sometimes voltage) signature**, typically discussed in the literature in a band on the order of **~1–100 kHz** (instrumentation and masking studies often emphasise tens of kHz). The useful research goal on this platform is therefore:

> Detect a **precursor / presence score** for arc-like HF energy **before** sustained damage, while **rejecting** inverter switching noise, MPPT dither, disconnects, and EMI — and never claim listing until a dual-MCU + interrupter program exists.

Speed alone is the wrong headline. Sandia-class work emphasises **masking reliability** (series inductance, C-to-ground, inverter EMI) and **provable self-test** as the product moat. UL-style unwanted-trip suites punish hair-trigger energy comparators.

---

## 2. Why the existing TEG capture path cannot do this

| Existing path | What it does | Why it fails for AFDD research |
|---------------|--------------|--------------------------------|
| `captureTick()` in PWM ISR | One V/I sample pair per carrier into a PSRAM ring | At 20 kHz carrier → ~20 kSPS → Nyquist ~10 kHz — **below** the arc HF band |
| Portable FFT / `/api/spectrum` | Operates on that same capture buffer | Spectrum of aliased / under-sampled energy; not an arc band instrument |
| `CarrierDitherMode` / `CarrierDitherPercent` | Spreads switching harmonics | **Smears** the same band detectors watch — active sabotage unless mutexed off |
| Dual Teensy_ADC modules | Simultaneous V/I for metering | Already owned by the ISR path; no ADC_ETC + eDMA HF pipeline today |
| On-chip 12-bit SAR | Up to ~1 MS/s class, ~10 ENOB (RM) | Exploratory only; PRODUCT_READINESS prefers **external ≥16-bit** for serious arc work |

**Invariant:** AFDD-class sensing needs a **separate HF acquisition path** (ADC_ETC+DMA and/or external ADC + AFE). Do not overload `captureTick` or claim the operator spectrum UI is arc-capable.

---

## 3. RT1060 capability map (what the silicon can actually do)

Grounded in RM Rev.3 / CEC (local extracts). Numbers are **design envelopes**, not a WCET claim for this firmware.

### 3.1 ADC (Ch.66)

- Dual 12-bit SAR ADCs (ADC1 / ADC2), up to ~**1 MS/s** class, ~**10 ENOB** on dedicated single-ended channels.
- Hardware trigger + channel select; compare unit; hardware average; calibration.
- CEC: `fADCK` up to **40 MHz** (ADLPC=0, ADHSC=1); conversion on the order of **~28–34+** ADCK cycles depending on sample time → hundreds of kSPS is realistic; **250–500 kSPS** is the research target band for 1–100 kHz coverage with margin.

### 3.2 ADC_ETC (Ch.67) — the key glue

- Time-division multiplexes multiple trigger sources onto ADC1/ADC2.
- Up to **8 XBAR external triggers** (TRIG0–3 → ADC1 channel0; TRIG4–7 → ADC2 channel1) plus TSC path.
- **SyncMode:** one ADC_ETC trigger drives **ADC1 and ADC2 together** (simultaneous HF V and I if two AFE channels exist).
- One trigger can launch a **chain of up to 8 segments** with programmable delay/interval.
- **DMA mode** (CTRL[DMA_MODE_SEL]) — conversion complete → DMA request without CPU per sample.
- Priority + hold when arbitration loses or ADC busy — needed if metering and HF ever share ADCs.

### 3.3 XBARA / FlexPWM / timers

- Route **PIT/GPT/QuadTimer/FlexPWM** compare events into ADC_ETC TRIG inputs for **jitter-bounded** sampling clocks independent of the PWM ISR.
- Route FlexPWM edges into a **blanking / ignore window** (software or comparator path) so known switching spikes do not inflate the arc score.

### 3.4 eDMA + DMAMUX

- Circular / ping-pong DMA destinations must be **OCRAM / `DMAMEM`** (eDMA cannot write DTCM). Optional **CPU copy into DTCM** for DSP scratch after the frame lands. PSRAM only for cold archive, not the real-time feature window (PSRAM latency + contention with capture/Ethernet).

### 3.5 Cortex-M7 DSP

- Single-precision FPU + DSP extensions (SIMD, MAC) are enough for **short STFT**, band energy, spectral kurtosis approximations, and IIR banks on 256–1024 sample frames at 250–500 kSPS **if** the work stays in DTCM and is duty-cycled.
- **Stay-off for this repo’s production image:** enabling `TEG_ENABLE_CMSIS_FFT` or global `-O3` (RAM1 floor). Research prototypes may use a **compile-flagged** CMSIS path on a lab image, or stay with portable radix-2 / Goertzel / IIR banks that fit host-testable math.

### 3.6 Errata / CEC caution

- Treat errata Rev.11 as a hard checklist before any DMA+ADC_ETC bring-up (ADC / DMA / XBAR items). Do not assume “1 MS/s brochure” survives pin capacitance, AFE settling, and simultaneous Ethernet+PWM load.

---

## 4. Proposed algorithm: **MACAPD**  
### Masking-Aware Carrier-Blanked Arc Precursor Detector

**Intent:** innovative for *this* platform — not a standard trip equation, not a UL method. Combines (a) physics of DC arc HF bursts, (b) known self-interference from *this* inverter’s carrier, and (c) RT1060 trigger/DMA features so detection is **ahead-looking** (precursor score) rather than a single energy threshold.

### 4.1 Sensing model

1. **Analog (board — does not exist yet):** HF current transducer (CT or shunt + isolation) → anti-alias / band-pass with cutoff **below Nyquist** (at default Fs=250 kSPS, Nyquist=125 kHz → prefer **f_c ≈ 0.4·Fs ≈ 100 kHz**, not a 150 kHz passband that aliases) → gain → clamp into ADC full-scale. Optional second channel: HF voltage across a sense window.
2. **Digital sample clock:** PIT or GPT → XBAR → ADC_ETC TRIG → SyncMode dual ADC @ **Fs = 250 kHz** (phase R default) or **500 kHz** (lab stretch). Frame length **N = 512** (2.048 ms @ 250 kHz) with **50% overlap**.
3. **Carrier blanking:** FlexPWM reload **and** compare edges → XBAR flag → firmware marks samples in ±*T_blank* of each switching edge as **invalid for scoring**. Prefer **exclude** blanks from moments / STFT energy (zero-filling creates sidebands). Track absolute carrier phase across overlapping frames. This is the platform-specific novelty: the detector *knows* the aggressor schedule because it *is* the aggressor MCU (or receives phase from the PWM MCU over a dual-MCU link).
4. **Dither mutex:** if `CarrierDitherMode != Off`, HF scoring is **forced inactive** (same validate / mode bit as roadmap P1). Sense mode requires dither off and a stable carrier for blanking.

### 4.2 Feature vector (per valid frame)

Compute on blanking-cleaned frame `x[n]` (and optional `v[n]`):

| ID | Feature | Rationale |
|----|---------|-----------|
| F1 | **Band energies** E_L, E_M, E_H in ~5–20 / 20–50 / 50–100 kHz (IIR or STFT bins) | Arc HF is broadband; switching is tonal at *f_c* and harmonics |
| F2 | **Tonal residual ratio** R_tonal = energy in ±Δ of *k·f_c* / total HF energy | High → inverter; low + high E → broadband candidate |
| F3 | **Spectral kurtosis** (or time-kurtosis of bandpass) SK | Impulsive intermittent arcs elevate kurtosis vs Gaussian EMI |
| F4 | **Burst duty** D_burst over a 50–200 ms horizon (fraction of frames with E_M > θ_adapt) | Arcs chatter; continuous EMI is steadier |
| F5 | **Precursor slope** dE_M/dt and dSK/dt over 0.5–2 s | “Ahead of time”: rising impulsiveness before sustained energy |
| F6 | **Cross-channel coherence** (if V and I HF both present) | Common-mode EMI vs series-arc current-dominant patterns |
| F7 | **Masking penalty** M from estimated loop L / C (operator entry or impedance probe later) | Sandia: series L and C-to-ground *hide* arcs — score must not pretend otherwise |

Adaptive noise floor θ_adapt: EWMA of E_M during **armed-but-quiet** windows (no OUTEN trip path on this image — research log only).

### 4.3 Decision logic (research score, not a trip)

```text
# Presence score (masking is NOT subtracted here)
S_raw = w1·z(E_M) + w2·z(SK) + w3·z(D_burst) + w4·z(slope)
      + w_coh·z(coherence) − w5·z(R_tonal)

observability = 1 − M   # F7 masking honesty, separate from presence

if dither_active or blanking_unavailable or AFE_fault:
    S = 0; state = INHIBITED_SENSE
elif observability < observability_min:
    state = QUIET                 # low coverage — do not arm Candidate*
elif S_raw > T_hi for persist_ms (hop-derived frames, ~50–150 ms):
    state = ARC_CANDIDATE_HIGH   # research event log only
elif S_raw > T_lo:
    state = ARC_CANDIDATE_LOW
else:
    state = QUIET
```

**Persistence** (N_persist) and dual thresholds reduce UL-style unwanted trips from single EMI spikes. On this Teensy image, **HIGH never drives OUTEN** — it only appends an EventLog / research ring (claim safety). Any future interrupter action lives on a **second MCU** with independent power and a fail-open policy.

### 4.4 “Ahead of time” precursor path

Most shipped AFCIs trip on sustained signature after ignition. MACAPD explicitly watches **F5**:

- Rising SK with modest E_M → **pre-ignition / unstable contact** hypothesis (research).
- Escalation E_M + D_burst without tonal rise → **series arc candidate**.
- E_H dominated + coherence collapse → possible parallel / EMI class (label for dataset, do not auto-trip).

This is where DC-without-frequency becomes an *advantage*: there is no 50/60 Hz envelope to wait for; the HF statistics can be scored continuously once blanking and dither policy are correct.

### 4.5 Self-test / nuisance dataset (moat, not marketing)

Until these exist, “protection” language stays forbidden:

1. **Injected HF chirp** through a lab coupler (or waveform-gen role stimulus) while OUTEN inhibited — prove the pipeline sees energy (instrument self-test, not UL AFDD self-test).
2. **Nuisance corpus:** dither on/off, MPPT steps, contactor bounce, LED drivers, radio, inverter EMI — labelled recordings from the DMA ring.
3. **Masking sweeps:** series L and C-to-ground as in Sandia-style setups — report **detection probability vs masking**, never “always detects.”

---

## 5. Firmware architecture (Teensy image vs dual-MCU)

```text
┌─────────────────────────────────────────────────────────────┐
│ Teensy 4.1 (this repo) — bench PWM + research capture       │
│  FlexPWM / OUTEN / fault / thermal / Ethernet / MTP         │
│  captureTick @ carrier ──► metering / portable spectrum     │
│  [LAB FLAG] HF path: PIT→XBAR→ADC_ETC→eDMA→OCRAM/DMAMEM→(copy DTCM)→MACAPD │
│            EventLog research states only; no OUTEN trip     │
└───────────────────────────┬─────────────────────────────────┘
                            │ future: SPI/UART ARC_CANDIDATE
┌───────────────────────────▼─────────────────────────────────┐
│ Safety MCU (not this board) — interrupter, self-test pulse  │
│  Independent trip decision, fail-open, NV log, FFI boundary │
└─────────────────────────────────────────────────────────────┘
```

### 5.1 ADC ownership modes

| Mode | Metering (`captureTick`) | HF MACAPD | Notes |
|------|--------------------------|-----------|-------|
| **M0 Production** | Owns ADC1/2 | Off | Default forever until Phase R hardware |
| **M1 Lab inhibit** | Paused | Owns ADC via ADC_ETC+DMA | Safest bring-up; OUTEN masked |
| **M2 Time-share** | ISR path | Burst windows via ADC_ETC priority | Hard; only after arbitration proven |
| **M3 External ADC** | Unchanged | SPI (+ eDMA) @ 16-bit | **Preferred** for serious sensitivity |

**Recommendation:** Phase R may use **M1** only for pipeline bring-up; migrate to **M3** before trusting any sensitivity claims. Do not ship M2 in operator builds.

### 5.1.1 Recommended M3 parts (external 16-bit)

On-chip RT1060 SAR is **12-bit / ~10 ENOB** — fine for proving DMA wiring, not for weak arc signatures under inverter EMI. Prefer an external **≥16-bit** converter clocked independently of `captureTick`.

| Role | Part (recommendation) | Why it fits TEG / RT1060 |
|------|------------------------|---------------------------|
| **Primary single-channel HF current** | **TI ADS8860** (16-bit, 1 MSPS, SPI, True-SPI / CONVST) | Matches 250–500 kSPS research Fs with headroom; LPSPI + eDMA friendly on Teensy 4.1; wide availability; EVM path for lab bring-up (`ADS8860EVM`-class) |
| **Preferred dual simultaneous I+V** | **ADI AD7380** (dual 16-bit simultaneous SAR, SPI, multi-MSPS class) | One package for HF current + HF voltage coherence (MACAPD F6); shared conversion instant replaces ADC_ETC SyncMode at the analog boundary |
| **Alt dual** | **TI ADS9224R** (dual simultaneous 16-bit SAR, SPI) | Similar dual-channel story if ADI supply is constrained |

**AFE (required with any of the above):** HF CT or isolated shunt → gain (e.g. instrumentation amp) → anti-alias LPF with **f_c ≈ 0.4·Fs** (≈100 kHz at 250 kSPS) → ADC full-scale clamp. Keep this chain off the metering pins used by `captureTick`.

**Interface sketch (not shipped firmware):** GPT/PIT or FlexPWM-derived CONVST/CS → ADS8860/AD7380 → LPSPI RX eDMA into OCRAM/`DMAMEM` ping-pong (optional CPU copy into DTCM for DSP) → `afddMacapdProcessRaw` on completed frames. Metering ADCs stay with the PWM ISR (M0/M3 coexistence).

**BOM note:** treat the table as an engineering starting point, not a certified AFDD reference design. Validate SNR, CMRR, and aliasing on the actual AFE before claiming detection probability.

### 5.1.2 Host-testable MACAPD math (no trip wiring)

- Header: `src/afdd_macapd.h` — blanking, Goertzel band energies, kurtosis, burst duty, precursor slopes, persistence state machine.
- Detail: `docs/FEATURE_AFDD_MACAPD_ALGORITHM.md`.
- Native test: `test/test_afdd_macapd/` (not part of the six-suite gate; report separately if run).
- **Must not** call OUTEN / fault trip paths. Compile/default remains “unused” until an explicit lab flag and AFE exist.

### 5.2 Memory / CPU budget (order-of-magnitude)

- 250 kSPS × 2 ch × 2 bytes ≈ **1 MB/s** into OCRAM/`DMAMEM` ping-pong (e.g. 8 KiB), then optional DTCM copy for DSP.
- 512-pt real FFT ~ every 1 ms overlap → feasible on 600 MHz M7 if FFT is occasional; prefer **IIR band energies + Goertzel notches at k·f_c** for continuous duty.
- Keep research DSP out of FAULT/OUTEN IRQ paths; run from main loop or a low-priority PIT soft IRQ with WDOG-aware budgets.

### 5.3 Config / validate hooks (when code exists)

- `HfSense.Enabled` default **false**; compile gate `TEG_WITH_HF_SENSE=0` default.
- Validate: `HfSense.Enabled` ⇒ `CarrierDitherMode == Off` and blanking source configured.
- Status JSON: expose `hfSenseState`, `hfScore`, `hfMaskingPenalty` as **research fields** with UI copy “HF research (not protection).”

---

## 6. Alternatives considered

| Alternative | Why not primary |
|-------------|-----------------|
| Raise carrier and reuse `captureTick` | Still one sample/carrier; carrier≠Nyquist for 100 kHz; wrecks EMI and thermal timing |
| Energy threshold on portable spectrum | Wrong buffer; no blanking; dither collision; false product signal |
| Full cyclostationary cyclic spectrum every frame | Too heavy for continuous duty on shared MCU with QNEthernet |
| ML classifier on-device | Dataset and FFI nightmare; host offline training OK, MCU inference later |
| CMSIS FFT always-on in production | Conflicts with RAM1 / project stay-off; lab-only flag if ever |

---

## 7. GO / NO-GO gates

| Gate | GO means | NO-GO means |
|------|----------|-------------|
| Docs / claim hygiene | This note + canvas + memories; no “AFDD” in operator trip UI | Marketing copy on Teensy image |
| AFE hardware | HF CT + BPF measured FR | Software-only “arc detect” PR |
| Acquisition | ADC_ETC+DMA (or external) ≥250 kSPS proven with scope | Piggyback on capture ring |
| Dither mutex | Validate refuses sense+dither | Both enabled |
| Dual-MCU | Architecture + handshake stub only until board | Trip from Ethernet MCU alone |
| Listing | Never on this program phase | UL 1699B language |

**Near-term firmware PR policy:** **NO-GO** for AFDD trip / OUTEN wiring. Docs, M3 part recommendation, and host-testable MACAPD math (`src/afdd_macapd.h` + `test/test_afdd_macapd`) may land without claiming hardware detection.

---

## 8. Suggested research sequence

1. **Phase N:** claim-safe algorithm + MCU mapping + canvas + MACAPD header/tests + M3 part pick.  
2. **Host math:** `test/test_afdd_macapd` on synthetic bursts vs tonal carriers (Unity native) — landed.  
3. **Bench AFE + ADS8860 (or AD7380):** BOM; measure transfer; inject known HF.  
4. **M1 DMA bring-up (optional):** inhibited PWM; stream rings; compare to M3.  
5. **M3 LPSPI+eDMA:** feed frames into `afddMacapdProcessRaw`; EventLog research states only.  
6. **Nuisance + masking corpus.**  
7. **Dual-MCU program** (separate repo/hardware) if productisation is ever funded.

---

## 9. Explicit non-claims

- Not UL 1699B / IEC AFDD compliant.  
- Not grid-tie protection.  
- Not proven on this board (no HF AFE yet).  
- Not ISR/OUTEN proof.  
- Host tests of feature math ≠ field detection.  
- DC “ahead of time” is a **research hypothesis** about precursor statistics, not a certified early-warning product.

---

## 10. References (context, not certification)

- NXP i.MX RT1060 product / RM Ch.66–67, CEC, errata (local PDFs cited above).  
- Sandia / OSTI PV arc-fault detection and **masking** literature (series L, capacitance to ground, inverter noise, unwanted trip). Cite as research context only.  
- In-repo: `docs/PRODUCT_READINESS.md`, `docs/FEATURE_ROADMAP_2026-08-30.md` (R-AFDD-R), `docs/FEATURE_AFDD_MACAPD_ALGORITHM.md`, `src/afdd_macapd.h`, `src/capture.cpp` / `capture.h`.
- M3 ADC datasheets: TI ADS8860; ADI AD7380 (and TI ADS9224R as alt).

---

## 11. Document control

| Field | Value |
|-------|--------|
| Date | 2026-08-30 |
| Branch class | Feature-branch docs + host math PR |
| Algorithm name | MACAPD |
| Default Fs / N | 250 kHz / 512 |
| M3 primary part | TI ADS8860 (dual: ADI AD7380) |
| Trip authority on Teensy image | **None** |
