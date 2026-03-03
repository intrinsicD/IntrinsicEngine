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

## 1. Rendering Architecture Refactor (PLAN.md)

Replace the dual-path (transient CPU + retained GPU) rendering with a **single unified path per primitive type** (`SurfacePass`, `LinePass`, `PointPass`). Full spec in `PLAN.md`.

### 1.7 Per-Face Attribute Support (PLAN.md Phase 7)

Per-face attribute rendering infrastructure exists (per-face colors via `gl_PrimitiveID`). Needs adaptation for the new SurfacePass naming.

- [ ] Verify per-face attribute buffer upload works in `SurfacePass` (from `Faces` PropertySet).
- [ ] Verify `PtrFaceAux` BDA channel in surface push constants.
- [ ] Test: flat-shading per-face colors, curvature visualization, segmentation labels.

### 1.8 UI and Inspector Integration (PLAN.md Phase 8)

- [ ] Add `PointRenderMode` UI combo selector in Inspector.
- [ ] Per-entity component attach/detach for wireframe and vertex visualization (replaces boolean toggles).
- [ ] Graph visualization mode controls.
- [ ] Per-edge/per-face attribute visualization toggles.

### 1.9 Documentation Update (PLAN.md Phase 9)

- [ ] Update `TODO.md` — remove completed items.
- [ ] Update `CLAUDE.md` — document new pass naming, ECS component types, and architecture.
- [ ] Update `README.md` — update rendering architecture description.

### 1.10 Push Constant Runtime Validation

- [ ] Add `maxPushConstantsSize` validation to `PipelineBuilder::Build()` (currently no runtime check exists in `RHI.Pipeline.cpp`).

---

## 2. Geometry View Lifecycle — Remaining Enhancement

- [ ] Staged (device-local) upload path for large static graphs in `GraphGeometrySyncSystem` (current Direct mode is suitable for dynamic re-layout; staged path for static graphs would reduce host-visible memory).

---

## 3. PropertySet Dirty-Domain Sync System

Per-frame CPU→GPU synchronization driven by PropertySet change detection, with independent dirty tracking per data domain (vertex/edge/face). Aligns with PLAN.md "Automatic CPU→GPU sync" requirement.

- [ ] Define dirty tag components: `VertexPositionsDirty`, `VertexAttributesDirty`, `EdgeTopologyDirty`, `EdgeAttributesDirty`, `FaceTopologyDirty`, `FaceAttributesDirty`.
- [ ] Sync system detects dirty tags, re-uploads only affected PropertySet spans to GPU buffers.
- [ ] Topology-dirty domains trigger index buffer rebuild; attribute-dirty domains trigger attribute buffer re-upload.
- [ ] Clear dirty tags after upload. Multiple simultaneous dirty domains handled independently (face color change doesn't re-upload vertex buffer).

---

## 4. Subcomponent Hierarchy (PLAN.md)

Support named sub-meshes/sub-graphs/sub-clouds as first-class components over a base geometry component.

- [ ] Define hierarchy node struct: `{NameId, BaseOffset(s), Size(s), Parent(optional)}` with domain-specific offsets (vertex/edge/halfedge/face ranges).
- [ ] `NameId` as `StringId`/hashed id for integration with selection, tooling, and material overrides.
- [ ] Renderables reference hierarchy slices so command building can issue draws for whole objects or named subcomponents.

---

## 5. Robustness & Numerical Safeguards (PLAN.md)

- [ ] Position sanitization: reject/skip non-finite positions (`NaN`, `Inf`) before upload in both retained and transient paths.
- [ ] Normal safety: renormalize with epsilon guard in point and surface shaders (fallback to camera-facing basis).
- [ ] Triangle degeneracy: skip zero-area triangles during edge extraction and surface rendering.
- [ ] Line width clamping: `[0.5, 32.0]` pixel range.
- [ ] Point radius clamping: `clamp(size, 0.0001, 1.0)` world-space.
- [ ] Zero-length graph edges: collapse to point primitive path.
- [ ] EWA covariance conditioning: eigenvalue floor + fallback to isotropic FlatDisc on ill-conditioned covariance.
- [ ] Depth conflicts: mode-specific depth bias for edge/point z-fighting against mesh surfaces.

---

## 6. Related Documents

- `PLAN.md` — detailed rendering architecture refactor spec (three-pass architecture, ECS component design, migration phases).
- `ROADMAP.md` — feature roadmap, prioritization phases, long-horizon planning, rendering modality redesign vision (§5), and architecture SLOs.
- `README.md` — architecture SLOs section with measurable SLO thresholds and telemetry milestones.
- `CLAUDE.md` — development conventions, C++23 adoption policy, architectural invariants.
