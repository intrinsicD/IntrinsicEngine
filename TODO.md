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

- `PLAN.md` — detailed rendering architecture refactor spec (three-pass architecture, ECS component design, migration phases).
- `ROADMAP.md` — feature roadmap, prioritization phases, long-horizon planning, rendering modality redesign vision (§5), and architecture SLOs.
- `README.md` — architecture SLOs section with measurable SLO thresholds and telemetry milestones.
- `CLAUDE.md` — development conventions, C++23 adoption policy, architectural invariants.
