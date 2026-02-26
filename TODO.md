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

### 1.1 Line Rendering Re-implementation (Broken)

`LineRenderPass` and `DebugDraw` rendering are non-functional. Re-implement from scratch:

- [ ] Robust `LineRenderPass` with SSBO-based thick-line expansion in vertex shader (6 verts/segment).
- [ ] Per-frame host-visible SSBO upload with proper lifecycle (staging belt or persistent mapped buffer).
- [ ] Anti-aliased edge rendering in fragment shader via signed-distance smoothstep.
- [ ] Depth-tested + overlay sub-passes (two draw calls per frame: one with depth test, one without).
- [ ] Push constants: line width, viewport dimensions.
- [ ] Descriptor set binding: `set = 0` global camera UBO, `set = 1` line SSBO.
- [ ] Pipeline registration in `DefaultPipeline`, gated by `FeatureRegistry`.
- [ ] Verify all `DebugDraw` overlays (octree, KD-tree, bounds, contact manifolds, convex hulls) render correctly once line rendering is restored.

### 1.2 Wireframe Rendering Re-implementation (Broken)

CPU-driven wireframe rendering (mesh edges → `DebugDraw` → `LineRenderPass`) is broken because it depends on `LineRenderPass`:

- [ ] Restore wireframe edge extraction in `MeshRenderPass` → `DebugDraw` submission.
- [ ] Verify overlay vs. depth-tested routing via `WireframeOverlay` flag.
- [ ] Test with various mesh topologies (triangles, quads, mixed polygons).

### 1.3 Point Cloud Rendering Re-implementation (Broken)

`PointCloudRenderPass` is non-functional. Re-implement from scratch:

- [ ] Robust `PointCloudRenderPass` as `IRenderFeature`, following the (restored) `LineRenderPass` SSBO pattern.
- [ ] Vertex shader: billboard expansion (6 verts/point, no geometry shader), 32-byte GPU point layout `{vec3 pos, float size, vec3 normal, uint packedColor}`.
- [ ] Fragment shader: mode-separated rendering — flat disc (unlit), surfel (Lambertian), EWA splatting (Zwicker et al. 2001).
- [ ] Clean `PointCloudRenderer::Component` ECS integration with proper GPU data upload lifecycle.
- [ ] Pipeline registration in `DefaultPipeline`, gated by `FeatureRegistry`.
- [ ] Test: GPU point data layout, color packing, staging buffer, render contract.

## 2. Related Documents

- `ROADMAP.md` — feature roadmap, prioritization phases, and long-horizon planning details.
- `docs/ARCHITECTURE_SLOS.md` — measurable SLO thresholds and telemetry milestones for orchestration and scheduler health.
