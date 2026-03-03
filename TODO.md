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

## 1. PropertySet Dirty-Domain Sync System

Per-frame CPU→GPU synchronization driven by PropertySet change detection, with independent dirty tracking per data domain (vertex/edge/face). Aligns with PLAN.md "Automatic CPU→GPU sync" requirement.

- [ ] Define dirty tag components: `VertexPositionsDirty`, `VertexAttributesDirty`, `EdgeTopologyDirty`, `EdgeAttributesDirty`, `FaceTopologyDirty`, `FaceAttributesDirty`.
- [ ] Sync system detects dirty tags, re-uploads only affected PropertySet spans to GPU buffers.
- [ ] Topology-dirty domains trigger index buffer rebuild; attribute-dirty domains trigger attribute buffer re-upload.
- [ ] Clear dirty tags after upload. Multiple simultaneous dirty domains handled independently (face color change doesn't re-upload vertex buffer).

---

## 2. Subcomponent Hierarchy (PLAN.md)

Support named sub-meshes/sub-graphs/sub-clouds as first-class components over a base geometry component.

- [ ] Define hierarchy node struct: `{NameId, BaseOffset(s), Size(s), Parent(optional)}` with domain-specific offsets (vertex/edge/halfedge/face ranges).
- [ ] `NameId` as `StringId`/hashed id for integration with selection, tooling, and material overrides.
- [ ] Renderables reference hierarchy slices so command building can issue draws for whole objects or named subcomponents.

---

## 3. Robustness & Numerical Safeguards (PLAN.md)

- [ ] Position sanitization: reject/skip non-finite positions (`NaN`, `Inf`) before upload in both retained and transient paths.
- [ ] Normal safety: renormalize with epsilon guard in point and surface shaders (fallback to camera-facing basis).
- [ ] Triangle degeneracy: skip zero-area triangles during edge extraction and surface rendering.
- [ ] Line width clamping: `[0.5, 32.0]` pixel range.
- [ ] Point radius clamping: `clamp(size, 0.0001, 1.0)` world-space.
- [ ] Zero-length graph edges: collapse to point primitive path.
- [ ] EWA covariance conditioning: eigenvalue floor + fallback to isotropic FlatDisc on ill-conditioned covariance.
- [ ] Depth conflicts: mode-specific depth bias for edge/point z-fighting against mesh surfaces.

---

## 4. Related Documents

- `PLAN.md` — detailed rendering architecture refactor spec (three-pass architecture, ECS component design, migration phases).
- `ROADMAP.md` — feature roadmap, prioritization phases, long-horizon planning, rendering modality redesign vision (§5), and architecture SLOs.
- `README.md` — architecture SLOs section with measurable SLO thresholds and telemetry milestones.
- `CLAUDE.md` — development conventions, C++23 adoption policy, architectural invariants.
