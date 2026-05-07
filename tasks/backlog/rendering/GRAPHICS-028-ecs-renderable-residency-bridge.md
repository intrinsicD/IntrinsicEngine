# GRAPHICS-028 — ECS renderable-entity to GpuWorld residency bridge

## Goal
Define the planning-level architecture for turning a renderable ECS entity (single-mesh, hierarchical, or compound across mesh / graph / point cloud / primitive domains) into the flat list of `GpuWorld` instance + geometry records that the renderer consumes, including the static-vs-dynamic stream split, per-stream dirty-tag protocol, and primitive-instancing policy. This is a design/owner task; no implementation lands here.

## Non-goals
- No new render passes, no shader changes, no pipeline-registry changes.
- No live ECS knowledge inside `src/graphics/*` — all extraction stays in `runtime` / `ecs/Systems/RenderSync`.
- No expansion of `GpuWorld` capacity, compaction, or eviction policy beyond what GRAPHICS-004/005/015 already establish.
- No transparency/OIT or hybrid-pipeline decisions (those belong to GRAPHICS-025).
- No new asset kinds; geometry domains are mesh, graph, point cloud, and the existing primitive set.

## Context
Existing pieces already cover the residency machinery in isolation:

- `src/ecs/Components/ECS.Component.AssetInstance.cppm` — `AssetInstance::Source { AssetId }`.
- `src/ecs/Components/ECS.Component.GeometrySources.cppm` — live `Vertices/Edges/Halfedges/Faces/Nodes` streams plus `Domain` detection (Mesh / Graph / PointCloud).
- `src/ecs/Components/ECS.Component.DirtyTags.cppm` — `GpuDirty`, `DirtyVertexPositions`, `DirtyVertexAttributes`, `DirtyEdgeTopology`, `DirtyFaceTopology`, `DirtyTransform`.
- `src/ecs/Components/ECS.Component.Hierarchy.cppm` — parent / first-child / sibling links.
- `src/ecs/Systems/ECS.System.RenderSync.cppm` — currently a stub; intended bridge.
- `src/graphics/assets/Graphics.GpuAssetCache.cppm` — `AssetId → GpuAssetView` with `NotRequested → CpuPending → GpuUploading → Ready/Failed`, generation counter, retire queue.
- `src/graphics/renderer/Graphics.GpuWorld.cppm` — instance pool, geometry pool, managed vertex/index buffers, scene-table, lights, compaction; strong handles `GpuInstanceHandle` / `GpuGeometryHandle`.
- `src/graphics/renderer/Graphics.GpuScene.cppm` — legacy dynamic-buffer pattern (`AllocateDynamicBuffer` / `UpdateDynamicBufferRange`) referenced for design discussion only.

What is *not* yet defined is the contract that wires them together: which component owns the GPU handles on the entity side, how a hierarchical multi-part entity decomposes into draw leaves, how heterogeneous geometry domains share or diverge in their packers, and whether procedural-primitive entities (AABB, sphere, capsule, cylinder, plane, OBB, ellipsoid) become individual instances or per-frame collected instanced batches.

## Required changes
- Specify a `GpuResidency` ECS component (location: `src/ecs/Components/`) that holds:
  - `GpuInstanceHandle Instance` — slot in `GpuWorld`.
  - `GpuGeometryHandle Geometry` — shared if asset-sourced, unique if dynamic.
  - `Assets::AssetId SourceAsset` (optional) — for cache lookups / generation tracking.
  - `RHI::BufferHandle DynamicAttribs` (optional) — per-entity host-visible buffer for high-frequency streams.
  - `std::uint64_t LastSeenAssetGeneration` — invalidates when `GpuAssetView::Generation` advances.
  - Document that this component is **owned and mutated by `RenderSync` only**; gameplay/edit systems must not write it.
- Specify the static-vs-dynamic stream split as a per-stream decision, not per-entity:
  - Static, immutable, dedupable across instances → `AssetId` → `GpuAssetCache` → `GpuWorld::UploadGeometry()` into managed vertex/index buffers.
  - Dynamic, per-entity, frequently overwritten → `GpuScene`-style host-visible buffer or successor on `GpuWorld` (decision recorded here, not implemented).
  - Per-instance constants → SoA SSBOs via `SetInstanceTransform / SetInstanceMaterialSlot / SetInstanceRenderFlags`.
- Specify the per-stream dirty-tag consumer protocol:
  - Editing systems set `DirtyVertexPositions` / `DirtyVertexAttributes` / `DirtyEdgeTopology` / `DirtyFaceTopology` / `DirtyTransform` on the leaf entity holding the live `GeometrySources` view.
  - `RenderSync` consumes and clears tags in dependency order (transform-only → attribute-only partial reupload → topology-changing full reupload).
  - `GpuDirty` is the documented "I don't know what changed" escape hatch and must not be the default emitter.
- Specify hierarchy semantics:
  - `TransformHierarchy` propagates `Local → World` top-down and stamps `DirtyTransform` on every leaf with a `GpuResidency`.
  - Root / interior entities may carry no draw; only entities with `(GeometrySources::* | AssetInstance::Source)` materialize a `GpuInstanceHandle`.
  - Subtree visibility / LOD / hide cascades become a flag on each leaf's `GpuInstance::RenderFlags`, set during the hierarchy walk; no GPU-side hierarchy traversal.
  - Coarse aggregate bounds on the root are CPU-only and used for editor-side culling, not for `GpuWorld`'s bounds buffer.
- Specify per-domain packers (one per `GeometrySources::Domain` plus primitives):
  - **Mesh** — packed vertex stream + `SurfaceIndices`; surface pipeline.
  - **Graph** — node positions as vertices + `LineIndices`; line pipeline.
  - **PointCloud** — vertices, no indices; point/splat pipeline.
  - **Primitives** (`AABB`, `Sphere`, `Capsule`, `Cylinder`, `Plane`, `OBB`, `Ellipsoid`) — see policy below.
  - All packers produce a uniform `GpuWorld::GeometryUploadDesc`; `GpuWorld` itself stays domain-agnostic.
- Specify primitive-instancing policy:
  - **Default**: each primitive type has one shared "unit mesh" asset in `GpuAssetCache` (unit cube, unit sphere, unit cylinder, unit capsule, unit plane). Primitive entities allocate a regular `GpuInstanceHandle` that points at the unit geometry; the entity's transform encodes center/extents/radius. They participate in normal culling/sorting/picking and contribute one culling slot each.
  - **High-volume / debug**: when the count of primitive entities of a type exceeds a configurable threshold (or they are tagged as transient debug), collect them per frame into a single instanced batch — one draw, one bound shared geometry, per-instance SSBO of `{type, params, color, render_flags}` — owned by the existing `Pass.Forward.Line / Point` debug overlay pattern (mirrors `GRAPHICS-010Q` transient debug packets). These do **not** consume `GpuWorld` instance slots.
  - **Authored** colliders/lights/zones that the user can pick or transform: stay as regular instances even when count is high; the cull/select pipeline already pays for them.
  - Decide and record where the threshold and the "collect" flag live: a dedicated `PrimitiveInstancing` component? a render-flag bit? `RenderWorld` extraction policy?
- Specify a `RenderSync` per-frame ordering contract:
  1. New renderables (`AssetInstance::Source` / `GeometrySources::*` without `GpuResidency`) — request asset upload, allocate instance handle, defer geometry binding.
  2. Geometry-dirty leaves — repack and `UploadGeometry()` (or update dynamic buffer); clear stream tags.
  3. Transform-dirty leaves — `SetInstanceTransform`; clear `DirtyTransform`.
  4. Asset cache poll — rebind if `GpuAssetView::Generation` advanced past `LastSeenAssetGeneration`.
  5. `GpuWorld::SyncFrame()` and `GpuAssetCache::Tick()` close the frame.
- Identify split points for follow-up implementation tasks (e.g. residency component + RenderSync skeleton; per-domain packers; primitive instancing policy; partial-reupload paths). These are listed but not opened here.
- Cross-link decisions with GRAPHICS-004, GRAPHICS-005, GRAPHICS-007, GRAPHICS-010, GRAPHICS-011, GRAPHICS-014, GRAPHICS-015, and GRAPHICS-025.

## Tests
- Planning task; task-policy and doc-link validation only.
- Future implementation subtasks must add `contract;ecs` and `contract;graphics` tests for the residency component lifecycle, dirty-tag drain ordering, and per-domain packer round-trip.
- GPU coverage stays opt-in `gpu;vulkan` smoke and outside the default CPU gate.

## Docs
- Add an "ECS renderable residency" section to `docs/architecture/graphics.md` (or a sibling under `docs/architecture/`) once decisions are recorded.
- Cross-link from `src/ecs/Components/README.md`, `src/ecs/Systems/README.md`, and `src/graphics/renderer/README.md`.
- Update `docs/migration/nonlegacy-parity-matrix.md` if the residency bridge subsumes any legacy renderable-iteration paths.

## Acceptance criteria
- The ECS-to-GpuWorld renderable bridge has a single recorded owner and a clear residency-component contract.
- Static-vs-dynamic stream classification, dirty-tag drain order, hierarchy decomposition, and primitive instancing policy are unambiguous for downstream implementation work.
- Heterogeneous geometry (mesh / graph / cloud / primitive) shares a uniform `GeometryUploadDesc` shape so `GpuWorld` remains domain-agnostic.
- Primitive entities have a documented "regular instance" vs "collected instanced batch" decision rule, with the threshold/flag location recorded.
- No live ECS access leaks into `src/graphics/*`; the bridge stays in `ecs` + `runtime`.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes
- No implementation, no shader changes, no pipeline-registry changes.
- No mixing of mechanical file moves with semantic refactors.
- No live ECS imports inside `src/graphics/*`.
- No expansion of transparency / OIT / hybrid scope (that lives in GRAPHICS-025).
