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

All Core & RHI audit items have been resolved (SafeDestroy extraction, device lifetime docs, Core.Memory fprintf rationale). Details in git history.

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

### D2. Finish Shared Pass Helper Adoption (P3)

`Graphics.PassUtils.hpp` covers shader-path resolution (`ResolveShaderPaths`), non-owning `VulkanDevice` aliases (`MakeDeviceAlias`), `CreateCullingFrustum`, `CleanupOrphanedBuffers`, `EnsurePerEntityBuffer`, and `CreateMultiSamplerSetLayout`. `LinePass` and `PointPass` now use `CreateCullingFrustum` and `CleanupOrphanedBuffers` (previously duplicated inline). `PostProcessPass` now uses `CreateMultiSamplerSetLayout` from `PassUtils` (previously a private `static` helper). `PipelineLibrary.cpp` still uses raw `ResolveShaderPathOrExit` pairs because it resolves shaders without a `ShaderRegistry` — this is acceptable.

- [ ] Evaluate whether `PipelineLibrary` can adopt `ResolveShaderPaths` with an alternate resolver overload.
- [ ] Keep the refactor behavior-preserving and opportunistic when the file is next touched.

### D20. Lifecycle System Bounding Sphere Consolidation — Resolved

`ComputeLocalBoundingSphere` was defined identically in three lifecycle system files (`MeshViewLifecycle`, `GraphGeometrySync`, `PointCloudGeometrySync`) and with a superficially different but behaviourally equivalent variant in `MeshRendererLifecycle` (the extra `GetIndexCount() == 0` guard was dead code: the caller already checked that precondition). All four local definitions have been removed; the shared canonical implementation lives in `Graphics.LifecycleUtils.hpp` (details in git history).


### D7. Picking Code Path Duplication (P3) — Resolved

`Runtime.SelectionModule.cpp` duplicated the NDC→world-ray construction — the hover path now calls `BuildPickRequest()` directly (details in git history). `Runtime.Selection.cpp` graph-edge picking now uses canonical `Geometry::ClosestRaySegment` instead of a local duplicate (D19, details in git history). The duplicated world-space triangle normal computation that appeared in both `PickMeshEntityAuthoritative` and the flat-array fallback path has been extracted into a local `ComputeTriangleNormal` helper, and the local `PointSegmentDistanceSq` and `DistancePointToRaySq` helpers have been removed in favour of `Geometry::ClosestPointSegment` and `Geometry::ClosestPointRay` (details in git history).

### D10. Geometry Kernel: Neighborhood Centroid — Point Cloud Path (P4)

`NormalEstimation.cpp` computes centroids over KNN point-cloud neighborhoods (raw `std::vector<glm::vec3>`, no halfedge connectivity). This is a different pattern from the mesh 1-ring centroid now in `MeshUtils::ComputeOneRingCentroid()` and cannot share the same helper.

- [ ] Evaluate extracting a standalone `ComputePointCentroid(points, indices)` helper if more point-cloud operators need it.

### D19. Selection.cpp Picking Helpers Overlap with Geometry Module (P4) — Resolved

`Geometry::Queries` partition now provides `ClosestRaySegment()`, `ClosestPointSegment()`, and `ClosestPointRay()` — all returning rich result structs (`RaySegmentResult`, `PointSegmentResult`, `PointRayResult`) with distance, parameter, and closest-point in a single call. `Runtime.Selection.cpp` no longer contains any local geometry-query helpers; all three picking paths (mesh, point cloud, graph) use the canonical Geometry module functions. `Test_GeometryQueries.cpp` covers all three functions (details in git history).
