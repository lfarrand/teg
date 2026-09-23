---
goal: Walk MACAPD research recommendations P0–P3 with claim-safe host math and docs
version: 1.0
date_created: 2026-08-30
last_updated: 2026-08-30
owner: teg
status: 'In progress'
tags: [feature, research, afdd, macapd, warp, mef]
---

# Introduction

![Status: In progress](https://img.shields.io/badge/status-In%20progress-yellow)

Execute MACAPD recommendations from `docs/FEATURE_AFDD_MACAPD_RESEARCH_2026-08-30.md` §7 in priority order. Keep research / algorithm / MEF documents synchronized. Host math never drives OUTEN.

## 1. Requirements & Constraints

- **REQ-001**: Update §7 recommendation table with Done/Spec/Partial status after each landing.
- **REQ-002**: Land P1 host fixes (mask-exclude kurtosis, keep-count inhibit, tonal ±Δ) in `src/afdd_macapd.h` + tests.
- **REQ-003**: Land P2 EWMA freeze, coherence weight hook, WARP host skeleton, MEF E01–E20 doc.
- **REQ-004**: Leave P1 dither wire and P2 real masking / MEF UI as Spec until HF sense path exists.
- **SEC-001**: No OUTEN / `releaseOutputInhibit` from MACAPD, WARP, or MEF scores.
- **CON-001**: SchemaVersion stays 1; omit-if-default for future `Afdd.Evidence[20]`.
- **CON-002**: Do not create `plan/refactor-adversarial-fixes-7.md`.
- **GUD-001**: Claim-safe wording only; no UL 1699B product claims.
- **PAT-001**: Feature-branch PR squash-merge; host tests reported separately from six-suite gate.

## 2. Implementation Steps

### Implementation Phase 1

- GOAL-001: P1 host math + docs sync

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-001 | Mask-exclude kurtosis/coherence + keepMin inhibit in `src/afdd_macapd.h` | ✅ | 2026-08-30 |
| TASK-002 | Tonal residual ±Δ Goertzel in `afddMacapdTonalResidual` | ✅ | 2026-08-30 |
| TASK-003 | Extend `test/test_afdd_macapd/test_main.cpp` for mask/keep/EWMA freeze | ✅ | 2026-08-30 |
| TASK-004 | Update `FEATURE_AFDD_MACAPD_ALGORITHM.md` Stage A–C for mask-exclude / ±Δ / freeze | ✅ | 2026-08-30 |

### Implementation Phase 2

- GOAL-002: P2 WARP + MEF docs

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-005 | Add `src/afdd_warp.h` Haar WPT J=3 + PrecursorWatch SM (never OUTEN) | ✅ | 2026-08-30 |
| TASK-006 | Add `test/test_afdd_warp/test_main.cpp` smoke tests | ✅ | 2026-08-30 |
| TASK-007 | Write `docs/FEATURE_AFDD_MACAPD_MEF_2026-08-30.md` (E01–E20) | ✅ | 2026-08-30 |
| TASK-008 | Status-column §7 + cross-links in research note; memories | ✅ | 2026-08-30 |

### Implementation Phase 3

- GOAL-003: Remaining Spec items (later)

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-009 | Wire `ditherActive` from `CarrierDitherMode` when HF sense lands | |  |
| TASK-010 | Real masking estimate / self-test feeding `maskingPenalty` | |  |
| TASK-011 | Settings `Afdd.Evidence[20]` checkbox grid + omit-if-default serde | |  |
| TASK-012 | Replace Haar interim with db4 lifting WPT in `afdd_warp.h` | |  |
| TASK-013 | Labelled capture library + P3 AR/SK ablations | |  |

## 3. Alternatives

- **ALT-001**: Full MEF UI in this slice — deferred; catalog doc first.
- **ALT-002**: db4 lifting before Haar — deferred; Haar unblocks host smoke.
- **ALT-003**: Wire Teensy EventLog now — deferred until HF path exists.

## 4. Dependencies

- **DEP-001**: `docs/FEATURE_AFDD_MACAPD_RESEARCH_2026-08-30.md` §6–§7.
- **DEP-002**: Existing `src/afdd_macapd.h` / native test pattern.
- **DEP-003**: Future M3 AFE + dither mutex for TASK-009.

## 5. Files

- **FILE-001**: `src/afdd_macapd.h`
- **FILE-002**: `src/afdd_warp.h`
- **FILE-003**: `test/test_afdd_macapd/test_main.cpp`
- **FILE-004**: `test/test_afdd_warp/test_main.cpp`
- **FILE-005**: `docs/FEATURE_AFDD_MACAPD_MEF_2026-08-30.md`
- **FILE-006**: `docs/FEATURE_AFDD_MACAPD_RESEARCH_2026-08-30.md`
- **FILE-007**: `docs/FEATURE_AFDD_MACAPD_ALGORITHM.md`
- **FILE-008**: `plan/feature-afdd-macapd-recs-1.md`
- **FILE-009**: `AGENTS.md`, `.github/instructions/teg-pwm-memory.instructions.md`

## 6. Testing

- **TEST-001**: `pio test -e native --filter test_afdd_macapd` passes.
- **TEST-002**: `pio test -e native --filter test_afdd_warp` passes.
- **TEST-003**: Manual claim hygiene — no UL / OUTEN trip language in new docs.

## 7. Risks & Assumptions

- **RISK-001**: Haar WPT underperforms db4 on real arcs — mitigate by documenting interim + TASK-012.
- **RISK-002**: EWMA freeze unit test depends on Candidate* entry — thresholds tuned soft for host.
- **ASSUMPTION-001**: Operator continues claim-safe PR review on feature branch #75.

## 8. Related Specifications / Further Reading

- [`docs/FEATURE_AFDD_MACAPD_RESEARCH_2026-08-30.md`](../docs/FEATURE_AFDD_MACAPD_RESEARCH_2026-08-30.md)
- [`docs/FEATURE_AFDD_MACAPD_MEF_2026-08-30.md`](../docs/FEATURE_AFDD_MACAPD_MEF_2026-08-30.md)
- [`plan/feature-afdd-research-1.md`](feature-afdd-research-1.md)
