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

### 1.1 2026-02-25 Architecture Review Follow-ups

- [ ] **Establish architecture SLOs + telemetry milestones (Cross-cutting).**
  - Define measurable targets for DAG compile budget, scheduler contention/tail latency, and frame critical-path timing.
  - Add instrumentation for queue contention, steal ratio, barrier/idle wait time, and per-frame compile/execute split.

## 1.2 2026-02-26 Code Quality Audit (Style/Syntax, Architecture, Duplication)

This section captures **newly observed inconsistencies** and concrete remediation actions.

### B. Architecture findings

### D. Problems in current TODO governance (meta)

- [ ] **Missing measurable acceptance criteria for several high-impact TODOs (High).**
  - Items mention goals (fairness/tail behavior/critical path) without explicit thresholds.
  - Action: for each High item define concrete SLO gates (example: `FrameGraph CPU execute p95 < 0.35 ms @ 2k nodes`, `steal ratio target band`, `compile budget p99`).

## 2. Related Documents

- `ROADMAP.md` — feature roadmap, prioritization phases, and long-horizon planning details.
