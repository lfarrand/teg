# MACAPD deep research — literature, edge cases, and algorithm elaboration

**Status:** research / claim-safe. **NO-SHIP.**  
**Not a listed AFDD / AFCI.** This note synthesises public literature and internal design critique for the proposed **MACAPD** pipeline. It invents **no** UL 1699B, IEC 62606, or “arc protection” product claims for the Teensy 4.1 image. Host Unity tests of `src/afdd_macapd.h` are **not** ISR/OUTEN proof and are **not** listing evidence.

| Companion | Role |
|-----------|------|
| [`FEATURE_AFDD_RESEARCH_2026-08-30.md`](FEATURE_AFDD_RESEARCH_2026-08-30.md) | Platform architecture (RT1060, M0–M3, OUTEN policy) |
| [`FEATURE_AFDD_MACAPD_ALGORITHM.md`](FEATURE_AFDD_MACAPD_ALGORITHM.md) | Stage-by-stage algorithm + host API |
| `src/afdd_macapd.h` | Host-testable math (never drives OUTEN) |
| `plan/feature-afdd-research-1.md` | Implementation plan |

---

## 1. Executive findings

1. **DC arcs need HF signatures, not line-frequency hooks.** Series arcs inject broadband / pink-like conducted noise; inverters inject strong tonal switching from roughly **1 kHz to >100 kHz**. Detectors must separate those classes under **masking** (series L, C-to-ground) and **unwanted-trip** (irradiance steps, optimizers, EMI) pressure — not merely raise energy thresholds.
2. **MACAPD’s architecture matches the literature’s hard constraints** for this platform: separate HF path, carrier-aware blanking, multi-feature score (band energy + impulsiveness + burst + precursor slopes), dither mutex, masking honesty, dual-MCU before any trip claim.
3. **The present host math is a research skeleton, not a field detector.** Several literature-shaped edges are soft or unimplemented: blank zeros bias moments; tonal penalty is single-bin not ±Δ; EWMA adapts into events; `maskingPenalty` / coherence barely drive physics; Fs/Nyquist and AFE aliasing are unchecked; persistence is frame-count not energy-budgeted ride-through.
4. **Listing / field history is a warning, not a target.** Sandia/Tigo surveys of UL-listed products still found missed arcs and nuisance trips under realistic extras. That is why Teensy MACAPD scores must **never** drive OUTEN.

---

## 2. Literature synthesis (what the field actually struggles with)

### 2.1 Propagation, pink noise, and inverter EMI (Sandia DETL)

Sandia / Eaton DETL work on PV DC arc-fault detectors ([OSTI 1120327](https://www.osti.gov/servlets/purl/1120327)) frames the problem as a **propagation chain**, not a local spark meter:

1. Arc initiates → injects roughly **1/f (“pink”)** AC noise on the DC current.
2. Modules, connectors, and wiring **frequency-dependently attenuate** the signature.
3. Antenna / crosstalk / RF effects further reshape the profile.
4. By the time the detector samples, the spectrum may no longer look like the source arc.
5. The inverter injects switching fundamentals and harmonics typically from **~1 kHz to >100 kHz**.

Implication for MACAPD: watching a fixed mid-band (default ~20–50 kHz) is reasonable only if (a) the AFE/ADC can see that band without aliasing, (b) **local carrier edges** are blanked so PWM harmonics do not dominate, and (c) **attenuation / masking** is treated as a first-class failure mode (honesty penalty or dual-site sensing), not an afterthought.

Related FR / robust-detector work ([OSTI 1119759](https://www.osti.gov/servlets/purl/1119759)) emphasises avoiding reliance on frequencies that are also strong inverter harmonics and characterising module/line frequency response — again: **geometry and EMI class matter more than a single FFT peak**.

### 2.2 Masking and unwanted tripping beyond the listing suite

An independent survey of UL-listed / recognised / prototype AFCI–AFD products ([OSTI 1648697](https://www.osti.gov/servlets/purl/1648697)) is the single most important “edge case” paper for this research track. Key experimental classes:

| Class | What happened in the survey | MACAPD relevance |
|-------|-----------------------------|------------------|
| **Missed arcs** | Some listed products failed to detect all 100–300 W arcs under the lab’s conditions | Persistence + sensitivity must not assume “energy above floor ⇒ trip”; weak / intermittent arcs need precursor features |
| **Series L masking** | ~994 µH-class series inductance hid arcs from some detectors | `maskingPenalty` concept; prefer sensing near the arc or dual-site; do not claim coverage when L is large |
| **C-to-ground** | ~1.5 µF to chassis could mask **or** cause unwanted trip via common-mode coupling | Dual I/V / CM awareness; AFE grounding discipline |
| **Inverter / supply EMI** | Startup, DC contactor close, MPPT, noisy supplies (e.g. ~278 kHz) caused nuisance trips | Carrier blanking + tonal residual + dither inhibit |
| **Optimizers / DC–DC** | Tigo-class converters change the noise floor | Treat MLPE as a distinct aggressor class; blanking alone may be insufficient |
| **Irradiance / string reconnect** | Current-step detectors false-tripped on string disconnect/reconnect and simulator irradiance steps | Do **not** key primarily on ΔI_DC; HF statistical features + persistence |
| **Monotone frequency sweep** | Injected ~100 dBµA tones 1–500 kHz tripped some FFT-style detectors | Single-frequency energy thresholds are fragile; broadband + kurtosis + burst duty |
| **Inductive coupling between arrays** | Parallel conduit runs coupled switching into other strings | Common-mode / multi-string honesty; EventLog only on Teensy |
| **Short broadband transients** | Products with ~62 ms trip times risk riding poorly through disconnect clicks | Persistence / ride-through before CandidateHigh; energy-before-interrupt is a dual-MCU concern |

The survey’s conclusion — that **wavelet / richer algorithms** may help where FFT-only fails — aligns with later statistical and multi-stage papers, but does **not** authorise product claims here.

### 2.3 Academic / thesis grounding (filters, standards gaps, pink-noise physics)

- **UBC thesis on digital filters for PV arc-fault detection** ([DOI 10.14288/1.0445539](https://doi.org/10.14288/1.0445539)): DC systems lack AC zero-cross timing; UL 1699B-style suites leave gaps around converter noise, rapid-shutdown / PLC, and realistic EMI. Filter design must preserve arc band while rejecting switching — the same trade MACAPD’s blanking + anti-alias AFE try to make in hardware/time.
- **Northeastern dissertation on pink-noise propagation** ([DOI 10.17760/d20449064](https://doi.org/10.17760/d20449064)): wire inductance, stray capacitance, and converter switching **mask series arcs**. Supports Sandia masking results and MACAPD’s “masking-aware” naming — but only if the penalty (or multi-sensor layout) is real, not a default-zero knob.

### 2.4 Cutting-edge detection themes (2022–2025)

These are **research methods**, not recipes we claim to implement on Teensy:

| Theme | Representative work | Takeaway for MACAPD |
|-------|---------------------|---------------------|
| **HF statistical features** (entropy, kurtosis, skew, losses) | [ACM 2023](https://doi.org/10.1145/3650400.3650416) | Excess kurtosis / impulsiveness is a valid arc cue; load-change rejection needs more than one moment |
| **Two-stage transient + steady + spectral kurtosis** | [Solar Energy 2024](https://www.sciencedirect.com/science/article/abs/pii/S0038092X24007795) | Separate “onset” from “sustained”; spectral (bandpass) kurtosis often beats raw time kurtosis on blanked zeros |
| **AR / randomness vs PWM stationarity** | [Appl. Sci. 2022 AR model](https://doi.org/10.3390/app122010379) | Arc ≈ non-stationary random; inverter noise ≈ more structured — MACAPD’s tonal residual is a crude cousin of this idea |
| **ICA + DTW on global non-Gaussianity** | [Sensors / PMC12526851](https://pmc.ncbi.nlm.nih.gov/articles/PMC12526851/) | Local FFT/DWT features fail in noisy environments; global independence metrics need dual/multi channels and compute beyond current host header |
| **Relative variability + impedance-guided bands** | [IEEE Access-class small-signal band selection](https://ieeexplore.ieee.org/document/9209997) | Choose analysis bands to **avoid** switching noise using plant impedance — motivates adaptive band centres, not only fixed 20/50/100 kHz |
| **Kurtosis for arc shoulder / leptokurtosis** | [Appl. Sci. 2022 distribution networks](https://doi.org/10.3390/app12062777) | Kurtosis rises when amplitude distribution sharpens; short windows and forced zeros invalidate textbook thresholds |
| **False-alarm / AGI reduction (instrumentation)** | [IEEE TIM 2025](https://doi.org/10.1109/tim.2025.3635319) | Field products still fight nuisance trips; research trend is multi-evidence fusion + persistence, not faster single features |

**Field nuisance context** (installer / vendor literature, not peer-reviewed): RSD / optimizers, cable capacitance, and EMI are repeatedly blamed for false AFCI trips ([example overview](https://www.anernstore.com/blogs/diy-solar-guides/false-arc-trips-pv-arrays)). Treat as **scenario checklist**, not as proof of any algorithm.

---

## 3. How MACAPD is intended to work (elaborated)

This section elaborates the **design intent**. Where the host header currently diverges, §5 calls it out explicitly.

### 3.1 Problem MACAPD owns on TEG

```text
DC string / bus ──► HF CT or shunt AFE ──► anti-alias ──► ADC (M3 16-bit preferred)
                                                              │
FlexPWM carrier fc, edge times ───────────────────────────────┼──► blank mask
                                                              ▼
                                                    float frame i[n] (± optional v[n])
                                                              │
                    dither / AFE / blanking gates ──► Inhibited or armed
                                                              ▼
                                              features → score → persist → sense state
                                                              │
                                                              ▼
                                    EventLog / offline research ONLY
                                    (never Teensy OUTEN / releaseOutputInhibit)
```

**Why not `captureTick`?** At a 20 kHz carrier, one sample per carrier ≈ 20 kSPS → Nyquist ≈ 10 kHz. The literature arc band of interest sits largely **above** that. Carrier dither **smears** switching energy across the same band detectors watch. MACAPD therefore **requires** a separate HF path and a dither/detect mutex.

### 3.2 Stage A — Acquisition and blanking

1. Sample at research target **≥ ~250 kSPS** (margin for 1–100 kHz with anti-alias at ~0.4·Fs).
2. Prefer **external ≥16-bit** (M3: e.g. ADS8860 / AD7380 class) over on-chip 12-bit exploratory (M1).
3. Build a **keep/blank mask** from FlexPWM edges: discard (or ideally **exclude from moments**) samples within ±`Tb` of each edge so local PWM harmonics do not dominate Goertzel/FFT bins.
4. If dither is active, blanking is unavailable, or AFE fault is set → **Inhibited** (features cleared, no Candidate*).

Blanking is **time-domain knowledge of the aggressor**, not a magic filter. It fails if edge phase is wrong, `Tb` is too wide (blind), or too narrow (EMI leaks).

### 3.3 Stage B — Feature extraction (multi-evidence)

| Feature | Physics / literature link | Role in score |
|---------|---------------------------|---------------|
| **eL / eM / eH** band energies | Pink / broadband arc vs tonal PWM; mid-band often used when inverter peaks are known | Primary energy cue (`eM` weighted) |
| **rTonal** | Energy at `k·fc` relative to broadband — inverter / dither residue | **Penalty** (not a trip cue) |
| **Excess kurtosis** | Non-Gaussian, impulsive arcs vs nearer-Gaussian EMI | Impulsiveness weight |
| **dBurst** | Intermittent electrode / plasma dynamics → duty of elevated frames | Rejects single EMI spikes somewhat |
| **slopeEm / slopeSk** | Precursor: rising energy/kurtosis before sustained trip energy | Early warning without hair-trigger |
| **coherence \|corr(i,v)\|** | Series vs parallel / CM vs DM discrimination when dual-channel | Intended weight; soft in host math today |
| **maskingPenalty** | Sandia L / C-to-ground honesty | Subtract from score when coverage is physically compromised |

### 3.4 Stage C — Score fusion

Design form (weights configurable):

```text
S ≈  wBand·z(eM) + wKurt·κ + wBurst·dBurst + wSlope·(slopeEm,slopeSk)
   − wTonal·rTonal − wMask·maskingPenalty
```

`z(·)` is intended as an adaptive floor (EWMA of quiet mid-band). Soft ratios in the host header are a stand-in until true z-scores / robust MAD floors exist.

### 3.5 Stage D — Persistence state machine

| State | Meaning |
|-------|---------|
| `Inhibited` | Dither / no blanking / AFE fault / invalid frame |
| `Quiet` | Armed, score below low threshold |
| `CandidateLow` | Score ≥ `tLo` — watch / log |
| `CandidateHigh` | Score ≥ `tHi` for `nPersist` consecutive frames — strong research flag |

Persistence is the ride-through analogue of Sandia’s “don’t trip on 50–150 ms disconnect clicks” advice — **on Teensy it only changes a sense enum**, never OUTEN.

### 3.6 Dual-MCU boundary (non-negotiable)

Any future interrupter / OUTEN-class action lives on a **separate MCU + hardware path** with self-test, after disconnected bench evidence and a real safety program. MACAPD on Teensy is **instrumentation**.

---

## 4. Edge-case matrix (unhappy paths)

| # | Edge case | Literature / field signal | Expected MACAPD behaviour | Current host-math risk |
|---|-----------|---------------------------|---------------------------|------------------------|
| 1 | Series inductance hides arc | Sandia masking with ~mH-class L | Raise `maskingPenalty` or declare Inhibited / “blind”; dual-site sense | Default penalty **0** → false confidence |
| 2 | Cap to ground | Mask **or** CM nuisance | Dual I/V; CM rejection in AFE | Coherence unused in score |
| 3 | Inverter switching in-band | DETL 1–100+ kHz EMI | Blank edges; tonal penalty | Blank phase assumes index-aligned carrier; single-bin tonal |
| 4 | Carrier dither on | Research mutex | Force Inhibited | Correct **if** `ditherActive` flag is wired |
| 5 | Blanking too wide | Blind detector | Inhibit or “low keep-count” flag | Kurtosis → 0 when `m2` tiny; looks Quiet while blind |
| 6 | Blanking too narrow / phase slip | EMI leaks into eM | Score rises → nuisance Candidate* | No FlexPWM timestamp lock in header |
| 7 | Zero-stuffed blanks | DSP artefact | Prefer exclude-from-moments | Zeros inflate/deflate kurtosis & leak spectrum |
| 8 | Irradiance / string step | Sandia unwanted trip | HF features + persist; ignore ΔI_DC alone | No DC ΔI path (good) but EWMA cold-start can sit on `tLo` |
| 9 | Optimizer / RSD / PLC | Thesis + field false trips | Aggressor class; may need inhibit during RSD chatter | Not modelled |
| 10 | Parallel vs series arc | Different V/I signatures | Coherence / dual channel | I-only score |
| 11 | Weak / pull-apart low-power arc | Missed detection in surveys | Precursor slopes + lower bands | Slopes are 1-frame Δ only |
| 12 | Narrowband tone sweep | Coupling-transformer nuisance | Broadband + kurtosis + persist | Pure mid-band tone can still lift eM |
| 13 | Aliasing (weak AA filter) | AFE dependency | Reject / inhibit | No Fs vs band validation |
| 14 | 12-bit ENOB exploratory | PRODUCT_READINESS M3 preference | Log as low-trust | Math will happily score noisy frames |
| 15 | Cross-array inductive coupling | Conduit coupling trips | CM honesty; multi-string | Single-channel assumption |
| 16 | Hair-trigger persistence | ~62 ms product trips | `nPersist` + future energy budget | Frame count ≠ joule budget |
| 17 | EWMA adapts into arc | Adaptive floor literature pitfall | Freeze floor when Candidate* | Floor updates every armed frame |
| 18 | Goertzel centres miss energy | Fixed-band fragility | Adaptive / impedance-guided bands | Three centres only; eL/eH unused in weights |
| 19 | Undersampled buffer fed to API | CaptureTick misuse | Inhibit / assert Fs | No hard reject |
| 20 | Operator treats CandidateHigh as protection | Claim hygiene | Docs + UI language | Process / memory invariant |

---

## 5. Gap analysis: design intent vs `src/afdd_macapd.h`

Happy-path assumptions the code encodes well: separate float HF frame API; dither / blanking / AFE inhibit; mid-band Goertzel triplet; excess kurtosis; burst duty history; simple score + persistence; `maskingPenalty` hook.

Gaps to treat as **research backlog** (not UL work):

1. **Blank zeros vs exclude** — moments and Goertzel see periodic zeros (blanking modulation).
2. **Tonal ±Δ notch** — docs say neighbourhood of `k·fc`; code is exact bin only → carrier jitter under-penalises inverter energy.
3. **EWMA policy** — seeds on first frame (`zBand≈1` near `tLo`); continues adapting during strong events.
4. **Coherence not in `scoreRaw`** — F6 is computed then ignored.
5. **`maskingPenalty` default 0** — “masking-aware” is opt-in honesty, not physics.
6. **No Fs / Nyquist / keep-count guards** — undersampled or over-blanked frames can look Quiet.
7. **Precursor horizon** — research text 0.5–2 s; code is one-frame Δ.
8. **Overlap / STFT** — no 50% overlap streaming model in the header.
9. **API inconsistency** — `ProcessRaw` truncates `n > MAX_N`; `ProcessFrame` inhibits.

Suggested **host synthetic** hardenings (claim only that math responds as coded): blank phase slip; blank duty sweep; kurtosis zeros-vs-exclude; tonal `fc+δ`; Fs floor; cold-start EWMA; floor contamination mid-event; masking knob delta; coherence inertness lock; flag matrix; aliased aggressor; length mismatch. Existing `test_afdd_macapd` is smoke, not this matrix.

---

## 6. Recommendations (research program only)

| Priority | Recommendation | Why |
|----------|----------------|-----|
| P0 | Keep Teensy MACAPD **off OUTEN** forever in this image | Literature + Sandia: listed products still fail; bench instrument ≠ interrupter |
| P0 | Prefer **M3 16-bit** + proper AA for any serious capture | ENOB / aliasing dominate weak-arc SNR |
| P1 | Wire **ditherActive** from real `CarrierDitherMode` when sense lands | Mutex is only as good as the flag |
| P1 | Change kurtosis / moments to **mask-exclude**; add keep-count inhibit | Fixes blank-zero bias and “Quiet while blind” |
| P1 | Widen tonal residual to **±Δ bins** or short STFT ridge | Matches docs; fights carrier drift |
| P2 | Freeze EWMA while Candidate*; longer slope windows | Stops floor chasing the arc; real precursors |
| P2 | Put coherence (and optionally eL/eH ratios) into the score with weights | Parallel / CM discrimination |
| P2 | Estimate or configure masking from known string L / C or self-test | Make “masking-aware” honest |
| P3 | Explore AR residual / spectral kurtosis / wavelet as **offline** competitors | Literature alternatives; not Teensy trip paths |
| P3 | Build a labelled capture library (quiet / dither / arc-like / optimizer) | Sandia-style replay tuning without claiming listing |

---

## 7. Non-claims (repeat for claim hygiene)

- This document is **not** evidence of UL 1699B / IEC AFDD compliance.
- Host tests of MACAPD are **not** disconnected-bench proof and **not** ISR/OUTEN proof.
- CandidateHigh is **not** a trip command.
- Preferring ADS8860 / AD7380-class parts is a **lab instrumentation** recommendation, not a certified BOM for AFCI.
- Citing Sandia / IEEE / ACM work acknowledges **problem structure**, not that MACAPD reproduces their results on TEG hardware.

---

## 8. Sources

### National lab / standards-context (primary)

1. J. Johnson et al., *Photovoltaic DC Arc Fault Detector Testing at Sandia National Laboratories* — [OSTI 1120327](https://www.osti.gov/servlets/purl/1120327).
2. Sandia work on PV module/line frequency response for robust detectors — [OSTI 1119759](https://www.osti.gov/servlets/purl/1119759).
3. J. Johnson et al., *Unwanted Tripping Survey of UL 1699B-Listed / Related PV Arc-Fault Products* (masking & nuisance classes) — [OSTI 1648697](https://www.osti.gov/servlets/purl/1648697).

### Theses

4. UBC thesis: digital filters for PV arc-fault detection — [DOI 10.14288/1.0445539](https://doi.org/10.14288/1.0445539).
5. Northeastern dissertation: pink-noise propagation / masking in PV DC — [DOI 10.17760/d20449064](https://doi.org/10.17760/d20449064).

### Recent methods (illustrative)

6. HF statistical features (entropy / kurtosis / skew) — [DOI 10.1145/3650400.3650416](https://doi.org/10.1145/3650400.3650416).
7. Two-stage + spectral kurtosis (Solar Energy 2024) — [ScienceDirect abstract](https://www.sciencedirect.com/science/article/abs/pii/S0038092X24007795).
8. AR-model DC arc detection under PWM noise — [DOI 10.3390/app122010379](https://doi.org/10.3390/app122010379).
9. ICA–DTW PV series arc detection — [PMC12526851](https://pmc.ncbi.nlm.nih.gov/articles/PMC12526851/).
10. Kurtosis-based arc detection (distribution networks) — [DOI 10.3390/app12062777](https://doi.org/10.3390/app12062777).
11. Relative variability / impedance-guided bands vs inverter switching — [IEEE Xplore 9209997](https://ieeexplore.ieee.org/document/9209997).
12. Instrumentation false-alarm reduction (IEEE TIM 2025) — [DOI 10.1109/tim.2025.3635319](https://doi.org/10.1109/tim.2025.3635319).

### Field / installer context (non-peer-reviewed checklist)

13. Example discussion of PV AFCI false trips (RSD / capacitance / EMI) — [Anern overview](https://www.anernstore.com/blogs/diy-solar-guides/false-arc-trips-pv-arrays).

### In-repo

14. [`FEATURE_AFDD_RESEARCH_2026-08-30.md`](FEATURE_AFDD_RESEARCH_2026-08-30.md), [`FEATURE_AFDD_MACAPD_ALGORITHM.md`](FEATURE_AFDD_MACAPD_ALGORITHM.md), `src/afdd_macapd.h`, `test/test_afdd_macapd/`.
