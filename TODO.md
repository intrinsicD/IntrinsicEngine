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

### 1.1 Per-Pass Typed ECS Components (PLAN.md Phase 1)

- [ ] Define `ECS::Surface::Component` (mirrors `MeshRenderer::Component` initially).
- [ ] Define `ECS::Line::Component` (wireframe settings; edge data from PropertySets, not pass-local cache).
- [ ] Define `ECS::Point::Component` (vertex/node/point cloud settings with `PointRenderMode`).
- [ ] Migration system: attach new components alongside old ones during transition period.

### 1.2 SurfacePass — Rename + Consolidate (PLAN.md Phase 2)

- [ ] Rename `ForwardPass` → `SurfacePass` (class, files, module partition `Graphics:Passes.Surface`).
- [ ] Rename shaders `triangle.vert/frag` → `surface.vert/frag`.
- [ ] Add `SubmitTriangles()` / `ResetTransient()` transient API on SurfacePass.
- [ ] Add `GetTriangles()` to `DebugDraw` for transient surface primitives.
- [ ] SurfacePass queries `ECS::Surface::Component` instead of `MeshRenderer::Component`.
- [ ] Migrate `GPUSceneSync` to use `ECS::Surface::Component`.
- [ ] Update `DefaultPipeline`, `FeatureRegistry`, `ShaderRegistry`, `CMakeLists.txt`.

### 1.3 LinePass — Consolidate All Line/Edge Sources (PLAN.md Phase 3)

- [ ] Delete old transient `line.vert/frag` (SSBO version) FIRST (name collision with retained rename target).
- [ ] Rename `RetainedLineRenderPass` → `LinePass`, rename `line_retained.vert/frag` → `line.vert/frag`.
- [ ] Add edge view creation: extract edge pairs from `Halfedge::Mesh` / `Graph` PropertySets, upload as edge index buffer via `ReuseVertexBuffersFrom()`.
- [ ] Add `ECS::Line::Component` iteration (replaces `RenderVisualization::ShowWireframe` boolean).
- [ ] Add graph edge iteration (replaces `GraphRenderPass` edge submission).
- [ ] LinePass reads `ctx.DebugDraw->GetLines()/GetOverlayLines()` for transient data.
- [ ] Add per-edge attribute BDA channel (`PtrEdgeAux`) for per-edge colors/widths from edge PropertySets.
- [ ] Delete wireframe code from `MeshRenderPass`.
- [ ] Delete edge submission from `GraphRenderPass`.
- [ ] Delete old `LineRenderPass` (transient-only pass) and `RetainedLineRenderPass` files (already renamed).

### 1.4 PointPass — Consolidate All Point Sources (PLAN.md Phase 4)

- [ ] Rename `RetainedPointCloudRenderPass` → `PointPass`.
- [ ] Split `point_retained.vert/frag` into `point_flatdisc.vert/frag` and `point_surfel.vert/frag`.
- [ ] PointPass stores pipeline array indexed by `PointRenderMode`.
- [ ] Add `ECS::Point::Component` iteration (replaces `RenderVisualization::ShowVertices` boolean).
- [ ] Add graph node iteration (replaces `GraphRenderPass` node submission).
- [ ] Add standalone point cloud iteration (replaces `PointCloudRenderPass`).
- [ ] Add `GetPoints()` to `DebugDraw` for transient point markers.
- [ ] PointPass reads `ctx.DebugDraw->GetPoints()` for transient data.
- [ ] Support per-point attributes (colors, radii, normals) from point PropertySets via `PtrAux` BDA channel.
- [ ] Delete vertex/node code from `MeshRenderPass`, `GraphRenderPass`, `PointCloudRenderPass`.
- [ ] Delete `RetainedPointCloudRenderPass` files (already renamed) and old transient `point.vert/frag`.

### 1.5 Delete Dead Code (PLAN.md Phase 5)

- [ ] Delete `MeshRenderPass` (class, files, module partition).
- [ ] Delete `GraphRenderPass` (class, files, module partition).
- [ ] Delete `PointCloudRenderPass` (class, files, module partition).
- [ ] Delete `RenderVisualization::Component` (replaced by typed components).
- [ ] Delete `GeometryViewRenderer::Component` (each component carries its own handle).
- [ ] Delete `MeshRenderer::Component` (aliased to `Surface::Component` during transition).
- [ ] Remove `VisualizationCollect` composite stage from `DefaultPipeline`.

### 1.6 Adapt Geometry View Lifecycle Systems (PLAN.md Phase 6)

Lifecycle systems already exist (`MeshViewLifecycleSystem`, `GraphGeometrySyncSystem`, `PointCloudGeometrySyncSystem`). They must be migrated to populate the new `ECS::Surface/Line/Point::Component` types and work with the unified pass architecture.

- [ ] Migrate `MeshViewLifecycleSystem` to create/populate `ECS::Line::Component` and `ECS::Point::Component` on attach/detach (replaces current `MeshEdgeView`/`MeshVertexView` intermediaries).
- [ ] Migrate `GraphGeometrySyncSystem` to populate sibling `ECS::Line::Component` + `ECS::Point::Component` handles (instead of relying on pass-specific iteration).
- [ ] Migrate `PointCloudGeometrySyncSystem` to populate `ECS::Point::Component` handle (instead of `PointCloudRenderer::Component`).
- [ ] All three systems allocate `GPUScene` slots, sync transforms, frustum culling — same path as `MeshRendererLifecycle`.

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
- `ROADMAP.md` — feature roadmap, prioritization phases, and long-horizon planning (rendering modes, post-processing, shadows, selection, UI, I/O, geometry operators, benchmarking).
- `docs/ARCHITECTURE_SLOS.md` — measurable SLO thresholds and telemetry milestones for orchestration and scheduler health.
- `docs/RENDERING_MODALITY_REDESIGN_PLAN.md` — long-term approach/mode framework that the three-pass refactor enables.
