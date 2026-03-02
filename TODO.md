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
- `RetainedPointCloudRenderPass` — iterates mesh entities with `ShowVertices` + valid `GeometryGpuData`, reads positions/normals via BDA, expands to billboard quads. Supports FlatDisc, Surfel, and EWA splatting modes via `point_retained.frag`. EWA mode (Zwicker et al. 2001) computes per-surfel perspective Jacobian in the vertex shader, producing perspective-correct elliptical Gaussian splats with a 1px² low-pass anti-aliasing filter. Also iterates `ECS::Graph::Data` entities for graph node rendering.
- `MeshRenderPass` edge caching decoupled from DebugDraw submission — `CachedEdges` is always populated when `ShowWireframe=true`, shared between CPU and retained GPU paths.
- `GraphGeometrySyncSystem` — uploads graph node positions to device-local vertex buffer (Direct mode, CPU_TO_GPU), extracts edge pairs from graph topology with vertex compaction/remapping. Both retained passes read from the shared buffer via BDA. `GraphRenderPass` skips entities with valid `GpuGeometry` when retained passes are active (no double-draw).
- Both passes registered in `DefaultPipeline` render path (stages 6a/6b), gated by `FeatureRegistry`.
- Shader compilation auto-discovered via `CompileShaders.cmake` glob.
- `GeometryViewRenderer::Component::WireframeEdgeCount` tracks persistent edge buffer lifecycle.
- CPU-side frustum culling in `RetainedLineRenderPass` and `RetainedPointCloudRenderPass` — extracts camera frustum from view-projection matrix, tests each entity's world-space bounding sphere via `Geometry::TestOverlap(Frustum, Sphere)`, skips draws for culled entities. Mirrors the GPU `instance_cull.comp` sphere-plane test. `GeometryGpuData` now stores precomputed local bounding spheres from CPU vertex positions at upload time (AABB-enclosing sphere), replacing the conservative 10,000-radius fallback in `MeshRendererLifecycle`. Respects `Debug.DisableCulling` toggle.
- `DebugDraw` immediate-mode accumulator for transient visualization (octree, KD-tree, bounds, contact manifolds, convex hulls). Depth-tested and overlay line APIs. Tested in `Test_DebugDraw.cpp`, `Test_BoundingDebugDraw.cpp`, `Test_OctreeDebugDraw.cpp`, `Test_KDTreeDebugDraw.cpp`, `Test_BVHDebugDraw.cpp`, `Test_ConvexHullDebugDraw.cpp`.
- `LineRenderPass` renders `DebugDraw` content via per-frame host-visible SSBO. Two sub-passes: depth-tested + overlay (no depth test). Registered in `DefaultPipeline` after visualization collection.
- GPU point data layout, color packing, BDA shared-buffer lifecycle, and render contract tests: `Test_PointCloudRenderPass.cpp`, `Test_RuntimeGeometry_Reuse.cpp`, `Test_ResourcePool.cpp`, `Test_BDASharedBufferContract.cpp`.
- **Standalone point cloud rendering (§1.2 → merged):** `PointCloudRenderer::Component` holds `GeometryHandle` for device-local GPU data. `PointCloudRendererLifecycle` system uploads CPU data once, allocates `GPUScene` slots, clears CPU vectors. `RetainedPointCloudRenderPass` iterates standalone point cloud entities alongside mesh vertex vis and graph nodes. `SceneManager::SpawnModel()` routes `PrimitiveTopology::Points` to `PointCloudRenderer::Component`. `GPUSceneSync` handles point cloud transform sync. Contract tests in `Test_PointCloudRendererLifecycle.cpp`.

**Status:** All retained-mode rendering items for §1.1 are complete (including standalone point cloud rendering).

### 1.2 Per-Edge and Per-Face Attribute Rendering

The rendering plan requires per-element attribute data from PropertySets flowing to GPU BDA channels, not just uniform colors via push constants.

**Per-edge attributes (LinePass):**
- [ ] Add `PtrEdgeAux` BDA channel to `LinePushConstants` for per-edge colors/widths from edge PropertySets.
- [ ] Shader support: `line.vert`/`line.frag` read per-edge color/width from `PtrEdgeAux` when `Flags` indicate per-edge mode.
- [ ] Edge attribute buffer upload in `LinePass` from `Mesh::EdgeProperties()` or `Graph::GetOrAddEdgeProperty()` spans.

**Per-face attributes (SurfacePass):**
- [ ] Add per-face attribute buffer support in `SurfacePass` for flat shading, curvature visualization, segmentation labels.
- [ ] Shader support: `surface.frag` reads per-face color via `gl_PrimitiveID` indexing into face attribute BDA channel.
- [ ] Face attribute buffer upload from `Mesh::FaceProperties()` spans.

### 1.3 Geometry View Lifecycle Systems

Automated creation/destruction of GPU geometry views when rendering components are attached/detached. Applies equally to all three geometry types.

- [ ] `MeshViewLifecycleSystem`: on `ECS::Line::Component` attach to mesh entity → extract edge pairs from `Mesh::EdgeProperties()`, create edge index buffer via `ReuseVertexBuffersFrom(meshHandle)`, assign to `Line::Component::EdgeView`. On `ECS::Point::Component` attach → create vertex view via `ReuseVertexBuffersFrom`. On detach → release handle, free `GPUScene` slot.
- [ ] `GraphGeometrySyncSystem` enhancements: current system uploads positions and edge pairs on `GpuDirty=true` via Direct mode. Remaining: staged (device-local) upload for large static graphs, `GPUScene` slot allocation for frustum culling, per-node attribute BDA channels (colors, radii).
- [ ] `PointCloudGeometrySyncSystem`: on `ECS::Point::Component` attach with `PointCloud::Cloud` source → upload `Cloud::Positions()`/`Normals()` spans to device-local `GeometryGpuData`, assign handle.
- [ ] All lifecycle systems allocate `GPUScene` slots, sync transforms, participate in frustum culling — same contract as `MeshRendererLifecycle`.

### 1.4 PropertySet Dirty-Domain Sync System

Per-frame CPU→GPU synchronization driven by PropertySet change detection, with independent dirty tracking per data domain (vertex/edge/face).

- [ ] Define dirty tag components: `VertexPositionsDirty`, `VertexAttributesDirty`, `EdgeTopologyDirty`, `EdgeAttributesDirty`, `FaceTopologyDirty`, `FaceAttributesDirty`.
- [ ] Sync system detects dirty tags, re-uploads only affected PropertySet spans to GPU buffers.
- [ ] Topology-dirty domains trigger index buffer rebuild; attribute-dirty domains trigger attribute buffer re-upload.
- [ ] Clear dirty tags after upload. Multiple simultaneous dirty domains handled independently (face color change doesn't re-upload vertex buffer).

## 2. Related Documents

- `ROADMAP.md` — feature roadmap, prioritization phases, and long-horizon planning details.
- `docs/ARCHITECTURE_SLOS.md` — measurable SLO thresholds and telemetry milestones for orchestration and scheduler health.
