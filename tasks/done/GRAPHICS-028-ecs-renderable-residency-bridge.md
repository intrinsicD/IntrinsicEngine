# GRAPHICS-028 — ECS renderable-entity to GpuWorld residency bridge

## Goal
Define the planning-level architecture for turning a renderable ECS entity (single-mesh, hierarchical, or compound across mesh / graph / point cloud / primitive domains) into the flat list of `GpuWorld` instance + geometry records that the renderer consumes, including the static-vs-dynamic stream split, per-stream dirty-tag protocol, and primitive-instancing policy. This is a design/owner task; no implementation lands here.

**Layer-contract constraint (non-negotiable, per `AGENTS.md` section 2):** `ecs -> core` only (with narrow geometry exceptions). The ECS layer must not import `graphics/*` or `graphics/rhi`. All GPU handles (`GpuInstanceHandle`, `GpuGeometryHandle`, `RHI::BufferHandle`) and GPU-residency metadata live in runtime-owned extraction sidecar/cache state keyed by stable entity ID, not in canonical `src/ecs/Components/` and not as registry-attached ECS components. That sidecar may store graphics-owned value types such as `Extrinsic::Graphics::Components::GpuSceneSlot` (`src/graphics/renderer/Components/Graphics.Component.GpuSceneSlot.cppm`) plus asset-binding metadata; ECS stores only CPU asset identifiers and dirty tags. `GpuAssetView::Generation` is a graphics-cache generation used by runtime for hot-reload comparison, not an ECS field.

## Non-goals
- No new render passes, no shader changes, no pipeline-registry changes.
- No live ECS knowledge inside `src/graphics/*` — graphics consumes snapshots via `Runtime.RenderExtraction`, not the live `entt::registry`.
- No graphics/RHI imports inside `src/ecs/*` — the ECS layer stays `core`-only per `AGENTS.md` section 2; GPU-typed sidecar values live under `src/graphics/renderer/Components/` and are stored/maintained only by `runtime` extraction caches outside canonical ECS components.
- No new "ECS-side renderable" component carrying GPU handles. The bridge cannot introduce one.
- No expansion of `GpuWorld` capacity, compaction, or eviction policy beyond what GRAPHICS-004/005/015 already establish.
- No transparency/OIT or hybrid-pipeline decisions (those belong to GRAPHICS-025).
- No new asset kinds; geometry domains are mesh, graph, point cloud, and the existing primitive set.

## Context
Status: completed planning task.

Existing pieces already cover the residency machinery in isolation:

- `src/ecs/Components/ECS.Component.AssetInstance.cppm` — `AssetInstance::Source { std::uint32_t AssetId }`; runtime normalizes this CPU asset identifier to the `Assets::AssetId` expected by `GpuAssetCache` where that bridge is available.
- `src/ecs/Components/ECS.Component.GeometrySources.cppm` — live `Vertices/Edges/Halfedges/Faces/Nodes` streams plus `Domain` detection (Mesh / Graph / PointCloud).
- `src/ecs/Components/ECS.Component.DirtyTags.cppm` — `GpuDirty`, `DirtyVertexPositions`, `DirtyVertexAttributes`, `DirtyEdgeTopology`, `DirtyFaceTopology`, `DirtyTransform`.
- `src/ecs/Components/ECS.Component.Hierarchy.cppm` — parent / first-child / sibling links.
- `src/ecs/Systems/ECS.System.RenderSync.cppm` — currently a stub. The bridge **does not** belong here in its GPU-handle-touching form; per GRAPHICS-016 runtime owns extraction. Anything in this module must remain `core`-only.
- `src/graphics/assets/Graphics.GpuAssetCache.cppm` — `Assets::AssetId → GpuAssetView` with `NotRequested → CpuPending → GpuUploading → Ready/Failed`, generation counter, retire queue.
- `src/graphics/renderer/Graphics.GpuWorld.cppm` — instance pool, geometry pool, managed vertex/index buffers, scene-table, lights, compaction; strong handles `GpuInstanceHandle` / `GpuGeometryHandle`.
- `src/graphics/renderer/Graphics.GpuScene.cppm` — legacy dynamic-buffer pattern (`AllocateDynamicBuffer` / `UpdateDynamicBufferRange`) referenced for design discussion only.
- `src/graphics/renderer/Components/Graphics.Component.GpuSceneSlot.cppm` — **existing graphics-owned residency value type**. Already carries `InstanceSlot/Generation`, `GeometrySlot/Generation`, and `NamedBuffers` / `NamedBufferEntries` maps for per-entity dynamic streams. Lives in namespace `Extrinsic::Graphics::Components`; runtime may store it in extraction sidecar/cache entries, but ECS never imports it and it is not a canonical ECS component.
- `src/runtime/Runtime.RenderExtraction.cppm` — already imports both ECS and graphics and is the established home for the bridge. It currently keeps `RenderExtractionCache::RenderableSidecar` entries in a `std::unordered_map<std::uint32_t, RenderableSidecar>` keyed by stable entity ID, allocates/frees `GpuWorld` instances, clears consumed `DirtyTransform` tags, and submits snapshots to graphics.

What is *not* yet defined is the contract that wires them together: how `GpuSceneSlot` is extended to cover asset-generation tracking and per-entity dynamic-attribute residency end-to-end, how a hierarchical multi-part entity decomposes into draw leaves, how heterogeneous geometry domains share or diverge in their packers, and whether procedural-primitive entities (AABB, sphere, capsule, cylinder, plane, OBB, ellipsoid) become individual instances or per-frame collected instanced batches.

## Required changes
- Specify the residency carrier as **runtime-owned sidecar/cache state** keyed by stable entity ID and containing graphics-owned value types, never as a component under `src/ecs/Components/` and never as a GPU-typed component attached to the live ECS registry. Concretely:
  - Record the future extension contract for `Extrinsic::Graphics::Components::GpuSceneSlot` (`src/graphics/renderer/Components/Graphics.Component.GpuSceneSlot.cppm`) — it already holds `InstanceSlot/Generation`, `GeometrySlot/Generation`, and `NamedBuffers` / `NamedBufferEntries` (per-entity dynamic streams). A follow-up implementation task may add the asset-binding fields needed for hot-reload tracking:
    - `Assets::AssetId SourceAsset{}` — if the geometry came from a shared asset; sentinel for purely dynamic geometry.
    - `std::uint64_t LastSeenAssetGeneration = 0` — compared against `GpuAssetView::Generation` to detect cache reloads.
    - Optional debug-name / packer-tag if needed for diagnostics.
  - `runtime` (specifically `Runtime.RenderExtraction` or a dedicated `Runtime.RenderResidency` sibling) is the **only** writer of the entity-to-residency cache. Graphics may consume only renderer-submitted snapshot/view copies; render passes do not query the live ECS registry or runtime cache directly.
  - On the ECS side, allow at most a tiny `core`-only marker tag (e.g. `Renderable {}` with no fields) if it improves view filtering; this carries no GPU types and imports nothing from graphics. Decide in this task whether the marker is needed at all — `GeometrySources::*` + `AssetInstance::Source` may already be sufficient as filter components.
  - Document the lifecycle: runtime allocates or updates a sidecar/cache entry when an entity gains `(GeometrySources::* | AssetInstance::Source)`; runtime frees it when the entity is destroyed or no longer qualifies during extraction; runtime is responsible for retire-queue ordering against `GpuAssetCache::Tick(currentFrame, framesInFlight)` and `GpuWorld` deferred-free semantics.
- Specify the static-vs-dynamic stream split as a per-stream decision, not per-entity:
  - Static, immutable, dedupable across instances → CPU `AssetInstance::Source::AssetId` normalized by runtime to `Assets::AssetId` → `GpuAssetCache` → `GpuWorld::UploadGeometry()` into managed vertex/index buffers.
  - Dynamic, per-entity, frequently overwritten → `GpuScene`-style host-visible buffer or successor on `GpuWorld` (decision recorded here, not implemented).
  - Per-instance constants → SoA SSBOs via `SetInstanceTransform / SetInstanceMaterialSlot / SetInstanceRenderFlags`.
- Specify the per-stream dirty-tag consumer protocol:
  - Editing systems set `DirtyVertexPositions` / `DirtyVertexAttributes` / `DirtyEdgeTopology` / `DirtyFaceTopology` / `DirtyTransform` on the leaf entity holding the live `GeometrySources` view. Tags are pure CPU markers under `src/ecs/Components/` — they carry no GPU types and stay layer-clean.
  - Do **not** encode ECS dirtiness as `property A → GPU buffer B`. ECS may optionally carry CPU-only property/channel dirty descriptors if profiling shows coarse tags are too expensive, but those descriptors must name only CPU concepts such as domain, element class, property key/stamp, value-vs-schema change, and optional element ranges. They must not name `GpuSceneSlot`, `RHI::BufferHandle`, bindless indices, or renderer buffer names.
  - The runtime residency bridge owns the property/channel-to-stream mapping. It translates CPU property keys or version stamps into `GpuSceneSlot::NamedBuffers` entries, packed `GeometryUploadDesc` streams, or full geometry reuploads according to the active per-domain packer. If a dirty property is interleaved into a packed vertex stream, runtime may still repack the whole stream; fine-grained descriptors are an optimization hint, not a promise of partial GPU writes.
  - The **runtime** residency-bridge consumes and clears tags in dependency order (transform-only → attribute-only partial reupload → topology-changing full reupload). The ECS-side `RenderSync` system, if retained, only forwards/aggregates tag state; it does not call `GpuWorld` or `GpuAssetCache`.
  - `GpuDirty` is the documented "I don't know what changed" escape hatch and must not be the default emitter.
- Specify hierarchy semantics:
  - `TransformHierarchy` propagates `Local → World` top-down and stamps `DirtyTransform` on every renderable leaf that runtime residency extraction tracks.
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
  - **High-volume / debug**: when the count of primitive entities of a type exceeds a configurable threshold (or they are classified by runtime/editor as transient debug), collect them per frame into a single instanced batch — one draw, one bound shared geometry, per-instance SSBO of `{type, params, color, render_flags}` — owned by the existing `Pass.Forward.Line / Point` debug overlay pattern (mirrors `GRAPHICS-010Q` transient debug packets). These do **not** consume `GpuWorld` instance slots.
  - **Authored** colliders/lights/zones that the user can pick or transform: stay as regular instances even when count is high; the cull/select pipeline already pays for them.
  - Record the threshold and collection decision as runtime extraction policy/config, not as a graphics render-flag bit and not as a GPU-typed ECS component. A future CPU-only ECS/editor marker may opt an entity into transient debug collection, but authored/pickable primitives default to regular residency.
- Specify the per-frame residency-bridge ordering contract (owner: `runtime`, not `ecs`):
  1. New renderables (`AssetInstance::Source` / `GeometrySources::*` whose stable entity ID has no runtime residency cache entry yet) — request or reserve asset upload via `GpuAssetCache` once CPU asset payloads are available, allocate `GpuInstanceHandle` via `GpuWorld::AllocateInstance`, store/update `GpuSceneSlot` inside the runtime sidecar cache, defer geometry binding until cache reports `Ready`.
  2. Geometry-dirty leaves — runtime repacks via the per-domain packer and calls `GpuWorld::UploadGeometry()` (or updates a per-entity dynamic buffer recorded in `GpuSceneSlot::NamedBuffers`); clears the corresponding ECS dirty tags.
  3. Transform-dirty leaves — `GpuWorld::SetInstanceTransform`; clears `DirtyTransform`.
  4. Asset cache poll — rebind if `GpuAssetView::Generation` has advanced past `GpuSceneSlot::LastSeenAssetGeneration`.
  5. `GpuWorld::SyncFrame()` and `GpuAssetCache::Tick(currentFrame, framesInFlight)` close the frame.

  Steps 1–4 read the live `entt::registry` and update only runtime-owned cache state; this is a runtime activity, not a graphics one. Graphics receives only the resulting snapshot/views per GRAPHICS-002.
- Identify split points for follow-up implementation tasks (e.g. runtime residency cache + `GpuSceneSlot` asset-generation extension; core-only `RenderSync` skeleton if needed; per-domain packers; primitive instancing policy/config; partial-reupload paths). These are listed but not opened here.
- Cross-link decisions with GRAPHICS-004, GRAPHICS-005, GRAPHICS-007, GRAPHICS-010, GRAPHICS-011, GRAPHICS-014, GRAPHICS-015, and GRAPHICS-025.

## Tests
- Planning task; task-policy, doc-link, and layering validation only.
- Future implementation subtasks must add `contract;runtime;graphics` tests for the residency cache lifecycle and dirty-tag drain ordering, plus `contract;ecs` boundary coverage for any CPU-only marker/tag semantics and `contract;graphics` coverage for per-domain packer round-trips.
- GPU coverage stays opt-in `gpu;vulkan` smoke and outside the default CPU gate.

## Docs
- Added the "ECS renderable residency bridge" section to `docs/architecture/graphics.md`.
- Cross-linked the boundary from `src/ecs/Components/README.md`, `src/ecs/Systems/README.md`, and `src/graphics/renderer/README.md`.
- Updated `docs/migration/nonlegacy-parity-matrix.md` links for the completed planning gate.

## Acceptance criteria
- The ECS-to-GpuWorld renderable bridge has a single recorded owner (`runtime`) and a clear runtime-owned residency-sidecar/cache contract that stores graphics-owned `GpuSceneSlot` state rather than introducing an ECS-side GPU-typed component.
- Static-vs-dynamic stream classification, CPU-only property/channel dirty semantics, dirty-tag drain order, hierarchy decomposition, and primitive instancing policy are unambiguous for downstream implementation work.
- Heterogeneous geometry (mesh / graph / cloud / primitive) shares a uniform `GeometryUploadDesc` shape so `GpuWorld` remains domain-agnostic.
- Primitive entities have a documented "regular instance" vs "collected instanced batch" decision rule, with the threshold/flag location recorded as runtime extraction policy/config.
- Layer contract holds: `src/ecs/*` imports nothing from `src/graphics/*` or `src/graphics/rhi/`; `src/graphics/*` does not access the live `entt::registry`. The bridge lives in `runtime`.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Completion
- Completed: 2026-05-07.
- Commit reference: local commit for GRAPHICS-028 planning-doc synchronization slice.
- Notes:
  - Planning decisions are mirrored in `docs/architecture/graphics.md` and source-layer README cross-links.
  - No implementation, shader, renderer pass, or ECS component changes landed in this slice.

## Forbidden changes
- No implementation, no shader changes, no pipeline-registry changes.
- No mixing of mechanical file moves with semantic refactors.
- No live ECS imports inside `src/graphics/*`.
- No `graphics/*` or `graphics/rhi` imports inside `src/ecs/*`. Specifically: do not introduce a `GpuResidency` (or similarly-named) ECS component carrying `GpuInstanceHandle`, `GpuGeometryHandle`, `RHI::BufferHandle`, or any GPU-typed field under `src/ecs/Components/`.
- No expansion of transparency / OIT / hybrid scope (that lives in GRAPHICS-025).
