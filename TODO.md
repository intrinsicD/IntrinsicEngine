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
| Wireframe | `RetainedLineRenderPass` | `line_retained.vert` — BDA pull `positions[edgePairs[segID].i0/i1]`, expand to screen-space quad (6 verts/segment) | Persistent per-entity edge buffer (BDA, uploaded once) | Same buffer, same device address |
| Vertex visualization | `RetainedPointCloudRenderPass` | `point_retained.vert` — BDA pull `positions[pointID]`, expand to billboard quad (6 verts/point) | Direct draw (vertexCount * 6) | Same buffer, same device address |
| kNN graph | `LineRenderPass` (transient) | `line.vert` — SSBO line segments | Per-frame SSBO | DebugDraw accumulator |

Zero vertex duplication. Each topology needs separate shader pipelines because thick lines and billboard points require vertex-shader expansion (6 verts/primitive) — `VK_PRIMITIVE_TOPOLOGY_LINE_LIST` and `POINT_LIST` only produce 1px primitives without expansion.

**Completed (retained-mode BDA path):**
- `RetainedLineRenderPass` — iterates mesh entities with `ShowWireframe` + valid `GeometryGpuData`, reads positions via BDA, creates persistent per-entity edge buffers from `CachedEdges` (uploaded once, reused each frame via BDA). Expands to screen-space quads. Anti-aliased via `line_retained.frag`. Also iterates `ECS::Graph::Data` entities with valid `GpuGeometry` for graph edge rendering. Edge buffers are recreated automatically when edge count changes (e.g. graph re-layout). Orphaned buffers cleaned up via deferred GPU-safe destruction.
- `RetainedPointCloudRenderPass` — iterates mesh entities with `ShowVertices` + valid `GeometryGpuData`, reads positions/normals via BDA, expands to billboard quads. Supports FlatDisc and Surfel modes via `point_retained.frag`. Also iterates `ECS::Graph::Data` entities for graph node rendering.
- `MeshRenderPass` edge caching decoupled from DebugDraw submission — `CachedEdges` is always populated when `ShowWireframe=true`, shared between CPU and retained GPU paths.
- `GraphGeometrySyncSystem` — uploads graph node positions to device-local vertex buffer (Direct mode, CPU_TO_GPU), extracts edge pairs from graph topology with vertex compaction/remapping. Both retained passes read from the shared buffer via BDA. `GraphRenderPass` skips entities with valid `GpuGeometry` when retained passes are active (no double-draw).
- Both passes registered in `DefaultPipeline` render path (stages 6a/6b), gated by `FeatureRegistry`.
- Shader compilation auto-discovered via `CompileShaders.cmake` glob.
- `GeometryViewRenderer::Component::WireframeEdgeCount` tracks persistent edge buffer lifecycle.

**Remaining work:**
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

### 1.3 Per-Edge and Per-Face Attribute Rendering

The rendering plan requires per-element attribute data from PropertySets flowing to GPU BDA channels, not just uniform colors via push constants.

**Per-edge attributes (LinePass):**
- [ ] Add `PtrEdgeAux` BDA channel to `LinePushConstants` for per-edge colors/widths from edge PropertySets.
- [ ] Shader support: `line.vert`/`line.frag` read per-edge color/width from `PtrEdgeAux` when `Flags` indicate per-edge mode.
- [ ] Edge attribute buffer upload in `LinePass` from `Mesh::EdgeProperties()` or `Graph::GetOrAddEdgeProperty()` spans.

**Per-face attributes (SurfacePass):**
- [ ] Add per-face attribute buffer support in `SurfacePass` for flat shading, curvature visualization, segmentation labels.
- [ ] Shader support: `surface.frag` reads per-face color via `gl_PrimitiveID` indexing into face attribute BDA channel.
- [ ] Face attribute buffer upload from `Mesh::FaceProperties()` spans.

### 1.4 Geometry View Lifecycle Systems

Automated creation/destruction of GPU geometry views when rendering components are attached/detached. Applies equally to all three geometry types.

- [ ] `MeshViewLifecycleSystem`: on `ECS::Line::Component` attach to mesh entity → extract edge pairs from `Mesh::EdgeProperties()`, create edge index buffer via `ReuseVertexBuffersFrom(meshHandle)`, assign to `Line::Component::EdgeView`. On `ECS::Point::Component` attach → create vertex view via `ReuseVertexBuffersFrom`. On detach → release handle, free `GPUScene` slot.
- [ ] `GraphGeometrySyncSystem` enhancements: current system uploads positions and edge pairs on `GpuDirty=true` via Direct mode. Remaining: staged (device-local) upload for large static graphs, `GPUScene` slot allocation for frustum culling, per-node attribute BDA channels (colors, radii).
- [ ] `PointCloudGeometrySyncSystem`: on `ECS::Point::Component` attach with `PointCloud::Cloud` source → upload `Cloud::Positions()`/`Normals()` spans to device-local `GeometryGpuData`, assign handle.
- [ ] All lifecycle systems allocate `GPUScene` slots, sync transforms, participate in frustum culling — same contract as `MeshRendererLifecycle`.

### 1.5 PropertySet Dirty-Domain Sync System

Per-frame CPU→GPU synchronization driven by PropertySet change detection, with independent dirty tracking per data domain (vertex/edge/face).

- [ ] Define dirty tag components: `VertexPositionsDirty`, `VertexAttributesDirty`, `EdgeTopologyDirty`, `EdgeAttributesDirty`, `FaceTopologyDirty`, `FaceAttributesDirty`.
- [ ] Sync system detects dirty tags, re-uploads only affected PropertySet spans to GPU buffers.
- [ ] Topology-dirty domains trigger index buffer rebuild; attribute-dirty domains trigger attribute buffer re-upload.
- [ ] Clear dirty tags after upload. Multiple simultaneous dirty domains handled independently (face color change doesn't re-upload vertex buffer).

## 2. Related Documents

- `ROADMAP.md` — feature roadmap, prioritization phases, and long-horizon planning details.
- `docs/ARCHITECTURE_SLOS.md` — measurable SLO thresholds and telemetry milestones for orchestration and scheduler health.
