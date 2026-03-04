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

## 1. Robustness & Numerical Safeguards (PLAN.md)

- [ ] Position sanitization: reject/skip non-finite positions (`NaN`, `Inf`) before upload in both retained and transient paths.
- [ ] Normal safety: renormalize with epsilon guard in point and surface shaders (fallback to camera-facing basis).
- [ ] Triangle degeneracy: skip zero-area triangles during edge extraction and surface rendering.
- [ ] Line width clamping: `[0.5, 32.0]` pixel range.
- [ ] Point radius clamping: `clamp(size, 0.0001, 1.0)` world-space.
- [ ] Zero-length graph edges: collapse to point primitive path.
- [ ] EWA covariance conditioning: eigenvalue floor + fallback to isotropic FlatDisc on ill-conditioned covariance.
- [ ] Depth conflicts: mode-specific depth bias for edge/point z-fighting against mesh surfaces.

---

## 2. Related Documents

- `PLAN.md` — detailed rendering architecture refactor spec (three-pass architecture, ECS component design, migration phases).
- `ROADMAP.md` — feature roadmap, prioritization phases, long-horizon planning, rendering modality redesign vision (§5), and architecture SLOs.
- `README.md` — architecture SLOs section with measurable SLO thresholds and telemetry milestones.
- `CLAUDE.md` — development conventions, C++23 adoption policy, architectural invariants.
