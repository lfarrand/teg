---
goal: Land claim-safe multi-role feature roadmap documentation for TEG
version: 1.0
date_created: 2026-08-30
last_updated: 2026-08-30
owner: teg
status: 'Completed'
tags: [docs, architecture, feature, roadmap]
---

# Introduction

![Status: Completed](https://img.shields.io/badge/status-Completed-brightgreen)

Publish a claim-safe multi-role feature roadmap (inverter, waveform generator, AFDD research lenses) as `docs/FEATURE_ROADMAP_2026-08-30.md`, align agent memories, and open a docs-only feature-branch PR. No behavioural firmware changes.

## 1. Requirements & Constraints

- **REQ-001**: Document roles as lenses on a bench PWM instrument, not certified product SKUs.
- **REQ-002**: Cross-link `docs/PRODUCT_READINESS.md` binding constraints (Nyquist, dither, FFI, wrong power stage).
- **REQ-003**: State explicit near-term GO (docs/bench) and NO-GO (AFDD firmware PRs, UL claims, fixes-7).
- **SEC-001**: Do not invent UL 1699B, grid-tie, or HA energy-dashboard product claims.
- **CON-001**: Land only via feature-branch PR; do not commit docs directly on `main`.
- **CON-002**: Do not create `plan/refactor-adversarial-fixes-7.md`.
- **GUD-001**: Keep host Unity / operator docs language as not ISR/OUTEN proof.
- **PAT-001**: Follow existing dated docs pattern (`docs/FEATURE_ROADMAP_YYYY-MM-DD.md`) and IDE canvas companion.

## 2. Implementation Steps

### Implementation Phase 1

- GOAL-001: Author roadmap markdown and memory alignment

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-001 | Write `docs/FEATURE_ROADMAP_2026-08-30.md` with roles, phases N/B/R/C, ranked features, AFDD prerequisites | ✅ | 2026-08-30 |
| TASK-002 | Write IDE canvas `feature-roadmap-2026-08-30.canvas.tsx` summarizing role matrix and priorities | ✅ | 2026-08-30 |
| TASK-003 | Update `AGENTS.md` preference/workspace facts for multi-role claim-safe ideation and #71/#72 | ✅ | 2026-08-30 |
| TASK-004 | Add Multi-role ideation section to `.github/instructions/teg-pwm-memory.instructions.md` | ✅ | 2026-08-30 |

### Implementation Phase 2

- GOAL-002: Open docs-only PR for review

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-005 | Commit roadmap + memories on branch `docs/feature-roadmap-2026-08-30` | ✅ | 2026-08-30 |
| TASK-006 | Push and open PR to `main` with claim-safe summary and test plan (docs review only) | ✅ | 2026-08-30 |

## 3. Alternatives

- **ALT-001**: Fold roadmap into `docs/PRODUCT_READINESS.md` only — rejected; PRODUCT_READINESS stays constraint-focused; roadmap needs role matrix and sequencing.
- **ALT-002**: Start AFDD firmware immediately — rejected; violates Nyquist/FFI and claim hygiene.
- **ALT-003**: Create `plan/refactor-adversarial-fixes-7.md` — rejected; host-safe leftover class empty; remaining items are bench/stay-off.

## 4. Dependencies

- **DEP-001**: Existing `docs/PRODUCT_READINESS.md` and `docs/BENCH_CHECKS.md` as authoritative gates.
- **DEP-002**: Completed review slices 1–6 for “no seventh fix slice” posture.

## 5. Files

- **FILE-001**: `docs/FEATURE_ROADMAP_2026-08-30.md` — primary roadmap.
- **FILE-002**: `AGENTS.md` — claim-safe multi-role preference and workspace fact.
- **FILE-003**: `.github/instructions/teg-pwm-memory.instructions.md` — multi-role ideation section.
- **FILE-004**: `plan/architecture-feature-roadmap-1.md` — this plan.
- **FILE-005**: IDE canvas `feature-roadmap-2026-08-30.canvas.tsx` (outside git tree under Cursor projects).

## 6. Testing

- **TEST-001**: Human review that roadmap contains no UL/grid-tie/HA energy-dashboard product claims.
- **TEST-002**: Confirm PR does not modify `src/**` behavioural firmware.
- **TEST-003**: Identifier uniqueness checks on this plan file return empty for declaration duplicates.

## 7. Risks & Assumptions

- **RISK-001**: Readers may treat role names as shippable SKUs — mitigated by repeated NO-SHIP / research-only wording.
- **ASSUMPTION-001**: Disconnected bench evidence remains the critical path before any Phase R firmware.

## 8. Related Specifications / Further Reading

- [docs/FEATURE_ROADMAP_2026-08-30.md](../docs/FEATURE_ROADMAP_2026-08-30.md)
- [docs/PRODUCT_READINESS.md](../docs/PRODUCT_READINESS.md)
- [docs/BENCH_CHECKS.md](../docs/BENCH_CHECKS.md)
