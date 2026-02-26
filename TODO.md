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

### 1.1 Line/Graph Rendering — First-Class Retained-Mode (Broken, Re-architect)

Lines and graphs must be **first-class retained-mode renderables** — the same architectural tier as triangle meshes. Not transient debug overlays rebuilt every frame, but persistent GPU-resident entities with their own ECS components, `GeometryGpuData` buffers, `GPUScene` slots, and lifecycle systems.

The existing infrastructure already supports this: `PrimitiveTopology::Lines` exists, `GeometryViewRenderer::Component` provides the "view" pattern (shared vertex buffer, independent index buffer with different topology), and the `GeometryPool`/`GPUScene` retained-mode slot system is topology-agnostic.

**Two rendering tiers:**

**Tier 1 — Retained-mode line entities** (first-class, like meshes):
- [ ] `LineRenderer::Component` ECS component: holds `GeometryHandle` pointing to a `GeometryGpuData` with `PrimitiveTopology::Lines`, persistent GPU buffers (device-local SSBO or vertex buffer via `GeometryUploadRequest`).
- [ ] Lifecycle system: allocates `GPUScene` slots, syncs transforms, participates in frustum culling — same path as `MeshRendererLifecycle`.
- [ ] `ForwardPass` already collects `GeometryViewRenderer::Component` with `PrimitiveTopology::Lines` — verify this path renders correctly with thick-line vertex shader expansion.
- [ ] Thick-line shader: vertex shader expands each line segment into a screen-space quad (6 verts/segment), fragment shader anti-aliases via signed-distance smoothstep. Push constants: line width, viewport dimensions.
- [ ] `GraphRenderer::Component` ECS component: wraps a `Geometry::Graph` as a line entity (edges) + optional point entity (nodes). CPU-side layout algorithms (`ComputeForceDirectedLayout`, `ComputeSpectralLayout`, `ComputeHierarchicalLayout`) produce positions; GPU buffers are uploaded once and updated only on layout change.
- [ ] Wireframe as a "view": mesh wireframe overlay uses `GeometryViewRenderer` with `ReuseVertexBuffersFrom` (shared vertex buffer) + edge index buffer + `PrimitiveTopology::Lines`. No per-frame CPU edge extraction — the edge indices are computed once and persisted.

**Tier 2 — Transient debug overlay** (DebugDraw, rebuilt per frame):
- [ ] `DebugDraw` immediate-mode accumulator remains for debug visualization (octree, KD-tree, bounds, contact manifolds, convex hulls) — these are genuinely transient.
- [ ] `LineRenderPass` renders `DebugDraw` content via per-frame host-visible SSBO upload. This is the only per-frame path — everything else is retained.
- [ ] Depth-tested + overlay sub-passes (two draw calls: one with depth test, one without).
- [ ] Verify all debug overlays render correctly once `LineRenderPass` is restored.

### 1.2 Point Cloud Rendering — First-Class Retained-Mode (Broken, Re-architect)

Point clouds must be **first-class retained-mode renderables** — same tier as meshes and line entities.

**Retained-mode point cloud entities:**
- [ ] `PointCloudRenderer::Component` ECS component: holds `GeometryHandle` pointing to `GeometryGpuData` with `PrimitiveTopology::Points`, persistent device-local GPU buffers. GPU point layout: 32 bytes `{vec3 pos, float size, vec3 normal, uint packedColor}`.
- [ ] Lifecycle system: allocates `GPUScene` slots, syncs transforms, participates in frustum culling — same path as `MeshRendererLifecycle`.
- [ ] Geometry upload via `GeometryUploadRequest` → `GeometryGpuData::CreateAsync()` → async transfer to device-local memory. No per-frame re-upload for static clouds.
- [ ] Vertex shader: billboard expansion (6 verts/point, no geometry shader).
- [ ] Fragment shader: mode-separated rendering — flat disc (unlit), surfel (Lambertian + ambient), EWA splatting (Zwicker et al. 2001, perspective-correct Gaussian elliptical splats).
- [ ] Pipeline registration in `DefaultPipeline`, gated by `FeatureRegistry`.
- [ ] Test: GPU point data layout, color packing, retained buffer lifecycle, render contract.

## 2. Related Documents

- `ROADMAP.md` — feature roadmap, prioritization phases, and long-horizon planning details.
- `docs/ARCHITECTURE_SLOS.md` — measurable SLO thresholds and telemetry milestones for orchestration and scheduler health.
