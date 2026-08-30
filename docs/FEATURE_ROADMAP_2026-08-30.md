# Multi-role feature roadmap (2026-08-30)

Research and architecture note for treating TEG as a **bench PWM instrument** that can grow several product *lenses* — inverter, custom waveform generator, instrumentation, reference lock, MPPT research, and AFDD-class arc research — without pretending those lenses are certified products.

**Status:** ideation / claim-safe documentation only. **NO-SHIP** until `docs/BENCH_CHECKS.md` disconnected checklist passes. Host Unity is not ISR/OUTEN proof. This document invents **no** UL 1699B, IEC AFDD, grid-tie, or HA energy-dashboard product claims.

Companion sources:

- [PRODUCT_READINESS.md](PRODUCT_READINESS.md) — firmware constraints that kill naïve product stories
- [BENCH_CHECKS.md](BENCH_CHECKS.md) — disconnected evidence gate
- [REVIEW_FIXES_2026-08-28.md](REVIEW_FIXES_2026-08-28.md) … `-6.md` — completed host-safe review slices (historical)
- Canvas (IDE): `feature-roadmap-2026-08-30.canvas.tsx`
- Operator README: modes, custom waveforms, capture, spectrum, PLL, MPPT, MQTT — all **bench** language

Do **not** create `plan/refactor-adversarial-fixes-7.md`. Remaining adversarial items are bench evidence or stay-offs.

---

## 1. Verdict

One firmware core remains a **Teensy 4.1 PWM bench instrument**. “Inverter”, “waveform generator”, “AFDD”, and so on are **roles / tracks**, not SKUs and not certification targets on this MCU alone.

| Role | Code | Near-term value | Hard gate |
|------|------|-----------------|-----------|
| Bench PWM instrument (core) | R-CORE | Already the product of record | `BENCH_CHECKS` disconnected campaign |
| Inverter / SPWM / DPWM | R-INV | Schemes + regulation already in firmware | Live polarity / fault / thermal / WCET evidence |
| Arbitrary waveform generator | R-WGEN | Strong differentiator for lab use | Same OUTEN / inhibit contracts; document dither vs sense |
| Instrumentation (scope / spectrum / meter) | R-INST | Already deep; keep claim-safe | Capture ≠ HF arc band |
| Reference PLL | R-REF | Bench reference lock only | Never market as grid-tie |
| MPPT research | R-MPPT | String-level tracker exists; ripple-correlation is interesting | String period ≫ module-level need |
| AFDD / arc research | R-AFDD-R | Highest strategic upside **and** highest false-claim risk | Separate HF ADC path + AFE + dual-MCU for any safety argument |
| Safe lab / fail-closed ops | R-SAFE-LAB | Contracts landed in software | Hardware proof still required |

**Near-term GO:** docs, canvas, claim hygiene, finish disconnected bench queue.  
**Near-term NO-GO:** AFDD firmware PRs, UL language, seventh software-fix slice, stay-off churn (USB composite, `delay(1)`, `applyPwmConfig` fan-out, FLAGS-WRAP, EXTMEM March, WCET as software churn, CMSIS FFT / global `-O3`).

---

## 2. What already exists (asset map)

Ground truth for “what can we productise” starts from landed capability, not from aspiration.

| Domain | Present today (bench) | Not present / not proven |
|--------|----------------------|---------------------------|
| Modulation | Multiple schemes (SPWM/DPWM variants), asymmetric Tm4 path, presets | Inverter-leg power stage on the 2 kV switch board (see PRODUCT_READINESS §2) |
| Custom waveforms | Arbitrary references / pulse sequences via web + API | Field waveform library with signed provenance |
| Closed loop | `Feedback.LoopHz` default 250; regulation paths | Host-proven OUTEN safety |
| Fault | ACMP → XBAR → FAULT0 shape; generation-gated `releaseOutputInhibit` | Pin-to-gate latency measurement; Tm3/Tm4 HW gate |
| Capture / spectrum | ISR-tied capture, portable FFT, TEGS binary spectrum wire | 250–500 kSPS free-running HF path for arcs |
| Metering | Power / VRMS / IRMS / PF / energy on full status | Die-only thermal as release gate (explicitly refused) |
| PLL | Bench reference lock | Grid-tie / anti-islanding / interconnection |
| MPPT | ~3 s string-level settle | Module-level ripple correlation |
| Comms | MQTT 17 entities, Influx, web UI, MTP read-only composite | Signed A/B OTA; AGPL Ethernet policy for commercial redistribute |
| Thermal | Fail-closed / keep-sample / mask-before-scan contracts | Live OneWire-while-inhibited campaign |
| Lab safety | Global inhibit, schema pins, identity-gated secrets | Functional-safety FFI (single MCU + internet stack) |

---

## 3. Four firmware facts that constrain every role

From PRODUCT_READINESS (unchanged; still binding):

1. **Capture cannot see an arc.** One sample per carrier → tens of kSPS, not the 1–100 kHz arc band. AFDD needs a **separate** ADC_ETC+DMA (or external ADC) path and an HF CT AFE that does not exist on the board yet.
2. **Carrier dither sabotages detection.** Spread-spectrum and arc listening share the band. Enforce mutual exclusion in validation, not only in docs.
3. **Safety shares a superloop with the internet.** HTTP, MQTT, Influx, SD, MTP, OTA, and trip logic on one MCU fail freedom-from-interference for any serious safety case. Split: bare-metal safety MCU (no network) + this firmware as comms / lab UI.
4. **MPPT is string-period.** Settled meter windows make ~3 s loops correct for strings and useless for module MLPE without a different control law (ripple correlation).

Hardware reminder: the existing switch board is a **bidirectional AC interrupter**, not an inverter half-bridge. A product inverter or optimiser needs a **second** power board; the 2 kV board stays the DC interrupter.

---

## 4. Role deep-dives

### 4.1 R-CORE — Bench PWM instrument

**Keep as the identity.** Every README / UI / MQTT sentence stays “bench / inhibited / not ISR proof” unless a checklist row is checked.

Useful near-term features (still claim-safe):

- Bench runbooks that bind each UI control to a `BENCH_CHECKS` row ID
- Event-log export that tags *unverified* vs *bench-stamped* (no fake certification)
- Fixture-driven UI stills (already: `scripts/readme_ui_fixtures/`) — extend only with claim-safe captions

### 4.2 R-INV — Inverter / modulation product lens

**Already strong software surface.** Gaps are almost all **hardware + evidence**:

- Pair-mode / dead-time scope proof (PRODUCT_READINESS still marks firmware fix unverified)
- Polarity schemes 2/5/4-POD on inverted cells (§0a)
- Fault-during-clear and ACMP→FAULT0 latency
- Thermal release while OUTEN live
- Real half-bridge stage (new PCB), not strap-hacked interrupter FET

**Useful software later (after critical bench):** modulation scheme presets labelled by *intended power stage*; soft-start profiles keyed to measured DC-link; explicit “no freewheel path” inhibit when stage type = interrupter.

### 4.3 R-WGEN — Custom waveform generator

This is the **highest near-term lab differentiator** already in the tree: arbitrary references, pulse sequences, multi-timer mapping, web editing.

Useful additions (host-testable where possible):

| Idea | Why | Caveat |
|------|-----|--------|
| Waveform library with CRC + schema version | Reproducible lab recipes | Do not auto-release OUTEN on load |
| Segmented sequences (burst / dead / re-arm) | Arc-injection *research* stimulus and EMI stress | Never call it AFDD self-test |
| Import/export of waveform sets separately from full config | Avoid secret/identity pitfalls | Keep `restoreSecrets` identity gate |
| Host Unity for quantize / clamp edges | Matches spectrum_wire pattern | Still not ISR proof |
| Dither auto-off when “sense” mode engaged | Prepares AFDD coexistence | Requires explicit mode bit |

### 4.4 R-INST — Instrumentation

Capture, triggered scope, spectrum/THD, power meter, power-mon on aux rail.

Useful additions:

- Longer PSRAM ring with inhibited-only MTP dump (already MTP-gated)
- Spectrum floor / saturate already host-tested — keep census language honest
- “Meter window ready” signalling for MPPT / experiments (status already distinguishes lite vs full)

**Do not** claim capture path is an arc detector.

### 4.5 R-REF — Reference PLL

Keep language: **bench reference lock, not grid-tie.** Useful lab features: lock-quality telemetry, intentional unlock inject for UI testing, documented phase-error units. Stay-off: anti-islanding, ride-through, interconnection profiles.

### 4.6 R-MPPT — Tracker research

String tracker exists. Differentiating research (PRODUCT_READINESS):

- **Ripple-correlation MPPT** using per-carrier V/I samples (this codebase is unusually well placed)
- Cap-load IV sweep **only** if a series interrupter can open the string and isolate (switch-board product story — not this PWM stage alone)

Explicitly low value here: EV charge coordination, forecast HA behaviour, grid services without aggregation, soiling without met station.

### 4.7 R-AFDD-R — Arc / AFDD research (not a listed device)

Positioning that survives scrutiny (PRODUCT_READINESS §2 / §4):

- Speed is the **wrong** headline; Sandia-class **masking reliability** and **provable self-test** are the moat
- UL 1699B unwanted-trip tests punish hair-trigger comparators
- Differentiating *product* features (for a **future** dual-MCU + interrupter system, not this image alone):
  1. Insulation resistance / prove-safe with switch open
  2. Interrupter self-test (µs open-pulse, logged) → Voc-based module temp free
  3. Capacitor-load IV sweep
  4. SunSpec Rapid Shutdown receiver polarity (normally-off / fail-open)
  5. SunSpec Modbus over Ethernet (after AGPL policy)
  6. Connector resistance trending via HF perturbation + sync detection (research/IP only)

**Firmware prerequisites (dependency order):**

1. Resolve QNEthernet **AGPL** posture for any commercial binary  
2. Fail-dark boot / config transaction — software hardened, hardware unverified  
3. Signed A/B OTA + anti-rollback (lab OTA stay stubbed in production)  
4. Recoverable settings / PSRAM — software hardened, power-cut proof pending  
5. **High-rate acquisition** (ADC_ETC+DMA 250–500 kSPS, HF CT AFE, prefer 16-bit external)  
6. **Split safety MCU** from comms MCU  
7. NV authenticated event log + trusted time  
8. Self-test scheduler  
9. Dither/detection interlock in validate  
10. Nuisance-trip dataset (inverter noise, MPPT, disconnects)

Until (5)+(6) exist, any UI string that says “arc protection” is a **false claim**. Prefer “HF research capture (disabled)” behind a compile flag that defaults off.

### 4.8 R-SAFE-LAB — Operational safety for the bench

Contracts already named in operator docs (#67/#68 era): thermal fail-closed / keep-sample / mask-before-scan; MTP `mtpAllowsPwmRelease`; identity-gated `restoreSecrets`; generation-gated OUTEN commit.

Useful additions:

- PIN / auth audit trail in EventLog (no secrets)
- “Release refused” reason codes over MQTT/status (operator clarity, not certification)
- Checklist-driven inhibit that refuses release if required bench rows unchecked (optional lab mode)

---

## 5. Cross-cutting platform features worth considering

Ranked by fit to **this** repo’s strengths (PWM timing, capture, web, Ethernet) and claim risk.

| Priority | Feature | Fits roles | Gate |
|----------|---------|------------|------|
| P0 | Finish disconnected `BENCH_CHECKS` campaign | All | Hardware time |
| P0 | Claim-safe roadmap / PRODUCT_READINESS cross-links (this doc) | All | Docs PR |
| P1 | Waveform library + dither/sense mutex bit | R-WGEN, R-AFDD-R prep | Host tests + docs |
| P1 | Release-refuse reason codes in status/MQTT | R-SAFE-LAB | Host serde tests |
| P2 | Ripple-correlation MPPT experiment behind flag | R-MPPT | Bench V/I integrity |
| P2 | HF DMA capture prototype on spare ADC pins (lab AFE) | R-AFDD-R | Hardware AFE first |
| P3 | Dual-image design notes / second MCU handshake stub | R-AFDD-R, R-SAFE | Architecture only until board |
| P3 | SunSpec / Modbus after AGPL decision | Field products | Legal + new stack |
| Stay-off | Grid services, HA energy dashboard product claims | — | Policy |
| Stay-off | CMSIS FFT / global `-O3`, USB lean PID, strip `applyPwmConfig` | — | Existing stay-offs |
| Stay-off | “AFDD certified” / UL 1699B marketing on Teensy image | — | Impossible honestly |

---

## 6. Sequencing (phases)

```text
Phase N  Now          Docs + claim hygiene + canvas (this PR class)
Phase B  Bench        Critical disconnected evidence (fault, thermal, MTP, polarity, WCET budget)
Phase R  Research     HF AFE + DMA capture + dither mutex + nuisance dataset (after B criticals)
Phase C  Cert path    Dual MCU + interrupter self-test + SunSpec — explicit non-commitment
```

Phase N completion criteria:

- [x] This document exists under `docs/`
- [ ] Canvas `feature-roadmap-2026-08-30.canvas.tsx` available in IDE
- [ ] `AGENTS.md` / `teg-pwm-memory` note multi-role ideation is claim-safe only
- [ ] No behavioural firmware change required

Phase B does **not** unlock product claims; it unlocks confidence in the **existing** bench instrument.

Phase R may produce publishable research and provisional IP (connector trending, masking datasets). It does **not** produce a listed AFDD.

Phase C is a **different program** (hardware + process + licence), not a Teensy software sprint.

---

## 7. Alternatives considered

| Alternative | Why not chosen |
|-------------|----------------|
| Single “all-in-one inverter+AFDD+MPPT” SKU on this board | Category error + Nyquist + FFI + wrong power stage |
| Seventh adversarial software-fix slice | Host-safe leftover class empty; remaining items need bench or stay-off |
| Market UL 1699B on current capture path | Physically false; creates liability |
| Optimiser + string inverter in one box | PRODUCT_READINESS: category error; Tigo-class diverter is a different architecture |
| Drop Ethernet / rewrite stack immediately | Large; gate on AGPL decision and product path, not ideation |

---

## 8. Dependencies and risks

| ID | Item |
|----|------|
| DEP-AGPL | QNEthernet AGPL attaches to algorithms in the binary once disclosed commercially |
| DEP-BENCH | ~many unchecked `BENCH_CHECKS` rows; NO-SHIP |
| DEP-HW-AFE | HF CT + external ADC for arc research |
| DEP-HW-HB | Real half-bridge for inverter product |
| DEP-FFI | Second MCU for any safety function argument |
| RISK-CLAIM | Operator copy drift into product language (watch README / MQTT / HA) |
| RISK-DITHER | Shipping dither with detection characterised without it |
| RISK-UL3741 | Rapid-shutdown market structure; insurer/arc wedge still the coherent story |

---

## 9. Suggested follow-on work (not started)

Executable docs/bench only unless a later plan says otherwise:

1. Squash-merge open docs PRs that stay claim-safe (#73 re-audit when ready).
2. Run disconnected checklist; stamp rows in `BENCH_CHECKS.md`.
3. When Phase R is authorised: new plan under `plan/feature-afdd-research-*.md` (never `refactor-adversarial-fixes-7.md`) with HF path design only.
4. Optional host tests for release-refuse reason codes / waveform library CRC — after API design review.

---

## 10. Related reading

- [PRODUCT_READINESS.md](PRODUCT_READINESS.md)
- [BENCH_CHECKS.md](BENCH_CHECKS.md)
- [SECURITY.md](SECURITY.md)
- [CI_SECURITY.md](CI_SECURITY.md)
- README sections: Modes, Custom waveforms, Capture, Spectrum, PLL, MPPT, Fault protection, MQTT
- Review slices 2026-08-28 (1–6) and any later evidence-triage notes

---

*Document date: 2026-08-30. Host tests and this roadmap are not bench proof.*
