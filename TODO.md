# IntrinsicEngine — Architecture TODOs (Living Document)

This document tracks **what's left to do** in IntrinsicEngine's architecture.

**Policy:** If something is fixed/refactored, it should **not** remain as an "issue" here. We rely on Git history for the past.

---

## 0. Scope & Success Criteria

**Goal:** a data-oriented, testable, modular engine architecture with:

- Deterministic per-frame orchestration (CPU + GPU), explicit dependencies.
- Robust multithreading contracts in Core/RHI.
- Minimal "god objects"; subsystems testable in isolation.

---

## 1. Related Documents

- `docs/architecture/rendering-three-pass.md` — canonical three-pass rendering architecture spec (pass contracts, data contracts, invariants).
- `PLAN.md` — archival index for the completed rendering refactor.
- `ROADMAP.md` — feature roadmap, prioritization phases, long-horizon planning, rendering modality redesign vision, and architecture SLOs.
- `README.md` — architecture SLOs section with measurable SLO thresholds and telemetry milestones.
- `CLAUDE.md` — development conventions, C++23 adoption policy, architectural invariants.

---

## 2. Near-Term Epics (from `ROADMAP.md`)

This is the active dependency-ordered execution queue. Complete top-to-bottom unless sequencing rules explicitly allow overlap.

---

### Sequencing Rules

- Epic 1 (HDR Post-Processing) is complete. Epic 2 (Scene Serialization) is complete. Epic 3 (Transform Gizmos) is complete. Epic 4 (Profiling & Benchmark) is complete.

### Cross-Epic Definition of Done

- Update `README.md` for each merged epic (usage + architecture deltas).
- Remove superseded code paths immediately (no compatibility clutter beyond staged migration windows).
- Add at least one integration test per epic and wire into existing test targets.
