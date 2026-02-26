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

## 1. Open TODOs (What's left)

No open items.

## 2. Related Documents

- `ROADMAP.md` — feature roadmap, prioritization phases, and long-horizon planning details.
- `docs/ARCHITECTURE_SLOS.md` — measurable SLO thresholds and telemetry milestones for orchestration and scheduler health.
