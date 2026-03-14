# IntrinsicEngine — Architecture TODOs (Living Document)

This document tracks the **active rendering-architecture backlog** for IntrinsicEngine.

**Policy:** completed/refactored work does **not** stay here. We rely on Git history for the past and keep this file focused on what remains.

---

## 0. Scope & Success Criteria

**Current focus:** finish the resource-driven rendering refactor without painting the engine into a corner for deferred, hybrid, transparency, or material-system work.

**Success criteria:**

- Deterministic frame construction with explicit pass/resource dependencies.
- Canonical render targets with validated producer/consumer contracts.
- Selection, post, and debug visualization decoupled from a single lighting path.
- Only recipe-required intermediate resources allocated per frame.
- Migration hardened by graph compile tests, contract tests, and at least one integration test.

---

## 1. Related Documents

- `docs/architecture/rendering-three-pass.md` — canonical runtime rendering architecture spec (pass contracts, data contracts, invariants).
- `PLAN.md` — archival index for the completed three-pass migration.
- `ROADMAP.md` — medium/long-horizon feature roadmap and phase ordering.
- `README.md` — user-facing architecture summary, build/test entry points, and SLOs.
- `CLAUDE.md` — contributor conventions, C++23 policy, and markdown sync contract.
- `PATTERNS.md` — reusable patterns catalog with canonical examples and usage guidance.

---

## P0 — Critical Refactor

- [ ] Vertex Selection mode, Edge selection mode, and Face selection mode should all be independent of the lighting path. They should write to the same selection buffer format and be composited as a post-process outline regardless of forward/deferred/hybrid lighting decisions.
- [ ] Vertex Selection mode uses Sphere rendering with a red color to mark the selected vertices.
- [ ] Multi selection with shift key.
- [ ] Make the selection mode work with radio buttons (vertex, edge, face, entity).
- [ ] Wire Geodesic computation in the ui. When active, mark the selected vertex or vertices (with shift for multiple selection) with a different colored shpere.

### Core & RHI Code Quality (Audit Findings)

- [ ] Extract RHI SafeDestroy helper — 9 files repeat identical move-capture + lambda pattern for deferred Vulkan resource destruction (`RHI.Buffer.cpp`, `RHI.Shader.cpp`, `RHI.Image.cpp`, `RHI.Texture.cpp`, `RHI.Pipeline.cpp`, etc.). Create a template or macro in `RHI.DestructionUtils`.
- [ ] Standardize frame counter naming — `GlobalFrameNumber` (Device), `CurrentFrame` (Telemetry), `frameEpoch` (CommandContext), `currentFrameNumber` (ResourcePool) all refer to the same monotonic counter. Pick one name and unify.
- [ ] Document device reference lifetime contract — RHI classes inconsistently use `VulkanDevice&` (non-owning) vs `shared_ptr<VulkanDevice>` (shared ownership). Add a comment convention explaining when each is appropriate.
- [ ] Document Core.Memory `fprintf` vs `Core::Log` error reporting trade-off — Core uses `fprintf(stderr, ...)` to avoid circular dependency with Logging; Runtime modules use `Core::Log`. Add a rationale comment in `Core.Memory.cpp`.

## 2. Next (P1) — Near-Term Follow-Up After the Refactor Lands

These are not required to finish the first wave, but they should begin soon after P0 is stable.

### B0. RenderGraph Raster Packet Merging

Extend `RenderGraph::Packetize()` to merge consecutive raster passes that target the exact same color+depth attachments into a single execution packet (shared `vkCmdBeginRendering` scope). Currently only non-raster (compute/copy) passes are eligible for merging. Requires attachment equality checking and shared `VkRenderingInfo` construction for multi-pass raster packets.

### B1. Deferred Lighting Path

- [ ] Add a proper deferred lighting composition path.
- [ ] Define the minimum G-buffer layout.
- [ ] Route opaque lighting through the deferred path.
- [ ] Preserve the forward path for unsupported cases.
- [ ] Keep selection/debug independent of the deferred path.

### B2. Hybrid Renderer Support

- [ ] Support forward + deferred coexistence.
- [ ] Define which primitives/materials are deferred-capable.
- [ ] Keep transparent/special/debug rendering in the forward path.
- [ ] Define composition rules between paths.

### B3. Two-Stage GPU Selection Pipeline

Replace entity-only GPU pick with a dual-channel MRT pipeline producing both `EntityId` and `PrimitiveId` in one frame, enabling sub-entity (face/edge/point) selection.

#### Infrastructure (readback & events)
- [ ] Add `TRANSFER_SRC_BIT` to `PrimitiveId` resource definition (`Graphics.RenderPipeline.cppm`).
- [ ] Expand readback buffer from 4 → 8 bytes; add `PrimitiveID` to `PickResultGpu` (`Graphics.Interaction.cppm/cpp`).
- [ ] Add `PrimitiveID` to `GpuPickCompleted` event (`ECS.Components.Events.cppm`).
- [ ] Thread `PrimitiveID` through `RenderSystem` event enqueue and `SelectionModule` cached pick.
- [ ] Set `recipe.PrimitiveId = true` alongside `recipe.EntityId` in `BuildDefaultPipelineRecipe()`.

#### MRT picking pass (mesh)
- [ ] Create `pick_mesh.vert/frag` shaders with two MRT outputs (`outEntityID`, `outPrimitiveID = gl_PrimitiveID`).
- [ ] Define unified 104-byte `PickPushConsts` struct (Model, PtrPositions, PtrAux, EntityID, PrimitiveBase, PickWidth, Viewport, pad).
- [ ] Update PipelineLibrary mesh pick pipeline to `SetColorFormats({R32_UINT, R32_UINT})`.
- [ ] Update `PickingPass` to write both EntityId and PrimitiveId attachments; PickCopy reads both into readback buffer.

#### Graph edge picking (line pick pipeline)
- [ ] Create `pick_line.vert/frag` shaders adapted from `line.vert` with vertex-amplified quads, `PickPushConsts`, and segment index output.
- [ ] Add line pick pipeline to `PickingPass` (2× R32_UINT, cull none, depth bias -1/-1).
- [ ] Add draw loop for `Graph::Data + Line::Component` entities and standalone `Line::Component` entities.

#### Point cloud picking (point pick pipeline)
- [ ] Create `pick_point.vert/frag` shaders adapted from `point_flatdisc.vert` with billboard quads, disc discard test, and point index output.
- [ ] Add point pick pipeline to `PickingPass` (2× R32_UINT, cull none, depth bias -2/-2).
- [ ] Add draw loop for `PointCloud::Data + Point::Component` entities and standalone `Point::Component` entities.

#### Selection module integration
- [ ] Update `ApplyFromGpuPick()` to decode primitive meaning by entity kind (mesh → triangle, graph → edge, point cloud → point).
- [ ] Add local-space nearest-feature refinement using primitive ID (nearest edge/vertex for meshes).

---

## 3. Later (P2) — Planned Downstream Work

These items should be **planned now** so the current refactor leaves room for them, but they should be implemented later.

### C1. Material System Rewrite

- [ ] Define future material data-model requirements.
- [ ] Decide how materials map to:
  - [ ] Forward path.
  - [ ] Deferred path.
  - [ ] Hybrid path.
- [ ] Define canonical material parameter packing for rendering.
- [ ] Define CPU-side material representation vs. GPU packed representation.
- [ ] Plan material feature flags:
  - [ ] Opaque.
  - [ ] Masked.
  - [ ] Transparent.
  - [ ] Double-sided.
  - [ ] Emissive.
  - [ ] Special shading models.
- [ ] Plan material graph/node-system scope, or explicitly defer it.
- [ ] Plan shader permutation containment strategy.
- [ ] Plan texture binding model.
- [ ] Plan material debug visualization modes.

### C2. Transparency Architecture

- [ ] Plan the transparent rendering path.
- [ ] Decide whether transparency stays fully forward.
- [ ] Plan ordering/composition rules.
- [ ] Plan debug-view behavior for transparent objects.
- [ ] Plan interaction with selection outlines.

### C3. Lighting System Rewrite / Expansion

- [ ] Plan light data structures for forward+, deferred, and hybrid.
- [ ] Plan clustered/tiled light culling support.
- [ ] Plan shadow integration points.
- [ ] Plan emissive/material/light interaction.
- [ ] Plan debug views for lights and shading terms.

### C4. Visibility System Improvements

- [ ] Plan depth-prepass support.
- [ ] Plan a visibility buffer or material-classification path if desired later.
- [ ] Plan occlusion-culling integration.
- [ ] Plan GPU-driven pass compatibility with new graph stages.

### C5. Motion/Reprojection Support

- [ ] Plan velocity-buffer support.
- [ ] Plan history-resource ownership.
- [ ] Plan TAA integration points.
- [ ] Plan motion-blur integration points.
- [ ] Plan camera/object motion contracts.

### C6. Decals / SSAO / Screen-Space Effects

- [ ] Plan resource requirements for SSAO.
- [ ] Plan normal/depth usage for screen-space effects.
- [ ] Plan decal insertion points in the graph.
- [ ] Plan debug output for effect intermediates.

### C7. PostProcessPass Factoring

- [ ] Factor `PostProcessPass` into sub-pass classes (bloom, SMAA, tone mapping, histogram) to reduce single-class complexity.

### C9. Render Asset / Shader System Cleanup

- [ ] Plan shader registration refactor.
- [ ] Plan shader hot-reload boundaries by pass/stage.
- [ ] Plan permutation management.
- [ ] Plan shader feature-key derivation from material/frame recipe.
- [ ] Plan pipeline-cache invalidation strategy.

### C10. Scene Serialization Compatibility

- [ ] Ensure render settings serialize cleanly.
- [ ] Ensure frame-recipe-relevant settings are serializable where appropriate.
- [ ] Plan material serialization compatibility with the future rewrite.
- [ ] Plan debug/editor-only render state separation from scene state.

---

## 4. Planned Constraints — Design Now, Build Later

These are the explicit constraints agents must preserve during the refactor even when the underlying features are not being implemented yet.

- [ ] Leave room for a material-system rewrite.
- [ ] Leave room for transparency-path separation.
- [ ] Leave room for deferred and hybrid lighting.
- [ ] Leave room for motion vectors/history buffers.
- [ ] Leave room for clustered/tiled lighting.
- [ ] Leave room for effect passes like SSAO, decals, and bloom.
- [ ] Leave room for future GPU-driven visibility integration.

---

## 5. Sequencing & Definition of Done

### Sequencing Rules

- Finish P0 before starting P1 feature work unless the task is pure documentation, instrumentation, or test scaffolding that directly de-risks P0.
- Treat P2 as design pressure during P0, not as implementation scope creep.
- Keep selection/debug independent of the chosen lighting path.
- Do not allocate every buffer every frame unless required by the active `FrameRecipe`.
- Do not force full deferred shading everywhere yet.

### Cross-Cut Definition of Done

- [ ] Update `README.md` for each merged refactor milestone when user-facing architecture or workflow changes.
- [ ] Remove superseded code paths immediately (no compatibility clutter beyond staged migration windows).
- [ ] Add at least one integration test per milestone and wire it into the existing test targets.
- [ ] Add or update migration notes when render contracts change.

---

## 6. Codebase Cleanup & Refactor (Audit Findings)

Identified via full codebase sweep (March 2026). Grouped by priority.

### D1. Graph Property Extraction Regression Test (P2)

`Graphics.GraphPropertyHelpers.hpp` consolidates graph color/radius extraction. Both `GraphGeometrySync` and `PropertySetDirtySync` use the shared helpers. Remaining:

- [ ] Add one regression test covering deleted-vertex skipping + fallback property names across both call sites.

### D2. Finish Shared Pass Helper Adoption (P3)

`Graphics.PassUtils.hpp` covers shader-path resolution (`ResolveShaderPaths`) and non-owning `VulkanDevice` aliases (`MakeDeviceAlias`). `PostProcessPass` and all other passes now use these helpers. `PipelineLibrary.cpp` still uses raw `ResolveShaderPathOrExit` pairs because it resolves shaders without a `ShaderRegistry` — this is acceptable.

- [ ] Evaluate whether `PipelineLibrary` can adopt `ResolveShaderPaths` with an alternate resolver overload.
- [ ] Keep the refactor behavior-preserving and opportunistic when the file is next touched.

### D3. Vertex Deduplication Consolidation (P3)

STL uses spatial-hash vertex deduplication (quantize-based `VertexKey` + `VertexKeyHash`). OBJ uses index-based dedup (position/normal/texcoord index tuples). The two strategies are fundamentally different, but the spatial-hash pattern could be shared with future importers that also produce unindexed triangle soups.

- [ ] Extract STL's `VertexKey` / `VertexKeyHash` / dedup-map pattern into `Graphics.Importers.VertexDedup.hpp`.
- [ ] Add a unit test for the spatial deduplicator (coincident vertices, near-threshold vertices, distinct vertices).

### D4. Color Parsing Unification (P3)

PLY importer migrated to use `NormalizeColorChannelToUnitRange` (was using inline normalization). XYZ and PCD importers still have independent parsing flows.

- [ ] Unify color triplet/intensity parsing into a shared `Importers::ParseColor` helper.
- [ ] Ensure consistent [0,255]→[0,1] and [0,1]→[0,1] range handling across remaining importers (XYZ, PCD).


### D6. Importer Line I/O Inconsistency (P3)

OBJ, PCD, TGF, XYZ, and OFF importers now use the shared `TextParse::NextLine()` / `SplitWhitespace()` utilities. PLY importer still uses `std::istringstream` + `std::getline()` for header parsing (acceptable — PLY header is inherently line-oriented with mixed element/property declarations). STL also uses `std::istringstream` for ASCII parsing.

- [ ] Evaluate migrating STL ASCII parser to `TextParse` (lower priority, format is simple).

### D7. Selection.cpp Picking Helper Extraction (P3)

`Runtime.SelectionModule.cpp` contains several large picking functions with repeated hit-test patterns (closest-point-on-segment, sphere-ray intersection). These could be factored into geometry query helpers.

- [ ] Extract closest-point-on-segment and ray-sphere helpers into `Geometry::Queries` or a `Selection` utility header.
- [ ] Reduce duplication between vertex/edge/face picking code paths.

### D10. Geometry Kernel: Neighborhood Centroid — Point Cloud Path (P4)

`NormalEstimation.cpp` computes centroids over KNN point-cloud neighborhoods (raw `std::vector<glm::vec3>`, no halfedge connectivity). This is a different pattern from the mesh 1-ring centroid now in `MeshUtils::ComputeOneRingCentroid()` and cannot share the same helper.

- [ ] Evaluate extracting a standalone `ComputePointCentroid(points, indices)` helper if more point-cloud operators need it.

### D12. Importer Color Parsing: Consolidate Remaining Paths (P3)

PLY, PCD, and OFF importers now use `Detail::NormalizeColorChannelToUnitRange()` for color normalization. XYZ importer has standalone `ParseColorTriplet()` / `ParsePointColor()` functions that also use the shared helper.

- [ ] Extract shared `Importers::ParseColor()` helper unifying [0,255]→[0,1] and [0,1]→[0,1] range handling across remaining importers (XYZ, PCD).

### D15. Render Pass: Generic EnsureAttrBuffer Template (P3)

Five nearly-identical `Ensure*Buffer()` functions exist across passes (LinePass, PointPass, SurfacePass), each following the same map-lookup → SafeDestroy → allocate → write → return BDA pattern. Consolidating into a single generic template in `PassUtils.hpp` would eliminate ~150 lines of duplicated buffer management logic.

- [ ] Extract a generic `EnsurePerEntityBuffer<T>()` template into `PassUtils.hpp`.
- [ ] Migrate `EnsureEdgeAuxBuffer`, `EnsurePointAuxBuffer`, `EnsurePointRadiiBuffer`, `EnsureFaceAttrBuffer`, `EnsureVertexAttrBuffer` to use the shared template.

### D16. Render Pass: Naming Inconsistency — Aux vs Attr (P4)

Per-entity attribute buffer entry structs use inconsistent naming across passes: `FaceAttrEntry` / `VertexAttrEntry` (SurfacePass) vs `RetainedEdgeAuxEntry` / `RetainedPointAuxEntry` (LinePass / PointPass). Standardize to one convention. Note: `PtrAux` and `PtrEdgeAux` are also used in push constants and shaders, so renaming requires coordinated shader updates.

- [ ] Pick one naming convention (`*AuxEntry` or `*AttrEntry`) and unify across all passes.
- [ ] Update corresponding push constant field names and shader `PtrAux`/`PtrEdgeAux` references.

### D17. Frame Counter Naming Inconsistency (P3)

Three naming conventions for the same monotonic frame counter across the codebase: `GetGlobalFrameNumber()` (Device), `frameEpoch` (CommandContext locals), `currentFrameNumber` (ResourcePool parameters), `globalFrame` / `currentGlobalFrame` (RenderSystem/RenderGraph locals). Within `RenderGraph.cpp` both `globalFrame` and `frameEpoch` are used.

- [ ] Pick a canonical local variable name (suggest `globalFrame`) and standardize across RHI, RenderGraph, and lifecycle system locals.
- [ ] Keep method name `GetGlobalFrameNumber()` and parameter name `globalFrameNumber` for public API consistency.
