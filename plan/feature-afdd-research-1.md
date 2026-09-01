---
goal: Document MACAPD AFDD-class HF arc research for Teensy 4.1 / i.MX RT1060
version: 1.0
date_created: 2026-08-30
last_updated: 2026-08-30
owner: teg
status: 'Completed'
tags: [feature, research, afdd, docs, architecture]
---

# Introduction

![Status: Completed](https://img.shields.io/badge/status-Completed-brightgreen)

Produce claim-safe research documentation and an IDE canvas for an innovative HF arc-precursor detection algorithm (MACAPD) that uses i.MX RT1060 ADC_ETC, DMA, XBAR, and FlexPWM blanking — without shipping AFDD firmware or UL claims.

## 1. Requirements & Constraints

- **REQ-001**: Write `docs/FEATURE_AFDD_RESEARCH_2026-08-30.md` covering physics (DC has no line frequency), MACAPD algorithm, RT1060 peripheral map, ADC ownership modes, GO/NO-GO, and explicit non-claims.
- **REQ-002**: Provide IDE canvas `feature-afdd-research-2026-08-30.canvas.tsx` summarizing pipeline, features, and gates.
- **REQ-003**: Cross-link PRODUCT_READINESS capture/dither gaps and R-AFDD-R roadmap framing when present; recommend M3 ADC parts (ADS8860 / AD7380).
- **REQ-004**: Land host-testable MACAPD math in `src/afdd_macapd.h` + `test/test_afdd_macapd` and detailed algorithm doc `docs/FEATURE_AFDD_MACAPD_ALGORITHM.md` with zero OUTEN wiring.
- **SEC-001**: Teensy image must retain zero OUTEN trip authority from HF scores; dual-MCU required before any safety product language.
- **CON-001**: Do not enable production `TEG_ENABLE_CMSIS_FFT` or global `-O3`; do not create `plan/refactor-adversarial-fixes-7.md`.
- **CON-002**: No behavioural AFDD firmware in this plan; docs + memories only.
- **GUD-001**: Prefer positive claim-safe wording (“HF research capture”) over protection marketing.
- **PAT-001**: Land via feature-branch PR squash-merge; do not commit operator-doc refreshes directly on `main`.

## 2. Implementation Steps

### Implementation Phase 1

- GOAL-001: Author research documentation and canvas

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-001 | Extract ADC / ADC_ETC / eDMA / CEC facts from local RT1060 PDFs into analysis (not committed under `.cache/`) | ✅ | 2026-08-30 |
| TASK-002 | Write `docs/FEATURE_AFDD_RESEARCH_2026-08-30.md` with MACAPD, pipeline, modes M0–M3, non-claims | ✅ | 2026-08-30 |
| TASK-003 | Write canvas `feature-afdd-research-2026-08-30.canvas.tsx` under Cursor canvases directory | ✅ | 2026-08-30 |

### Implementation Phase 2

- GOAL-002: Persist agent memory and open docs PR

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-004 | Update `AGENTS.md` and `.github/instructions/teg-pwm-memory.instructions.md` with R-AFDD-R / MACAPD framing | ✅ | 2026-08-30 |
| TASK-005 | Open feature-branch PR `docs/feature-afdd-research-2026-08-30` targeting `main` (docs only; no `.cache/`) | ✅ | 2026-08-30 |
| TASK-006 | Add M3 part recommendation (ADS8860 / AD7380) to research note | ✅ | 2026-08-30 |
| TASK-007 | Implement `src/afdd_macapd.h` + `test/test_afdd_macapd` (no OUTEN wiring) | ✅ | 2026-08-30 |
| TASK-008 | Write `docs/FEATURE_AFDD_MACAPD_ALGORITHM.md` and include in git | ✅ | 2026-08-30 |

## 3. Alternatives

- **ALT-001**: Reuse `captureTick` at higher carrier — rejected (still one sample per carrier; Nyquist insufficient).
- **ALT-002**: Energy threshold on portable spectrum UI — rejected (wrong buffer; false product signal).
- **ALT-003**: Full continuous cyclic spectral density on MCU — deferred (CPU/Ethernet contention); offline OK.

## 4. Dependencies

- **DEP-001**: Local NXP RT1060 RM / CEC / errata PDFs for peripheral accuracy.
- **DEP-002**: `docs/PRODUCT_READINESS.md` §1.1–1.2 (capture Nyquist; dither sabotage).
- **DEP-003**: Future hardware AFE and optional dual-MCU — out of scope for this docs PR.

## 5. Files

- **FILE-001**: `docs/FEATURE_AFDD_RESEARCH_2026-08-30.md` — primary research note.
- **FILE-002**: `plan/feature-afdd-research-1.md` — this plan.
- **FILE-003**: `AGENTS.md` — continual-learning / claim-safe AFDD bullets.
- **FILE-004**: `.github/instructions/teg-pwm-memory.instructions.md` — PWM-domain AFDD research pointer.
- **FILE-005**: IDE canvas `feature-afdd-research-2026-08-30.canvas.tsx` (not in git repo).
- **FILE-006**: `docs/FEATURE_AFDD_MACAPD_ALGORITHM.md` — detailed MACAPD explanation.
- **FILE-007**: `src/afdd_macapd.h` — host-testable algorithm (no OUTEN).
- **FILE-008**: `test/test_afdd_macapd/test_main.cpp` — Unity native tests.

## 6. Testing

- **TEST-001**: Manual review — document contains no UL 1699B compliance claim and states zero Teensy trip authority.
- **TEST-002**: Confirm PR diff excludes `.cache/`, `.cursor/`, and firmware behavioural changes.
- **TEST-003**: `pio test -e native --filter test_afdd_macapd` passes on host (math only).

## 7. Risks & Assumptions

- **RISK-001**: Readers may treat research score as a product trip — mitigated by repeated non-claims and NO-GO firmware policy.
- **RISK-002**: On-chip 12-bit ADC may be too noisy for field arcs — document prefers external 16-bit (M3).
- **ASSUMPTION-001**: Arc HF interest band ~1–100 kHz remains a valid research framing pending AFE measurement.
- **ASSUMPTION-002**: ADC_ETC DMA SyncMode can be brought up in lab inhibit mode without changing production metering defaults.

## 8. Related Specifications / Further Reading

- [docs/FEATURE_AFDD_RESEARCH_2026-08-30.md](../docs/FEATURE_AFDD_RESEARCH_2026-08-30.md)
- [docs/FEATURE_AFDD_MACAPD_ALGORITHM.md](../docs/FEATURE_AFDD_MACAPD_ALGORITHM.md)
- [docs/FEATURE_AFDD_MACAPD_RESEARCH_2026-08-30.md](../docs/FEATURE_AFDD_MACAPD_RESEARCH_2026-08-30.md)
- [docs/PRODUCT_READINESS.md](../docs/PRODUCT_READINESS.md)
- [docs/FEATURE_ROADMAP_2026-08-30.md](../docs/FEATURE_ROADMAP_2026-08-30.md) (when merged)
- [NXP i.MX RT1060](https://www.nxp.com/products/i.MX-RT1060)
