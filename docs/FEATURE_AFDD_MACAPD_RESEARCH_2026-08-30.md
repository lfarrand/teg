# MACAPD deep research — literature, edge cases, and algorithm elaboration

**Status:** research / claim-safe. **NO-SHIP.**  
**Not a listed AFDD / AFCI.** This note synthesises public literature and internal design critique for the proposed **MACAPD** pipeline. It invents **no** UL 1699B, IEC 62606, or “arc protection” product claims for the Teensy 4.1 image. Host Unity tests of `src/afdd_macapd.h` are **not** ISR/OUTEN proof and are **not** listing evidence.

| Companion | Role |
|-----------|------|
| [`FEATURE_AFDD_RESEARCH_2026-08-30.md`](FEATURE_AFDD_RESEARCH_2026-08-30.md) | Platform architecture (RT1060, M0–M3, OUTEN policy) |
| [`FEATURE_AFDD_MACAPD_ALGORITHM.md`](FEATURE_AFDD_MACAPD_ALGORITHM.md) | Stage-by-stage MACAPD algorithm + host API |
| [`FEATURE_AFDD_MACAPD_MEF_2026-08-30.md`](FEATURE_AFDD_MACAPD_MEF_2026-08-30.md) | Multi-evidence fusion — 20 channels E01–E20 |
| **§6 WARP (this file)** | Wavelet-first **precursor** sibling (irregularity before sustained energy) |
| `src/afdd_macapd.h` | Host-testable MACAPD math (never drives OUTEN) |
| `src/afdd_warp.h` | Host-testable WARP math (Haar WPT interim; never OUTEN) |
| `plan/feature-afdd-research-1.md` | Initial research plan |
| `plan/feature-afdd-macapd-recs-1.md` | Recommendation walkthrough plan |

---

## 1. Executive findings

1. **DC arcs need HF signatures, not line-frequency hooks.** Series arcs inject broadband / pink-like conducted noise; inverters inject strong tonal switching from roughly **1 kHz to >100 kHz**. Detectors must separate those classes under **masking** (series L, C-to-ground) and **unwanted-trip** (irradiance steps, optimizers, EMI) pressure — not merely raise energy thresholds.
2. **MACAPD’s architecture matches the literature’s hard constraints** for this platform: separate HF path, carrier-aware blanking, multi-feature score (band energy + impulsiveness + burst + precursor slopes), dither mutex, masking honesty, dual-MCU before any trip claim.
3. **The present host math is a research skeleton, not a field detector.** Several literature-shaped edges are soft or unimplemented: blank zeros bias moments; tonal penalty is single-bin not ±Δ; EWMA adapts into events; `maskingPenalty` / coherence barely drive physics; Fs/Nyquist and AFE aliasing are unchecked; persistence is frame-count not energy-budgeted ride-through.
4. **Listing / field history is a warning, not a target.** Sandia/Tigo surveys of UL-listed products still found missed arcs and nuisance trips under realistic extras. That is why Teensy MACAPD scores must **never** drive OUTEN.
5. **FFT-only energy is a late cue.** Intermittent micro-discharges and contact chatter raise **time-local irregularity** (wavelet packet CV, entropy, micro-burst rate) **before** mean mid-band energy would trip a naive FFT/Goertzel detector. **WARP** (§6) is the proposed wavelet-first precursor sibling: primary tool is lifting **db4 WPT**; FFT/Goertzel is **tonal EMI penalty only**. Still research-only; still never OUTEN.

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

The survey’s conclusion — that **wavelet / richer algorithms** may help where FFT-only fails — aligns with later statistical and multi-stage papers, but does **not** authorise product claims here. That citation is the explicit motivation for **WARP** (§6): keep carrier blanking and unwanted-trip honesty, but replace “mean FFT bin above threshold” as the *early* cue with **multi-scale irregularity**.

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
| **Wavelet / WPT + SVM / entropy** | [Yao et al. TPEL 2013](https://doi.org/10.1109/tpel.2013.2273292); [Xia et al. IEEJ 2018](https://doi.org/10.1002/tee.22797); [WT+MFE](https://doi.org/10.1109/phm-qingdao46334.2019.8942846); [fractional wavelet energy entropy](https://doi.org/10.1063/5.0186731); [WPT+RCNN+SVM](https://doi.org/10.1063/5.0205503) | Wavelets localise intermittent bursts better than global FFT; packet energy / entropy / multi-scale fuzzy entropy are standard rich features |
| **Entropy / Tsallis / Hurst without FFT** | Northeastern survey in [DOI 10.17760/d20449064](https://doi.org/10.17760/d20449064) | Disorder / long-memory cues can flag arc-like noise with linear cost in window length; still EMI-vulnerable without blanking |
| **Two-stage pre-arc then verify** | Cited in same dissertation (WT+SVM pre-detection then second SVM) | Separates **precursor watch** from **sustained confirm** — the state-machine pattern WARP adopts |

**Field nuisance context** (installer / vendor literature, not peer-reviewed): RSD / optimizers, cable capacitance, and EMI are repeatedly blamed for false AFCI trips ([example overview](https://www.anernstore.com/blogs/diy-solar-guides/false-arc-trips-pv-arrays)). Treat as **scenario checklist**, not as proof of any algorithm.

**Honest limit on “before they happen”:** No algorithm sees a future plasma that has not begun. “Precursor” means detecting **intermittent micro-discharges, rising contact irregularity, and multi-scale disorder** that typically appear **before sustained arc energy** would trip a naive mid-band energy detector — not clairvoyance, and not a certified early-warning product.

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

## 6. WARP — Wavelet-Augmented Rich Precursor

**Status:** research blueprint only. **NO-SHIP.** Not implemented in `src/` yet. Never drives Teensy OUTEN. Host Unity / synthetic “precursor before energy” tests would **not** be bench proof or UL evidence.

### 6.1 Why FFT alone is late

A global FFT (or three Goertzel centres) answers: “is there elevated energy in band X *in this window*?” Sustained arcs often do raise that energy — but **contact degradation and intermittent micro-discharges** first appear as **short, sparse, non-stationary bursts** whose energy averages out over a long FFT window. Wavelets (compact support, multi-resolution) localise those bursts in time *and* scale. Sandia’s unwanted-trip survey ([OSTI 1648697](https://www.osti.gov/servlets/purl/1648697)) explicitly pointed at wavelet / richer methods where FFT-style detectors struggled; PV and EV literature since then routinely uses DWT/WPT energy, wavelet entropy, multi-scale fuzzy entropy, and two-stage pre-detect → verify classifiers ([Yao TPEL 2013](https://doi.org/10.1109/tpel.2013.2273292), [Xia IEEJ 2018](https://doi.org/10.1002/tee.22797), [WT+MFE](https://doi.org/10.1109/phm-qingdao46334.2019.8942846), [fractional wavelet entropy](https://doi.org/10.1063/5.0186731)).

**WARP** keeps MACAPD’s platform spine (separate HF path, carrier blanking, dither inhibit, masking honesty) and adds a **wavelet-first precursor score**. FFT/Goertzel is retained **only** as a tonal EMI **penalty**, not as the early-warning cue.

### 6.2 Name and role beside MACAPD

**WARP** = **W**avelet-**A**ugmented **R**ich **P**recursor.

| Piece | Role |
|-------|------|
| MACAPD | Sustained / impulsive arc-like score (Goertzel bands, time kurtosis, burst duty) |
| WARP | **Pre-energy** irregularity over a **0.5–2 s** horizon (packet CV, entropy, micro-burst rate, packet kurtosis, slopes) |
| Joint | Soft fusion for logging; `PrecursorWatch` can fire while mean arc-band energy is still below a naive energy trip |

```text
blanked HF frame i[n]
        │
        ├──────────────────────────────┐
        ▼                              ▼
   MACAPD features                lifting db4 WPT (J=3)
   S_macapd                       → packet Ep, Hw, κpkt
                                  → horizon I_irr, rμ, slopes
                                  Goertzel rTonal (penalty ONLY)
                                       │
                                       ▼
                                    S_warp
        │                              │
        └──────── S_joint ─────────────┘
                      │
                      ▼
         Quiet / PrecursorWatch / Candidate*  → EventLog ONLY
```

### 6.3 Transform choice (defaults)

| Choice | Default | Why |
|--------|---------|-----|
| Primary TF | **Lifting Wavelet Packet Transform** | Splits both low and high branches → uniform resolution in the ~15–100 kHz arc interest band (plain DWT starves HF detail) |
| Mother | **db4** (`sym4` lab A/B) | Compact, 4 vanishing moments, lifting-friendly on Cortex-M7 offline |
| Depth | **J = 3** (lab J=4) | 8 terminal packets; ~15.625 kHz each at Fs=250 kHz |
| Decimation | Decimated WPT default | O(N·J) MACCs; undecimated/SWT optional lab-only for shift invariance |
| Frame | N=512 (~2.05 ms), optional 50% hop | Overlap mitigates WPT shift variance |
| Blanking | Same ±Tb as MACAPD; **prefer exclude** blanks from coeff moments | Avoid blank-zero kurtosis bias (§4 #7, §5) |

**Packet map at Fs = 250 kHz, J = 3** (after frequency reordering):

| Packet | Approx Hz | Default use |
|--------|-----------|-------------|
| p0 | 0–15.625 | Near-DC / low EMI (`P_lo`) |
| p1–p4 | 15.625–78.125 | **Arc interest** `P_arc` |
| p5–p6 | 78.125–109.375 | Upper HF (`P_hi`) |
| p7 | 109.375–125 | Near Nyquist honesty |

**Complexity:** lifting db4 WPT ≈ \(c\cdot N\cdot J\) MACCs with \(c\sim 8\)–\(12\). At N=512, J=3 → ~15k–20k MACCs/frame — fine for **offline / lab M7**, not for FAULT/OUTEN IRQ or the Teensy control-loop WCET until explicitly budgeted. Prefer DTCM scratch; no EXTMEM dependence for coeffs.

### 6.4 Feature set (rich, wavelet-first)

All on **kept** samples / valid coeff indices.

1. **Packet energies** \(E_p\), \(E_{\mathrm{arc}}=\sum_{p\in P_{\mathrm{arc}}} E_p\).
2. **Subband irregularity (precursor core)** over horizon \(T_H\in[0.5,2]\) s (default 1 s → hop-derived \(H\): at N=512 with **50% overlap** hop=256 → \(H\approx 977\) frames @ 250 kSPS; non-overlapped hop=N → \(H\approx 488\)):
   \[
   \mathrm{CV}_p=\sigma_p/(\mu_p+\varepsilon),\quad
   I_{\mathrm{irr}}=\mathrm{mean}_{p\in P_{\mathrm{arc}}}\mathrm{CV}_p
   \]
   Intermittent micro-discharges raise **relative variance** of mid/HF packets **before** mean \(E_{\mathrm{arc}}\) crosses a naive energy trip.
3. **Packet entropy** \(H_w=-\sum q_p\log_2 q_p\), \(H_{\mathrm{norm}}=H_w/\log_2 8\). Pink / multi-packet → high; single PWM ridge → low.
4. **Micro-burst duty** \(r_\mu\): fraction of horizon frames with \(E_{\mathrm{arc}}>2\mu_{\mathrm{arc}}\). Chatter band is elevated but \(<1\) (not continuous saturation).
5. **Packet excess kurtosis** \(\kappa_{\mathrm{pkt}}\) on \(P_{\mathrm{arc}}\) — spectral-kurtosis cousin; prefer over time kurtosis on zero-stuffed blanks.
6. **Horizon slopes** of \(I_{\mathrm{irr}}\) and \(E_{\mathrm{arc}}\) over \(H/2\) (replaces MACAPD’s 1-frame Δ for precursor narrative).
7. **Tonal EMI penalty (FFT/Goertzel augment only):** reuse MACAPD `rTonal`; optional packet concentration \(r_{\mathrm{pkt}}=\max q_p\). **Never** use FFT band energy alone as the precursor trip cue.
8. **maskingPenalty / optional coherence** — same honesty hooks as MACAPD.

### 6.5 Score fusion

Soft scalings (clip / triangle as needed):

\[
\begin{aligned}
S_{\mathrm{warp}}=&\;
w_I z_{\mathrm{irr}}+w_H z_H+w_\mu z_\mu+w_\kappa z_\kappa+w_{ps} z_{\mathrm{ps}}\\
&-w_t\,r_{\mathrm{tonal}}-w_{pk}\,r_{\mathrm{pkt}}-w_m\cdot\mathrm{maskingPenalty}
\end{aligned}
\]

**Default weights:** `wI=1.2`, `wH=0.6`, `wμ=0.8`, `wκ=0.5`, `wps=0.7`, `wt=1.0`, `wpk=0.4`, `wm=1.0`.

\[
S_{\mathrm{joint}}=\beta\,S_{\mathrm{macapd}}+(1-\beta)\,S_{\mathrm{warp}}
\quad\text{default }\beta=0.45
\]

**Policy:** precursor path keys on \(S_{\mathrm{warp}}\) + horizon; sustained arc-like path still needs MACAPD energy/kurtosis/burst. Freeze quiet-floor EWMAs while any Candidate* / Precursor* is armed (§5 / §7 P2).

### 6.6 State machine extension

| State | Enter when | Meaning |
|-------|------------|---------|
| `Inhibited` | dither / no blanking / AFE / keep-count too low / bad Fs | Features cleared |
| `Quiet` | \(S_{\mathrm{joint}} < t_{\mathrm{Lo}}\) and \(S_{\mathrm{warp}} < t_{\mathrm{pre}}\) | Armed quiet |
| **`PrecursorWatch`** | \(S_{\mathrm{warp}} \ge t_{\mathrm{pre}}\) for \(n_{\mathrm{pre}}\) frames **and** \(E_{\mathrm{arc}} < \gamma\cdot\theta_{\mathrm{energy}}\) | Irregularity rising **without** sustained energy trip |
| `CandidateLow` / `CandidateHigh` | MACAPD-compatible joint thresholds | Watch / strong research flag |
| `PrecursorConfirmed` (log tag) | `PrecursorWatch` held for \(T_H\) then MACAPD crosses `tLo` | Dataset hypothesis: “ahead of energy” |

**Defaults:** `t_pre=0.9`, `n_pre=5`, `γ=0.7`, MACAPD `tLo=1.0`, `tHi=2.5`, `nPersist=3`.

**Binding rule:** no state may call `releaseOutputInhibit`, fault trip, or OUTEN. Dual-MCU interrupter remains the only future trip owner.

### 6.7 Pseudocode (frame tick)

```text
function warpProcessFrame(cfg, st, i_blank, keepMask, macapdFeat):
  if cfg.dither or not cfg.blanking or cfg.afeFault or keepCount < keepMin:
    st.sense = Inhibited; return zeros

  C = liftingWptDb4(i_blank, keepMask, J=cfg.J)   // J default 3
  for p in 0..2^J-1:
    Ep[p] = meanSquare(C[p], keep)
  Earc = sum(Ep[p] for p in P_arc)
  Hnorm = packetEntropy(Ep) / log2(2^J)
  kpkt = meanExcessKurtosis(C[p] for p in P_arc)

  updateEwmaAndCv(st, Ep, Earc)          // freeze if armed
  Iirr = mean(CVp for p in P_arc)
  rμ = burstDutyHorizon(st, Earc, H)
  slopeI, slopeE = horizonSlopes(st, H)

  rTonal = macapdFeat.rTonal             // Goertzel augment only
  rpkt = max(Ep) / (sum(Ep)+eps)

  S_warp = fuseW(Iirr, Hnorm, rμ, kpkt, slopeI, slopeE)
           - wt*rTonal - wpk*rpkt - wm*cfg.maskingPenalty
  S_joint = β*macapdFeat.scoreRaw + (1-β)*S_warp

  return updateSense(st, S_warp, S_joint, Earc)  // + PrecursorWatch
```

### 6.8 WARP-specific edge cases

| # | Case | Expected WARP behaviour |
|---|------|-------------------------|
| 1 | Sustained tonal EMI @ k·fc | High `rTonal` / `r_pkt` → score down; no PrecursorWatch |
| 2 | Sparse micro-bursts, low mean E | High \(I_{\mathrm{irr}}\), mid \(r_μ\) → PrecursorWatch while \(E_{\mathrm{arc}}<\gamma\theta\) |
| 3 | Naive FFT energy would trip | WARP may already be PrecursorWatch on labelled data — **hypothesis**, not listing |
| 4 | Blank zeros into WPT | Prefer exclude; else inhibit / demote κpkt |
| 5 | Blanking too wide / low keep-count | Inhibited (blind) |
| 6 | Dither on | Inhibited |
| 7 | Series L masking | Raise `maskingPenalty`; do not claim visibility |
| 8 | Irradiance / ΔI_DC step | No DC ΔI feature; rely on persist + irregularity |
| 9 | Optimizer / RSD chatter | May mimic intermittency → aggressor inhibit window (unmodelled → honesty) |
| 10 | Burst phase vs decimated grid | 50% overlap; optional SWT lab |
| 11 | EWMA adapts into event | Freeze floors in Candidate*/Precursor* |
| 12 | Alias / weak AA | Inhibit / low-trust; M3+AA for serious SNR |
| 13 | Operator treats Precursor* as protection | Docs + UI: research only |

### 6.9 Later implementation map (feature-branch only)

| Artifact | Action |
|----------|--------|
| This §6 | Spec locked as research |
| `docs/FEATURE_AFDD_WARP_ALGORITHM.md` | Optional dedicated algorithm note (mirror MACAPD style) |
| `src/afdd_warp.h` + `test/test_afdd_warp/` | **Landed** header-only host math (Haar WPT J=3 interim; db4 still preferred); compile/default off on Teensy |
| `docs/FEATURE_AFDD_MACAPD_MEF_2026-08-30.md` | MEF E01–E20 fusion blueprint |
| Teensy image | EventLog only if ever wired; **never** OUTEN |
| Synthetic tests | Tone → no PrecursorWatch; sparse impulses → irregularity feature elevated |

### 6.10 WARP non-claims

- Not AFDD/AFCI; not UL/IEC compliance; not “detects arcs before they exist.”
- “Precursor ahead of FFT energy” is a **labelled-capture research hypothesis**, not a product claim.
- Wavelets are not proven superior on TEG hardware until M3 replay evidence exists.
- Host tests ≠ bench proof ≠ ISR/OUTEN proof.

---

## 7. Recommendations (research program only)

Track status in-place. **Done** = landed in host math and/or claim-safe docs on this feature branch. **Spec** = documented only. Firmware sense-path wiring stays compile/default off and never OUTEN.

| Priority | Recommendation | Why | Status (2026-08-30) |
|----------|----------------|-----|---------------------|
| P0 | Keep Teensy MACAPD / WARP / MEF **off OUTEN** forever in this image | Literature + Sandia: listed products still fail; bench instrument ≠ interrupter | **Done** (policy + headers) |
| P0 | Prefer **M3 16-bit** + proper AA for any serious capture | ENOB / aliasing dominate weak-arc / precursor SNR | **Done** (docs) |
| P1 | Wire **ditherActive** from real `CarrierDitherMode` when sense lands | Mutex is only as good as the flag | **Spec** (host flag exists; no Teensy wire) |
| P1 | Change kurtosis / moments to **mask-exclude**; add keep-count inhibit | Fixes blank-zero bias and “Quiet while blind” | **Done** (`afdd_macapd.h` + tests) |
| P1 | Widen tonal residual to **±Δ bins** or short STFT ridge | Matches docs; fights carrier drift | **Done** (±Δ Goertzel in host) |
| P2 | Freeze EWMA while Candidate* / Precursor*; longer slope windows | Stops floor chasing the event; real precursors | **Done** (freeze on Candidate*; WARP horizon slopes) |
| P2 | Put coherence (and optionally eL/eH ratios) into the score with weights | Parallel / CM discrimination | **Partial** (`wCoh` hook; eL/eH ratio soft) |
| P2 | Estimate or configure masking from known string L / C or self-test | Make “masking-aware” honest | **Spec** (`maskingPenalty` still operator/default) |
| P2 | Prototype **WARP** host math (`afdd_warp.h`) per §6; fuse with MACAPD | Wavelet-first precursor sibling; FFT penalty-only | **Done** (Haar interim + `test_afdd_warp`; db4 still preferred) |
| P2 | Land **MEF** catalog + Settings Evidence[20] blueprint | Multi-evidence enable matrix | **Docs done**; UI/config **Spec** |
| P3 | Offline AR residual / spectral kurtosis ablations vs WARP on labelled captures | Literature alternatives; not Teensy trip paths | Pending |
| P3 | Build a labelled capture library (quiet / dither / micro-burst / sustained / optimizer) | Sandia-style replay tuning without claiming listing | Pending |

**Next in priority order:** P1 dither wire (when HF sense lands) → P2 real masking estimate → P2 MEF `Afdd.Evidence[20]` config/UI (SchemaVersion 1, omit-if-default) → P2 db4 lifting replacing Haar → P3 capture library.

---

## 8. Non-claims (repeat for claim hygiene)

- This document is **not** evidence of UL 1699B / IEC AFDD compliance.
- Host tests of MACAPD (or future WARP) are **not** disconnected-bench proof and **not** ISR/OUTEN proof.
- CandidateHigh / PrecursorWatch / PrecursorConfirmed are **not** trip commands.
- Preferring ADS8860 / AD7380-class parts is a **lab instrumentation** recommendation, not a certified BOM for AFCI.
- Citing Sandia / IEEE / ACM work acknowledges **problem structure**, not that MACAPD or WARP reproduces their results on TEG hardware.
- “Detect ARC faults BEFORE they happen” in operator language must be qualified as **precursor irregularity research**, not clairvoyant protection.

---

## 9. Sources

### National lab / standards-context (primary)

1. J. Johnson et al., *Photovoltaic DC Arc Fault Detector Testing at Sandia National Laboratories* — [OSTI 1120327](https://www.osti.gov/servlets/purl/1120327).
2. Sandia work on PV module/line frequency response for robust detectors — [OSTI 1119759](https://www.osti.gov/servlets/purl/1119759).
3. J. Johnson et al., *Unwanted Tripping Survey of UL 1699B-Listed / Related PV Arc-Fault Products* (masking & nuisance classes; wavelet/richer suggestion) — [OSTI 1648697](https://www.osti.gov/servlets/purl/1648697).

### Theses

4. UBC thesis: digital filters for PV arc-fault detection — [DOI 10.14288/1.0445539](https://doi.org/10.14288/1.0445539).
5. Northeastern dissertation: pink-noise propagation / masking / wavelet & entropy survey in PV DC — [DOI 10.17760/d20449064](https://doi.org/10.17760/d20449064).

### Recent methods (illustrative)

6. HF statistical features (entropy / kurtosis / skew) — [DOI 10.1145/3650400.3650416](https://doi.org/10.1145/3650400.3650416).
7. Two-stage + spectral kurtosis (Solar Energy 2024) — [ScienceDirect abstract](https://www.sciencedirect.com/science/article/abs/pii/S0038092X24007795).
8. AR-model DC arc detection under PWM noise — [DOI 10.3390/app122010379](https://doi.org/10.3390/app122010379).
9. ICA–DTW PV series arc detection — [PMC12526851](https://pmc.ncbi.nlm.nih.gov/articles/PMC12526851/).
10. Kurtosis-based arc detection (distribution networks) — [DOI 10.3390/app12062777](https://doi.org/10.3390/app12062777).
11. Relative variability / impedance-guided bands vs inverter switching — [IEEE Xplore 9209997](https://ieeexplore.ieee.org/document/9209997).
12. Instrumentation false-alarm reduction (IEEE TIM 2025) — [DOI 10.1109/tim.2025.3635319](https://doi.org/10.1109/tim.2025.3635319).

### Wavelet / rich precursor methods

13. Yao et al., time-domain DWT hybrid series DC arc detection — [DOI 10.1109/tpel.2013.2273292](https://doi.org/10.1109/tpel.2013.2273292).
14. Xia et al., wavelet packet + SVM for PV series DC arc — [DOI 10.1002/tee.22797](https://doi.org/10.1002/tee.22797).
15. WT + multi-scale fuzzy entropy for DC arc — [IEEE PHM-Qingdao 2019](https://doi.org/10.1109/phm-qingdao46334.2019.8942846).
16. Multiple wavelet + fractional wavelet energy entropy — [DOI 10.1063/5.0186731](https://doi.org/10.1063/5.0186731).
17. WPT + residual CNN + SVM series arc detection — [DOI 10.1063/5.0205503](https://doi.org/10.1063/5.0205503).
18. Wang & Balog, arc fault / flash detection with WT + SVM (PVSC) — [DOI 10.1109/PVSC.2016.7750271](https://doi.org/10.1109/PVSC.2016.7750271).
19. Wang et al., FFT vs wavelet decomposition on synthesized arc data (PVSC) — [DOI 10.1109/PVSC.2014.6925625](https://doi.org/10.1109/PVSC.2014.6925625).

### Field / installer context (non-peer-reviewed checklist)

20. Example discussion of PV AFCI false trips (RSD / capacitance / EMI) — [Anern overview](https://www.anernstore.com/blogs/diy-solar-guides/false-arc-trips-pv-arrays).

### In-repo

21. [`FEATURE_AFDD_RESEARCH_2026-08-30.md`](FEATURE_AFDD_RESEARCH_2026-08-30.md), [`FEATURE_AFDD_MACAPD_ALGORITHM.md`](FEATURE_AFDD_MACAPD_ALGORITHM.md), [`FEATURE_AFDD_MACAPD_MEF_2026-08-30.md`](FEATURE_AFDD_MACAPD_MEF_2026-08-30.md), `src/afdd_macapd.h`, `src/afdd_warp.h`, `test/test_afdd_macapd/`, `test/test_afdd_warp/`.