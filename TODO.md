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

### 1.1 Shared-Buffer Multi-Topology Rendering (Broken, Re-architect)

**Core principle:** One vertex buffer on the GPU, multiple index buffers with different topologies referencing into it. A mesh entity uploads positions/normals once, then all visualization modes are **views** sharing that same `std::shared_ptr<VulkanBuffer>` via `GeometryViewRenderer` + `ReuseVertexBuffersFrom`:

| View | Topology | Index buffer | Shared vertex buffer |
|------|----------|-------------|---------------------|
| Surface mesh | `PrimitiveTopology::Triangles` | Triangle indices (original) | Positions, normals, aux |
| Wireframe | `PrimitiveTopology::Lines` | Unique edge pairs (extracted once, persisted) | Same |
| Vertex visualization | `PrimitiveTopology::Points` | Trivial identity or direct draw | Same |
| kNN graph | `PrimitiveTopology::Lines` | Neighbor edge pairs (separate buffer) | Same |

Zero vertex duplication. The `GeometryPool`/`GPUScene` retained-mode slot system is topology-agnostic — each view gets its own `GeometryHandle`, `GPUScene` slot, and participates in frustum culling independently.

**Line/graph rendering (Tier 1 — retained-mode, first-class):**
- [ ] Wireframe view: when a mesh is loaded, extract unique edges once → create a `GeometryViewRenderer` with `ReuseVertexBuffersFrom = meshHandle`, `PrimitiveTopology::Lines`, edge index buffer. Persisted alongside the mesh entity.
- [ ] Vertex view: same pattern with `PrimitiveTopology::Points`, trivial index buffer or vertex count direct draw.
- [ ] `GraphRenderer::Component`: wraps `Geometry::Graph` as a line view (edges) + point view (nodes) sharing the same vertex buffer. Layout algorithms produce positions uploaded once; updated only on layout change.
- [ ] Thick-line shader: vertex shader expands each line segment into a screen-space quad (6 verts/segment), fragment shader anti-aliases via signed-distance smoothstep. Push constants: line width, viewport dimensions.
- [ ] Lifecycle system: same path as `MeshRendererLifecycle` — `GPUScene` slot allocation, transform sync, frustum culling.

**Debug overlay (Tier 2 — transient, DebugDraw only):**
- [ ] `DebugDraw` immediate-mode accumulator remains for genuinely transient visualization (octree, KD-tree, bounds, contact manifolds, convex hulls).
- [ ] `LineRenderPass` renders `DebugDraw` content via per-frame host-visible SSBO. Depth-tested + overlay sub-passes.
- [ ] Verify all debug overlays render correctly once `LineRenderPass` is restored.

### 1.2 Point Cloud Rendering — First-Class Retained-Mode (Broken, Re-architect)

Point clouds follow the same shared-buffer philosophy. A point cloud entity uploads positions/normals once to a persistent device-local buffer. If a mesh already has those positions on the GPU, the point cloud view can `ReuseVertexBuffersFrom` the mesh handle.

**Retained-mode point cloud entities:**
- [ ] `PointCloudRenderer::Component` holds `GeometryHandle` pointing to `GeometryGpuData` with `PrimitiveTopology::Points`. GPU point layout: 32 bytes `{vec3 pos, float size, vec3 normal, uint packedColor}`.
- [ ] For mesh-derived vertex visualization: `ReuseVertexBuffersFrom = meshHandle` — zero additional vertex upload.
- [ ] For standalone point clouds (`.xyz`, `.pcd`, `.ply`): upload positions/normals once via `GeometryUploadRequest` → `GeometryGpuData::CreateAsync()` → device-local. No per-frame re-upload.
- [ ] Lifecycle system: `GPUScene` slot allocation, transform sync, frustum culling — same path as meshes.
- [ ] Vertex shader: billboard expansion (6 verts/point, no geometry shader).
- [ ] Fragment shader: mode-separated — flat disc (unlit), surfel (Lambertian + ambient), EWA splatting (Zwicker et al. 2001).
- [ ] Pipeline registration in `DefaultPipeline`, gated by `FeatureRegistry`.
- [ ] Test: GPU point data layout, color packing, shared-buffer lifecycle, render contract.

## 2. Related Documents

- `ROADMAP.md` — feature roadmap, prioritization phases, and long-horizon planning details.
- `docs/ARCHITECTURE_SLOS.md` — measurable SLO thresholds and telemetry milestones for orchestration and scheduler health.
