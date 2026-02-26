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

**Core principle:** One device-local vertex buffer on the GPU, multiple index buffers with different topologies referencing into it. A mesh entity uploads positions/normals once; all visualization modes are **views** sharing that same `std::shared_ptr<VulkanBuffer>` via `GeometryViewRenderer` + `ReuseVertexBuffersFrom`.

**Data sharing is via BDA (buffer device addresses), not VkVertexInputBinding.** The engine already uses BDA-based vertex pulling in `ForwardPass`: push constants carry `uint64_t` pointers to position/normal/aux SoA arrays, and the vertex shader reads via `GL_EXT_buffer_reference`. There are zero calls to `vkCmdBindVertexBuffers` in the codebase. Each topology view gets its **own VkPipeline** with its own vertex shader that reads from the shared buffer via BDA — the sharing is at the data level (same `VulkanBuffer`, same device address), not at the pipeline level.

| View | Pipeline | Vertex shader | Index buffer | Shared vertex buffer (BDA) |
|------|----------|--------------|-------------|---------------------------|
| Surface mesh | `ForwardPass` | `triangle.vert` — BDA pull `positions[gl_VertexIndex]` | Triangle indices (original) | Positions, normals, aux |
| Wireframe | `LineRenderPass` (retained) | `line.vert` — BDA pull `positions[edgeIndices[lineID*2+0/1]]`, expand to screen-space quad (6 verts/segment) | Unique edge pairs (extracted once, persisted) | Same buffer, same device address |
| Vertex visualization | `PointCloudRenderPass` | `point.vert` — BDA pull `positions[pointID]`, expand to billboard quad (6 verts/point) | Identity / direct draw | Same buffer, same device address |
| kNN graph | `LineRenderPass` (retained) | Same line shader | Neighbor edge pairs (separate buffer) | Same buffer, same device address |

Zero vertex duplication. Each topology needs separate shader pipelines because thick lines and billboard points require vertex-shader expansion (6 verts/primitive) — `VK_PRIMITIVE_TOPOLOGY_LINE_LIST` and `POINT_LIST` only produce 1px primitives without expansion. The `GeometryPool`/`GPUScene` retained-mode slot system is topology-agnostic — each view gets its own `GeometryHandle`, `GPUScene` slot, and participates in frustum culling independently.

**Line/graph rendering (Tier 1 — retained-mode, first-class):**
- [ ] Wireframe view: when a mesh is loaded, extract unique edges once → create a `GeometryViewRenderer` with `ReuseVertexBuffersFrom = meshHandle`, edge index buffer. Own `VkPipeline` with line vertex shader that reads positions from the mesh's vertex buffer via BDA, expands each segment to a screen-space quad. Persisted alongside the mesh entity.
- [ ] Vertex view: same shared-buffer pattern. Own `VkPipeline` with point vertex shader that reads positions via BDA, expands each point to a billboard quad.
- [ ] `GraphRenderer::Component`: wraps `Geometry::Graph` as a line view (edges) + point view (nodes) sharing the same vertex buffer. Layout algorithms produce positions uploaded once; updated only on layout change.
- [ ] Thick-line shader (`line.vert`): receives BDA pointer to shared position buffer + edge index buffer via push constants or SSBO. Vertex shader expands each line segment into a screen-space quad (6 verts/segment), fragment shader anti-aliases via signed-distance smoothstep. Push constants: line width, viewport dimensions, BDA pointers.
- [ ] Billboard point shader (`point.vert`): receives BDA pointer to shared position buffer. Vertex shader expands each point to a billboard quad (6 verts/point). Fragment shader: disc/surfel/EWA modes.
- [ ] Lifecycle system: same path as `MeshRendererLifecycle` — `GPUScene` slot allocation, transform sync, frustum culling.

**Debug overlay (Tier 2 — transient, DebugDraw only):**
- [ ] `DebugDraw` immediate-mode accumulator remains for genuinely transient visualization (octree, KD-tree, bounds, contact manifolds, convex hulls).
- [ ] `LineRenderPass` renders `DebugDraw` content via per-frame host-visible SSBO. Depth-tested + overlay sub-passes.
- [ ] Verify all debug overlays render correctly once `LineRenderPass` is restored.

### 1.2 Point Cloud Rendering — First-Class Retained-Mode (Broken, Re-architect)

Point clouds follow the same BDA shared-buffer philosophy. A point cloud entity uploads positions/normals once to a persistent device-local buffer (with `VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT`). If a mesh already has those positions on the GPU, the point cloud view can `ReuseVertexBuffersFrom` the mesh handle — the point cloud vertex shader reads from the mesh's buffer via the same BDA pointer.

**Retained-mode point cloud entities:**
- [ ] `PointCloudRenderer::Component` holds `GeometryHandle` pointing to `GeometryGpuData` with `PrimitiveTopology::Points`. GPU point layout: 32 bytes `{vec3 pos, float size, vec3 normal, uint packedColor}`.
- [ ] For mesh-derived vertex visualization: `ReuseVertexBuffersFrom = meshHandle` — zero additional vertex upload, shared BDA pointer to same device-local buffer.
- [ ] For standalone point clouds (`.xyz`, `.pcd`, `.ply`): upload positions/normals once via `GeometryUploadRequest` → `GeometryGpuData::CreateAsync()` → device-local with `SHADER_DEVICE_ADDRESS_BIT`. No per-frame re-upload.
- [ ] Lifecycle system: `GPUScene` slot allocation, transform sync, frustum culling — same path as meshes.
- [ ] Own `VkPipeline` with point vertex shader (`point.vert`): receives BDA pointer to position buffer via push constants. Billboard expansion (6 verts/point, no geometry shader). `VK_PRIMITIVE_TOPOLOGY_POINT_LIST` alone gives 1px dots — expansion shader required.
- [ ] Fragment shader: mode-separated — flat disc (unlit), surfel (Lambertian + ambient), EWA splatting (Zwicker et al. 2001).
- [ ] Pipeline registration in `DefaultPipeline`, gated by `FeatureRegistry`.
- [ ] Test: GPU point data layout, color packing, BDA shared-buffer lifecycle, render contract.

## 2. Related Documents

- `ROADMAP.md` — feature roadmap, prioritization phases, and long-horizon planning details.
- `docs/ARCHITECTURE_SLOS.md` — measurable SLO thresholds and telemetry milestones for orchestration and scheduler health.
