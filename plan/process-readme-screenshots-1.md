---
goal: Add claim-safe README screenshots of the operator web UI
version: 1.0
date_created: 2026-08-30
last_updated: 2026-08-30
owner: teg
status: 'Completed'
tags: [docs, chore, media]
---

# Introduction

![Status: Completed](https://img.shields.io/badge/status-Completed-brightgreen)

Add static PNG screenshots under `docs/images/` and wire them into `README.md` § Web UI and API. Captures use a local fixture `/api` peer so the SPA renders; captions must state UI orientation only — not bench proof, not ISR/OUTEN proof.

## 1. Requirements & Constraints

- **REQ-001**: Commit 3–4 PNGs under `docs/images/` showing Settings and Stats operator chrome.
- **REQ-002**: Embed those images in `README.md` under **Web UI and API** with honesty captions.
- **REQ-003**: Provide a reproducible capture script under `scripts/readme_ui_fixtures/`.
- **SEC-001**: Frames must not show write PIN, MQTT password, Influx token, or real hostnames.
- **CON-001**: Do not invent bench proof, grid-tie product language, HA energy-dashboard claims, or a test census.
- **CON-002**: Captions must say fixtures / UI orientation; outputs shown as inhibited; see `docs/BENCH_CHECKS.md`.
- **CON-003**: Do not create `plan/refactor-adversarial-fixes-7.md`.
- **CON-004**: Land via feature-branch PR; do not commit docs refresh on `main`.
- **GUD-001**: Prefer PNG over animated GIF for README stills.
- **PAT-001**: Store media in `docs/images/`, not `web/` or repo-root `assets/`.

## 2. Implementation Steps

### Implementation Phase 1

- GOAL-001: Capture and commit claim-safe README media

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-001 | Create `scripts/readme_ui_fixtures/serve_and_capture.py` serving `web/` plus synthetic `/api/status`, `/api/config`, `/api/log`, spectrum stub | ✅ | 2026-08-30 |
| TASK-002 | Write PNGs to `docs/images/readme-ui-*.png` (settings inhibited, MTP crop, fault banner, stats) | ✅ | 2026-08-30 |
| TASK-003 | Update `README.md` § Web UI and API with figures and honesty captions | ✅ | 2026-08-30 |
| TASK-004 | Open feature-branch PR; squash-merge only after review | | |

## 3. Alternatives

- **ALT-001**: Capture only from live Teensy — preferred for production photography later; blocked here without hardware.
- **ALT-002**: Open `file://` HTML without `/api` — rejected; empty chrome misrepresents the UI.
- **ALT-003**: AI-generated mockups — rejected; would invent product look unrelated to `web/`.

## 4. Dependencies

- **DEP-001**: Playwright + Chromium for headless capture.
- **DEP-002**: Existing `web/index.html` and `web/stats.html`.

## 5. Files

- **FILE-001**: `scripts/readme_ui_fixtures/serve_and_capture.py` — fixture server + capture.
- **FILE-002**: `docs/images/readme-ui-*.png` — committed screenshots.
- **FILE-003**: `README.md` — embed figures.
- **FILE-004**: `plan/process-readme-screenshots-1.md` — this plan.

## 6. Testing

- **TEST-001**: Run `python scripts/readme_ui_fixtures/serve_and_capture.py` and confirm four PNGs rewrite under `docs/images/`.
- **TEST-002**: Human check: captions mention fixture/UI orientation and point at `docs/BENCH_CHECKS.md`; no PIN/secrets in frames.

## 7. Risks & Assumptions

- **RISK-001**: Reviewers may mistake fixture telemetry for bench data — mitigated by caption fences.
- **ASSUMPTION-001**: Synthetic inhibited/fault status JSON is enough to exercise status bar, fault banner, MTP status, and Stats layout.

## 8. Related Specifications / Further Reading

- [docs/BENCH_CHECKS.md](../docs/BENCH_CHECKS.md)
- [docs/SECURITY.md](../docs/SECURITY.md)
- [AGENTS.md](../AGENTS.md)
