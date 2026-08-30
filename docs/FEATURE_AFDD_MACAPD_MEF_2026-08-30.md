# MACAPD multi-evidence fusion (MEF) — 20 research channels

**Status:** research / claim-safe. **NO-SHIP.**  
**Not a listed AFDD / AFCI.** MEF is a **lab fusion blueprint** for enabling/disabling evidence channels behind `Afdd.Evidence[20]`. Host Unity and EventLog tags are **not** ISR/OUTEN proof and are **not** UL 1699B / IEC evidence. Teensy MACAPD / WARP / MEF scores must **never** drive OUTEN or `releaseOutputInhibit`.

| Companion | Role |
|-----------|------|
| [`FEATURE_AFDD_RESEARCH_2026-08-30.md`](FEATURE_AFDD_RESEARCH_2026-08-30.md) | Platform architecture (RT1060, M0–M3, OUTEN policy) |
| [`FEATURE_AFDD_MACAPD_ALGORITHM.md`](FEATURE_AFDD_MACAPD_ALGORITHM.md) | Stage-by-stage MACAPD + host API |
| [`FEATURE_AFDD_MACAPD_RESEARCH_2026-08-30.md`](FEATURE_AFDD_MACAPD_RESEARCH_2026-08-30.md) | Literature, edge cases, WARP §6, recommendations §7 |
| `src/afdd_macapd.h` | Host-testable MACAPD math (never OUTEN) |
| `src/afdd_warp.h` | Host-testable WARP precursor math (Haar WPT interim; never OUTEN) |

---

## 1. Why MEF

Single-feature energy trips fail Sandia-class masking and monotone-tone nuisance suites. Literature converges on **multi-evidence** (band energy + impulsiveness + burst + precursor irregularity − tonal − masking), with **persistence** and **inhibit windows** (dither / blanking / AFE / keep-count).

MEF makes that explicit as **exactly twenty** channels operators can enable in Settings for research captures. Default lean ON set stays Teensy-viable; heavy channels stay default OFF (offline / dual-channel / high RAM).

---

## 2. Fusion contract

```text
inhibit ← dither || !blanking || afeFault || keepCount < keepMin
if inhibit → sense=Inhibited; clear features; return

for each enabled evidence channel Ei:
  zi = softScale(Ei)
S = Σ w_i·zi  −  w_tonal·rTonal  −  w_mask·maskingPenalty  [− optional other penalties]
freeze EWMA floors while Candidate* / Precursor*
persist → Quiet / PrecursorWatch / CandidateLow / CandidateHigh / PrecursorConfirmed (log only)
```

| Rule | Value |
|------|--------|
| SchemaVersion | **1** (no bump) |
| Config | `Afdd.Enabled` + `Afdd.Evidence[20]` bools; **omit-if-default** in `configToJson` |
| UI | Settings checkbox grid (20); research hint, not protection marketing |
| Output | EventLog / status research fields only |
| OUTEN | **Forbidden** for all MEF scores |
| Host tests | Six-suite gate unchanged; `test_afdd_macapd` / `test_afdd_warp` reported separately |

---

## 3. Channel catalog (E01–E20)

| ID | Short name | Class | Default | Cost | Host? | Teensy? | Role | Notes |
|----|------------|-------|---------|------|-------|---------|------|-------|
| **E01** | Goertzel mid-band | established | **ON** | low | yes | yes | evidence | MACAPD `eM` (~20–50 kHz) |
| **E02** | STFT ridge map | established | OFF | high | yes | marginal | evidence | Short-time ridge vs blank; lab |
| **E03** | WPT packet energy | established | **ON** | med | yes | yes | evidence | WARP `eArc` / Haar→db4 |
| **E04** | Blanking / keep-count | established | **ON** | low | yes | yes | veto | Inhibit if keep &lt; keepMin |
| **E05** | Persistence | established | **ON** | low | yes | yes | evidence | `nPersist` / `nPre` frames |
| **E06** | ΔI_DC step veto | established | OFF | low | yes | yes | veto | Default off — irradiance nuisance |
| **E07** | HF RMS variance | established | **ON** | low | yes | yes | evidence | Short-window RMS var on kept |
| **E08** | Tonal residual ±Δ | established | **ON** | low | yes | yes | **penalty** | k·fc ±Δ Goertzel / packet conc. |
| **E09** | Time kurtosis (mask-excl.) | established | **ON** | low | yes | yes | evidence | Kept samples only |
| **E10** | AR residual energy | cutting-edge | OFF | med | yes | marginal | evidence | PWM-stationary vs arc random |
| **E11** | Spectral / packet kurtosis | cutting-edge | **ON** | med | yes | yes | evidence | WARP `kPkt` |
| **E12** | ICA–DTW (I+V) | cutting-edge | OFF | high | offline | no | evidence | Dual-channel heavy |
| **E13** | Entropy / Tsallis / Hurst | cutting-edge | OFF | med | yes | marginal | evidence | Disorder cues |
| **E14** | Multi-scale fuzzy entropy | cutting-edge | OFF | high | offline | no | evidence | WT+MFE literature |
| **E15** | Fractional wavelet entropy | cutting-edge | OFF | high | offline | no | evidence | Lab export |
| **E16** | Z-guided band centres | cutting-edge | OFF | med | yes | marginal | evidence | Impedance-informed bands |
| **E17** | Two-stage precursor | cutting-edge | **ON** | med | yes | yes | evidence | WARP PrecursorWatch |
| **E18** | WPT–SVM export | cutting-edge | OFF | high | offline | no | evidence | Offline classifier only |
| **E19** | I/V coherence | established | OFF | low | yes | yes* | evidence | Needs HF V; `wCoh` |
| **E20** | Masking + blank residual | established | **ON** | low | yes | yes | **penalty** | `maskingPenalty` honesty |

\*E19 Teensy-viable only with dual HF (prefer AD7380 M3).

**Lean default ON (10):** E01, E03, E04, E05, E07, E08, E09, E11, E17, E20.  
**Default OFF (10):** E02, E06, E10, E12, E13, E14, E15, E16, E18, E19.

---

## 4. Mapping to existing host math

| Channel | Today |
|---------|--------|
| E01, E08, E09, E05, E20 | `src/afdd_macapd.h` (mask-exclude kurtosis; tonal ±Δ; freeze EWMA; keep-count inhibit; coherence hook for E19) |
| E03, E11, E17 | `src/afdd_warp.h` (Haar WPT interim; packet kurtosis; PrecursorWatch) |
| E04 | Shared blank mask + keepMin |
| E07 | Partial via burst / RMS proxies — full HF RMS var still soft |
| E02, E06, E10, E12–E16, E18 | Spec only / offline |

---

## 5. Web UI / config sketch (not product)

```text
Settings → AFDD research (claim-safe banner)
  [ ] Afdd.Enabled
  Evidence grid: E01…E20 checkboxes (defaults per §3)
  Read-only: sense enum, S_joint, keepCount (EventLog)
```

- Empty / default Evidence array omitted from export (`configToJson` omit-if-default).
- Import must not invent trip authority.
- No MQTT “arc trip” entity.

---

## 6. Recommendation program tie-in (§7)

MEF is the **P2 fusion umbrella**. Host landings that feed MEF:

| Pri | Item | MEF link | Status (2026-08-30) |
|-----|------|----------|---------------------|
| P0 | Never OUTEN | all | **Done** (policy) |
| P0 | Prefer M3 16-bit | SNR for all Ei | **Done** (docs) |
| P1 | ditherActive wire | E04 inhibit | Spec (firmware sense path) |
| P1 | Mask-exclude + keepMin | E04, E09 | **Host done** |
| P1 | Tonal ±Δ | E08 | **Host done** |
| P2 | Freeze EWMA | fusion | **Host done** |
| P2 | Coherence into score | E19 | **Host hook** (`wCoh`; default off weight small) |
| P2 | Real masking estimate | E20 | Spec |
| P2 | WARP host math | E03/E11/E17 | **Host skeleton** (Haar) |
| P2 | MEF UI Evidence[20] | this doc | Spec |
| P3 | AR / SK ablations | E10/E11 | Pending labelled library |

---

## 7. Non-claims

- MEF is **not** UL 1699B / IEC AFDD compliance and **not** an arc-protection product.
- Enabling channels in Settings does **not** create a trip path.
- Host tests of MACAPD/WARP/MEF are **not** disconnected-bench proof and **not** ISR/OUTEN proof.
- Default-ON lean set is a **research convenience**, not a certified detector profile.
- Offline SVM / ICA / fractional entropy channels are **export analytics**, not Teensy interrupters.
