# MACAPD / WARP Multi-Evidence Fusion (MEF) — 20 channels E01–E20

**Status:** research / claim-safe. **NO-SHIP.**  
**Not a listed AFDD / AFCI.** This note specifies a **Multi-Evidence Fusion (MEF)** contract for MACAPD and WARP research on the Teensy 4.1 TEG image. It invents **no** UL 1699B, IEC 62606, or “arc protection” product claims. Host Unity tests of `src/afdd_macapd.h` / `src/afdd_warp.h` are **not** ISR/OUTEN proof and are **not** listing evidence. MEF scores append **EventLog / research state only** — they **never** drive OUTEN, `releaseOutputInhibit`, or fault-trip paths.

| Companion | Role |
|-----------|------|
| [`FEATURE_AFDD_RESEARCH_2026-08-30.md`](FEATURE_AFDD_RESEARCH_2026-08-30.md) | Platform architecture (RT1060, M0–M3, OUTEN policy) |
| [`FEATURE_AFDD_MACAPD_ALGORITHM.md`](FEATURE_AFDD_MACAPD_ALGORITHM.md) | Stage-by-stage MACAPD algorithm + host API |
| [`FEATURE_AFDD_MACAPD_RESEARCH_2026-08-30.md`](FEATURE_AFDD_MACAPD_RESEARCH_2026-08-30.md) | Literature, edge cases, WARP §6, recommendations §7 |
| `src/afdd_macapd.h` | Host-testable MACAPD math (never drives OUTEN) |
| `src/afdd_warp.h` | Host-testable WARP precursor math (Haar WPT interim; never drives OUTEN) |
| `plan/feature-afdd-macapd-recs-1.md` | Recommendation walkthrough plan |

---

## 1. Purpose

MACAPD’s Stage B already speaks of **multi-evidence** features (band energy, kurtosis, burst, slopes, tonal residual, masking). Literature and Sandia unwanted-trip surveys push further: **fuse several independent cues**, persist them, and treat inverter EMI / masking as **penalties or vetoes** — not as a single mid-band energy trip.

**MEF** is the named research contract for that fusion:

1. Exactly **20 evidence channels** `E01`–`E20`, each independently enableable.
2. A **lean default-ON** set that a host-testable / lab-M7 path can run without heavy offline ML.
3. A **default-OFF heavy** set for capture replay, dual-channel labs, and ablation studies.
4. One **fusion equation** and one **inhibit / freeze policy** shared with MACAPD / WARP state machines.
5. Operator config that stays **SchemaVersion 1** with omit-if-default serde — research flags, not a product AFDD panel.

MEF does **not** authorize Teensy protection language. Dual-MCU interrupter ownership remains unchanged from the platform research note.

---

## 2. Recommendation status (ties to research §7)

This MEF note elaborates **P2** fusion from [`FEATURE_AFDD_MACAPD_RESEARCH_2026-08-30.md`](FEATURE_AFDD_MACAPD_RESEARCH_2026-08-30.md) §7, plus the multi-evidence trend in §2.4.

| Research §7 | MEF mapping | Status (2026-08-30) |
|-------------|-------------|---------------------|
| **P0** — never OUTEN; prefer M3 16-bit | Binding for every channel; MEF outputs sense/log only | **Hard invariant** |
| **P1** — dither flag, mask-exclude kurtosis, keep-count inhibit, ±Δ tonal | **E04**, **E08**, **E09**, inhibit policy | **Host done** (mask-exclude / keepMin / ±Δ); dither **wire** still Spec |
| **P2** — freeze EWMA; coherence; masking honesty; WARP host; MEF catalog | Freeze policy; **E19** opt-in; **E20**; **E03**/**E11**/**E17** | **Host partial** (freeze, `wCoh`, WARP Haar); masking estimate + Evidence[20] UI **Spec** |
| **P3** — offline AR / SK / labelled library | **E02**, **E10**, **E12**–**E16**, **E18** default OFF | Pending |

---

## 3. Channel catalog (exactly E01–E20)

**Legend**

- **Method class:** `established` = widely used in PV/EMI / DSP literature or already in MACAPD spine; `cutting-edge` = recent / heavy / multi-channel research methods.
- **Default:** lean ON vs heavy OFF (operator may toggle; serde omit-if-default).
- **Cost:** `low` / `med` / `high` approximate MACCs / memory per frame at N≈512, Fs≈250 kHz.
- **Host-testable?** Synthetic float frames in native Unity without Teensy peripherals.
- **Teensy-viable?** Feasible on Cortex-M7 **offline / duty-cycled lab path** in DTCM without claiming WCET inside FAULT/OUTEN IRQ. `no` ⇒ offline PC / dual-MCU export only.
- **Fusion role:** `evidence` (adds to score), `penalty` (subtracts), `veto` (forces Inhibited or hard zero contribution and may clear armed states).

### 3.1 Master table

| ID | Short name | Method class | Default | Cost | Host-testable? | Teensy-viable? | Fusion role |
|----|------------|--------------|---------|------|----------------|----------------|-------------|
| **E01** | Goertzel mid-band | established | **ON** | low | yes | yes | evidence |
| **E02** | STFT overlap bank | established | OFF | high | yes | lab-only | evidence |
| **E03** | WPT packet energy | established | **ON** | med | yes | lab-only | evidence |
| **E04** | Blanking / keep-count | established | **ON** | low | yes | yes | veto |
| **E05** | Persistence ride-through | established | **ON** | low | yes | yes | evidence |
| **E06** | ΔI_DC step veto | established | OFF | low | yes | yes | veto |
| **E07** | HF RMS variance | established | **ON** | low | yes | yes | evidence |
| **E08** | Tonal EMI penalty | established | **ON** | low | yes | yes | penalty |
| **E09** | Time kurtosis (mask-exclude) | established | **ON** | low | yes | yes | evidence |
| **E10** | AR residual randomness | cutting-edge | OFF | med | yes | lab-only | evidence |
| **E11** | Spectral / packet kurtosis | established | **ON** | med | yes | lab-only | evidence |
| **E12** | ICA–DTW non-Gaussianity | cutting-edge | OFF | high | partial | no | evidence |
| **E13** | Entropy / Tsallis / Hurst | cutting-edge | OFF | med | yes | lab-only | evidence |
| **E14** | Multi-scale fuzzy entropy (MFE) | cutting-edge | OFF | high | yes | no | evidence |
| **E15** | Fractional wavelet entropy | cutting-edge | OFF | high | yes | no | evidence |
| **E16** | Z-guided / impedance bands | cutting-edge | OFF | med | partial | lab-only | evidence |
| **E17** | Two-stage precursor | established | **ON** | med | yes | lab-only | evidence |
| **E18** | WPT–SVM export offline | cutting-edge | OFF | high | no (model) | no | evidence |
| **E19** | I/V coherence | established | OFF | low | yes | yes | evidence |
| **E20** | Masking + blank residual | established | **ON** | low | yes | yes | penalty |

### 3.2 Channel detail

#### E01 — Goertzel mid-band *(default ON)*

- **Description:** Single-bin / few-bin Goertzel powers for MACAPD `eL` / `eM` / `eH` (default centres in ~5–20 / 20–50 / 50–100 kHz). Primary **sustained energy** cue; mid-band `eM` carries the default evidence weight.
- **Fusion role:** evidence (`w[E01] · z(eM)` and optional soft `eL`/`eH` ratios when enabled).
- **Ties:** MACAPD F1; algorithm Stage B; research Stage B. **Host:** `afddMacapdBandEnergy`.

#### E02 — STFT overlap bank *(default OFF)*

- **Description:** Short-time FFT or portable radix-2 bank with ~50% hop for spectrogram energy, ridge tracking, and richer tonal neighbourhoods than single-bin Goertzel.
- **Fusion role:** evidence (may replace or augment E01 in ablations; never alone as a trip cue).
- **Notes:** Production stay-off remains `TEG_ENABLE_CMSIS_FFT` / global `-O3`. Host may use portable FFT. High RAM/MACC — default OFF.

#### E03 — WPT packet energy *(default ON)*

- **Description:** Lifting / Haar WPT (WARP: db4 preferred research bank; interim Haar J=3 in `afdd_warp.h`) packet energies \(E_p\), especially \(E_{\mathrm{arc}}\) over arc-interest packets.
- **Fusion role:** evidence (WARP energy / irregularity inputs; MACAPD joint path via `S_warp`).
- **Ties:** research §6.3–§6.4. **Host:** `afddWarpHaarWpt3` / `eArc`.

#### E04 — Blanking / keep-count *(default ON)*

- **Description:** FlexPWM-aware keep/blank mask; **inhibit** when blanking unavailable, dither active, AFE fault, or `keepCount < keepMin` (blind / over-blanked frames).
- **Fusion role:** **veto** → `Inhibited`; features cleared; no Candidate*/Precursor* advance.
- **Ties:** research §3.2, §4 #5–#7, §7 P1; MACAPD Stage A. **Host:** `keepMin` inhibit landed; dither **wire** from `CarrierDitherMode` still Spec.

#### E05 — Persistence ride-through *(default ON)*

- **Description:** Frame-count (research) persistence before `CandidateHigh` / `PrecursorWatch` confirm tags — rejects single-frame EMI spikes (Sandia-style unwanted-trip mindset).
- **Fusion role:** evidence **gating** (does not add energy; required enabler for HIGH / precursor confirm logging).
- **Notes:** Frame count ≠ joule budget; dual-MCU concern if ever productized elsewhere.

#### E06 — ΔI_DC step veto *(default OFF)*

- **Description:** Optional veto when a large low-frequency / DC current step is detected (irradiance, string reconnect). Literature: current-step detectors nuisance-trip; MACAPD prefers HF statistics.
- **Fusion role:** **veto** (when enabled): freeze or inhibit HF Candidate* briefly on large ΔI_DC — **not** an arc evidence cue.
- **Default OFF** so the lean bank does **not** key on ΔI_DC (research §4 #8; §2.2 irradiance class).

#### E07 — HF RMS variance *(default ON)*

- **Description:** Short-window variance / RMS of kept HF samples (or packet RMS CV proxy). Cheap intermittency / chatter cue related to WARP \(I_{\mathrm{irr}}\) / micro-burst narratives.
- **Fusion role:** evidence.
- **Host:** partial via burst / WARP irregularity proxies; dedicated HF RMS var still soft.

#### E08 — Tonal EMI penalty *(default ON)*

- **Description:** `rTonal` = energy near `k·fc` (±Δ bins) over broadband HF energy. High ⇒ inverter / dither residue.
- **Fusion role:** **penalty** (`− wTonal · rTonal`). **Never** an early-warning trip cue (WARP: FFT/Goertzel tonal-only).
- **Ties:** MACAPD F2; WARP §6.4 item 7. **Host:** ±Δ Goertzel landed.

#### E09 — Time kurtosis (mask-exclude) *(default ON)*

- **Description:** Excess kurtosis of **kept** samples only (exclude blanks from moments — not zero-stuff). Impulsive arcs raise leptokurtosis vs nearer-Gaussian EMI.
- **Fusion role:** evidence.
- **Ties:** research §5 gap #1; §7 P1; MACAPD F3. **Host:** `afddMacapdExcessKurtosisMasked` landed.

#### E10 — AR residual randomness *(default OFF)*

- **Description:** Autoregressive model residual energy / whiteness tests — arcs ≈ non-stationary random; PWM ≈ more structured (Appl. Sci. 2022-class theme).
- **Fusion role:** evidence (ablation vs tonal penalty).
- **Cost / viability:** med; lab-only on M7; default OFF (research §7 P3).

#### E11 — Spectral / packet kurtosis *(default ON)*

- **Description:** Kurtosis on bandpass / WPT arc packets (`κpkt`) — preferred over raw time kurtosis when blanks would bias moments.
- **Fusion role:** evidence.
- **Ties:** Solar Energy 2024 two-stage / spectral kurtosis theme; WARP §6.4 item 5. **Host:** WARP `kPkt`.

#### E12 — ICA–DTW non-Gaussianity *(default OFF)*

- **Description:** Independent component + dynamic time warping style global non-Gaussianity (multi-channel literature). Needs dual/multi streams and heavy compute.
- **Fusion role:** evidence (offline / multi-capture).
- **Teensy-viable?** **no**. Host-testable only with canned multi-channel fixtures (partial).

#### E13 — Entropy / Tsallis / Hurst *(default OFF)*

- **Description:** Shannon / Tsallis entropy and Hurst-style long-memory on kept HF or packet probabilities — disorder cues without full FFT.
- **Fusion role:** evidence.
- **Default OFF** (EMI-vulnerable without blanking; research P3 ablation). WARP packet entropy is a lean cousin when E03/E17 run.

#### E14 — Multi-scale fuzzy entropy (MFE) *(default OFF)*

- **Description:** WT + multi-scale fuzzy entropy style richness (PHM / DC-arc papers).
- **Fusion role:** evidence; offline / host heavy.
- **Teensy-viable?** **no** for real-time lean path.

#### E15 — Fractional wavelet entropy *(default OFF)*

- **Description:** Fractional-order wavelet energy entropy features (cutting-edge wavelet papers).
- **Fusion role:** evidence; offline.
- **Teensy-viable?** **no**.

#### E16 — Z-guided / impedance bands *(default OFF)*

- **Description:** Choose analysis band centres from plant small-signal / impedance estimates to **avoid** switching ridges (IEEE Access-class band selection).
- **Fusion role:** evidence (retunes E01/E03 centres; not a raw energy addend alone).
- **Host-testable?** partial (needs configured Z or recorded band map).

#### E17 — Two-stage precursor *(default ON)*

- **Description:** WARP-style **pre-energy** stage: irregularity / micro-burst / slopes over 0.5–2 s horizon → `PrecursorWatch` while mean arc-band energy stays below \(\gamma\cdot\theta_{\mathrm{energy}}\); sustained confirm still needs MACAPD-like energy/kurtosis/burst.
- **Fusion role:** evidence (drives `S_warp` / joint precursor path).
- **Ties:** research §6.5–§6.6; §2.4 two-stage pre-arc then verify. **Host:** `AfddWarpPrecursorWatch` in `afdd_warp.h`.

#### E18 — WPT–SVM export offline *(default OFF)*

- **Description:** Export WPT / MEF feature vectors for offline SVM / RCNN+SVM class training (literature classifiers). **No** on-box inference requirement in this image.
- **Fusion role:** evidence only in **offline** scoring pipelines; Teensy path may emit feature dumps while inhibited — never OUTEN.
- **Host-testable?** model training **no** inside firmware Unity; fixture export format yes.

#### E19 — I/V coherence *(default OFF)*

- **Description:** `|corr(i,v)|` on kept dual-channel samples — CM vs series-arc discrimination when HF V exists.
- **Fusion role:** evidence (`wCoh · z(coherence)` when enabled).
- **Default OFF** in UI until dual AFE is real; host score hook `wCoh` exists (default 0.25 in MACAPD config — treat as research weight, UI default still OFF per this table). Prefer AD7380 M3 for dual I+V.

#### E20 — Masking + blank residual *(default ON)*

- **Description:** Sandia-class honesty: operator / probe `maskingPenalty` (series L, C-to-ground) plus residual trust from blanking quality (phase slip / duty extremes logged as low-trust).
- **Fusion role:** **penalty** (`− wMask · maskingPenalty` and optional blank-trust demotion). High masking ⇒ lower score / declare blind — **do not claim coverage**.
- **Ties:** MACAPD F7; research §2.2 masking classes; §7 P2. **Host:** penalty hook; **real** L/C estimate still Spec.

---

## 4. Default enable sets

### 4.1 Lean default ON (10 channels)

```text
E01, E03, E04, E05, E07, E08, E09, E11, E17, E20
```

Intent: Goertzel + WPT energy, blanking veto, persistence, HF variance, tonal penalty, mask-exclude time kurtosis, packet/spectral kurtosis, two-stage precursor, masking honesty. Fits host-testable MACAPD/WARP spine and a duty-cycled lab M7 path without offline ML.

### 4.2 Heavy default OFF (10 channels)

```text
E02, E06, E10, E12, E13, E14, E15, E16, E18, E19
```

Intent: STFT, ΔI_DC veto, AR residual, ICA–DTW, entropy family, MFE, fractional wavelet entropy, Z-guided bands, WPT–SVM export, I/V coherence — ablation, dual-channel, or PC replay only until evidence says otherwise.

---

## 5. Fusion contract

### 5.1 Score (enabled-weighted)

Let \(\mathcal{E}\) be the set of **enabled** channels with role `evidence`, \(\mathcal{P}\) with role `penalty`. Veto channels do not add; they force inhibit (or a documented soft veto window for E06 when enabled).

\[
\begin{aligned}
S_{\mathrm{mef}}
&=
\sum_{e\in\mathcal{E}} w_e\,z_e
\;-\;
\sum_{p\in\mathcal{P}} w_p\,z_p
\\[0.5em]
&\quad\text{with lean penalties typically } p\in\{\mathrm{E08},\mathrm{E20}\}
\end{aligned}
\]

In operator language:

```text
S_mef ≈ enabled-weighted sum(evidence z-scores)
 − tonal (E08)
 − masking (E20)
```

Soft scalings `z_e` reuse MACAPD/WARP proxies (EWMA floors, clipped ratios). WARP joint path remains:

\[
S_{\mathrm{joint}} = \beta\,S_{\mathrm{macapd}} + (1-\beta)\,S_{\mathrm{warp}}
\]

when E03/E11/E17 are contributing; MEF does not replace that β-fuse — it **names which channels may feed** each side.

**Default weight sketch (research, not calibrated):** evidence weights ~0.4–1.2 by cost class; `wTonal≈1.0`, `wMask≈1.0`. Disabled channels contribute **0** (no phantom defaults inside the sum).

### 5.2 EWMA freeze

Quiet-floor EWMAs (band, irregularity, burst baselines) **freeze** while any of:

- `CandidateLow` / `CandidateHigh`
- `PrecursorWatch` / `PrecursorConfirmed` (log tag)

is armed. Prevents floor chasing the event (research §4 #17, §7 **P2**, WARP §6.5). **Host:** MACAPD `freezeEwmaOnCandidate`; WARP `freezeEwmaOnArm`.

### 5.3 Inhibit (hard)

Force `Inhibited`, clear features / scores for the frame, and do not advance persistence when any of:

- carrier **dither** active
- **blanking** unavailable or E04 keep-count below `keepMin`
- **AFE** fault
- (optional, if E06 enabled) active ΔI_DC veto window

Same binding rule as MACAPD/WARP: inhibit is a **sense** outcome, not a trip.

### 5.4 State outputs (log only)

| Sense / tag | Meaning |
|-------------|---------|
| `Inhibited` | Mutex / blind / AFE / keep-count |
| `Quiet` | Armed, below thresholds |
| `PrecursorWatch` | E17 path: irregularity without sustained energy trip |
| `CandidateLow` / `CandidateHigh` | MEF/MACAPD joint thresholds + E05 persistence |
| `PrecursorConfirmed` | Log tag only after precursor held then energy rises |

**Never** call `releaseOutputInhibit`, mask/trip faults for “arc,” or drive OUTEN from these states.

---

## 6. Config, serde, and Settings UI

| Item | Contract |
|------|----------|
| **SchemaVersion** | Stays **1**. MEF does not bump schema. |
| **Master enable** | `Afdd.Enabled` (bool, default false). Compile/default off on Teensy image. |
| **Per-channel** | `Afdd.Evidence[20]` bools — index `0`→E01 … `19`→E20. |
| **Defaults** | Lean ON / heavy OFF per §4. |
| **`configToJson`** | **Omit-if-default:** if `Afdd.Enabled` is false and every `Evidence[i]` matches §4 defaults, omit the `Afdd` object (or omit unchanged fields per existing omit style). Reads + validate clamps still accept full objects. |
| **Settings UI** | Checkbox grid: master `Afdd.Enabled` + 20 evidence toggles (E01–E20 short names). Copy must say **research / EventLog only — not protection**. |
| **Telemetry** | EventLog / research ring for sense transitions and optional per-channel contributions; **no** MQTT/HA “arc fault” product entities implied. |
| **Secrets / import** | No new secret fields. Preset/import still follows PWM IRQ inhibit rules unrelated to MEF. |

---

## 7. Data flow (research path)

```text
HF AFE → ADC (M3 preferred) → float frame i[n] (± v[n])
 │
FlexPWM edges → blank mask (E04)
 │
dither / AFE / keep-count ──veto──► Inhibited
 │
 ▼
 per-enabled channel extractors (E01–E20)
 │
 ▼
 S_mef = Σ w·z(evidence) − tonal − masking
 EWMA freeze if Candidate* / Precursor*
 │
 ▼
 persistence (E05) → sense enum / log tags
 │
 ▼
 EventLog / offline export ONLY
 (never OUTEN / releaseOutputInhibit)
```

Host path: `afdd_macapd.h` / `afdd_warp.h` (+ future `afdd_mef.h` if split) under native tests. Teensy path: compile-flagged, default off, EventLog only.

---

## 8. Mapping to existing host math

| Channel | Today |
|---------|--------|
| E01, E08, E09, E05, E20 | `src/afdd_macapd.h` (mask-exclude kurtosis; tonal ±Δ; freeze EWMA; keep-count inhibit; coherence hook for E19) |
| E03, E11, E17 | `src/afdd_warp.h` (Haar WPT interim; packet kurtosis; PrecursorWatch) |
| E04 | Shared blank mask + keepMin |
| E07 | Partial via burst / WARP irregularity — full HF RMS var still soft |
| E02, E06, E10, E12–E16, E18 | Spec only / offline |

---

## 9. Implementation map (feature-branch only)

| Artifact | Action |
|----------|--------|
| This file | MEF channel + fusion contract |
| `src/afdd_macapd.h` | Lean evidence partial (E01, E04, E05, E08, E09, E20 hooks); P1 mask-exclude / ±Δ / keep-count **landed** |
| `src/afdd_warp.h` | E03, E11, E17 precursor bank (**Haar interim**; db4 preferred) |
| Future `src/afdd_mef.h` (optional) | Thin enable-mask + weighted sum over feature structs; host-testable; never OUTEN |
| `web/index.html` Settings | Checkbox grid when Afdd UI is prototyped; research wording — **Spec** |
| `config` serde | `Afdd.Enabled` + `Afdd.Evidence[20]` omit-if-default; SchemaVersion 1 — **Spec** |
| Native tests | `test_afdd_macapd`, `test_afdd_warp` smoke; report separately from six-suite; not bench proof |

Do **not** land operator “protection” claims on `main` without feature-branch PR discipline for claim-safe AFDD docs.

---

## 10. Non-claims

- This document is **not** UL 1699B / IEC 62606 / AFDD / AFCI compliance evidence.
- MEF, MACAPD, and WARP scores are **not** trip commands and must **never** drive Teensy OUTEN.
- Host Unity tests are **not** disconnected-bench proof and **not** ISR/OUTEN proof.
- Default-ON channels are a **research lean bank**, not a certified feature set.
- Default-OFF channels are **not** “coming soon protection”; they are ablation / offline tools.
- “Precursor” means **irregularity before sustained energy proxies**, not clairvoyant arc prevention.
- Citing Sandia / IEEE / ACM / wavelet literature acknowledges **problem structure**, not reproduction of their results on TEG hardware.
- Preferring external 16-bit M3 ADCs is a **lab instrumentation** recommendation, not a listed BOM.
- Settings checkboxes and EventLog lines are **operator research orientation**, not product AFDD UI.
- No energy-dashboard, grid-tie, or HA arc-entity product claims are authorized by MEF.

---

## 11. Sources (pointer)

Primary synthesis and citations live in [`FEATURE_AFDD_MACAPD_RESEARCH_2026-08-30.md`](FEATURE_AFDD_MACAPD_RESEARCH_2026-08-30.md) §2 and §9 (Sandia DETL / unwanted-trip survey, theses, HF statistics, spectral kurtosis, AR, ICA–DTW, wavelet / MFE / fractional entropy, WPT+SVM, impedance-guided bands, TIM false-alarm fusion theme). This MEF note does not re-litigate those DOIs; it **assigns them to numbered channels and a fusion policy** under the same claim-safe rules.
