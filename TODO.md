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

### 1.1 Shared-Buffer Multi-Topology Rendering

**Core principle:** One device-local vertex buffer on the GPU, multiple index buffers with different topologies referencing into it. A mesh entity uploads positions/normals once; all visualization modes are **views** sharing that same `std::shared_ptr<VulkanBuffer>` via `GeometryViewRenderer` + `ReuseVertexBuffersFrom`.

**Data sharing is via BDA (buffer device addresses), not VkVertexInputBinding.** The engine already uses BDA-based vertex pulling in `ForwardPass`: push constants carry `uint64_t` pointers to position/normal/aux SoA arrays, and the vertex shader reads via `GL_EXT_buffer_reference`. There are zero calls to `vkCmdBindVertexBuffers` in the codebase. Each topology view gets its **own VkPipeline** with its own vertex shader that reads from the shared buffer via BDA — the sharing is at the data level (same `VulkanBuffer`, same device address), not at the pipeline level.

| View | Pipeline | Vertex shader | Index buffer | Shared vertex buffer (BDA) |
|------|----------|--------------|-------------|---------------------------|
| Surface mesh | `ForwardPass` | `triangle.vert` — BDA pull `positions[gl_VertexIndex]` | Triangle indices (original) | Positions, normals, aux |
| Wireframe | `RetainedLineRenderPass` | `line_retained.vert` — BDA pull `positions[edgePairs[segID].i0/i1]`, expand to screen-space quad (6 verts/segment) | Unique edge pairs (per-frame SSBO from CachedEdges) | Same buffer, same device address |
| Vertex visualization | `RetainedPointCloudRenderPass` | `point_retained.vert` — BDA pull `positions[pointID]`, expand to billboard quad (6 verts/point) | Direct draw (vertexCount * 6) | Same buffer, same device address |
| kNN graph | `LineRenderPass` (transient) | `line.vert` — SSBO line segments | Per-frame SSBO | DebugDraw accumulator |

Zero vertex duplication. Each topology needs separate shader pipelines because thick lines and billboard points require vertex-shader expansion (6 verts/primitive) — `VK_PRIMITIVE_TOPOLOGY_LINE_LIST` and `POINT_LIST` only produce 1px primitives without expansion.

**Completed (retained-mode BDA path):**
- `RetainedLineRenderPass` — iterates mesh entities with `ShowWireframe` + valid `GeometryGpuData`, reads positions via BDA, uploads edge pairs from `CachedEdges` to per-frame SSBO, expands to screen-space quads. Anti-aliased via `line_retained.frag`.
- `RetainedPointCloudRenderPass` — iterates mesh entities with `ShowVertices` + valid `GeometryGpuData`, reads positions/normals via BDA, expands to billboard quads. Supports FlatDisc and Surfel modes via `point_retained.frag`.
- `MeshRenderPass` edge caching decoupled from DebugDraw submission — `CachedEdges` is always populated when `ShowWireframe=true`, shared between CPU and retained GPU paths.
- Both passes registered in `DefaultPipeline` render path (stages 6a/6b), gated by `FeatureRegistry`.
- Shader compilation auto-discovered via `CompileShaders.cmake` glob.

**Remaining work:**
- [ ] `GraphRenderer::Component`: wraps `Geometry::Graph` as a retained line view (edges) + point view (nodes) sharing the same vertex buffer. Layout algorithms produce positions uploaded once; updated only on layout change.
- [ ] Fully retained edge SSBO: upload edge pairs once to device-local storage when the mesh loads (currently re-uploaded per-frame from `CachedEdges`). Requires `GeometryViewRenderer` wireframe handle + lifecycle management.
- [ ] Frustum culling integration: retained line/point views should participate in `GPUScene` slot-based frustum culling alongside surface meshes.
- [ ] EWA splatting mode (Zwicker et al. 2001) for point clouds.
- [ ] Test: GPU point data layout, color packing, BDA shared-buffer lifecycle, render contract.

**Debug overlay (Tier 2 — transient, DebugDraw only):**
- [ ] `DebugDraw` immediate-mode accumulator remains for genuinely transient visualization (octree, KD-tree, bounds, contact manifolds, convex hulls).
- [ ] `LineRenderPass` renders `DebugDraw` content via per-frame host-visible SSBO. Depth-tested + overlay sub-passes.

### 1.2 Point Cloud Rendering — Standalone Retained-Mode

Standalone point clouds (`.xyz`, `.pcd`, `.ply`) that arrive without an existing mesh vertex buffer still need their own device-local upload path.

- [ ] `PointCloudRenderer::Component` holds `GeometryHandle` pointing to `GeometryGpuData` with `PrimitiveTopology::Points`. Upload positions/normals once via `GeometryUploadRequest` → `GeometryGpuData::CreateAsync()` → device-local with `SHADER_DEVICE_ADDRESS_BIT`.
- [ ] For mesh-derived vertex visualization: `ReuseVertexBuffersFrom = meshHandle` — zero additional vertex upload, shared BDA pointer to same device-local buffer.  *(Currently handled by `RetainedPointCloudRenderPass` reading directly from mesh geometry.)*
- [ ] Lifecycle system: `GPUScene` slot allocation, transform sync, frustum culling — same path as meshes.
- [ ] Pipeline registration in `DefaultPipeline`, gated by `FeatureRegistry`.

## 2. Related Documents

- `ROADMAP.md` — feature roadmap, prioritization phases, and long-horizon planning details.
- `docs/ARCHITECTURE_SLOS.md` — measurable SLO thresholds and telemetry milestones for orchestration and scheduler health.
