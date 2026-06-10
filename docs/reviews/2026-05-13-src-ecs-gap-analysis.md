# `src/ecs` Gap Analysis (2026-05-13)

## Scope

This review covers the promoted `src/ecs` layer as it exists on 2026-05-13. It focuses on the gap between the current implementation and the repository goal from [`AGENTS.md`](../../AGENTS.md): a modular, high-performance, scientifically rigorous engine with clean layer ownership, deterministic/testable APIs, and snapshot/export seams for lower-to-higher-layer integration.

This review is analysis only. It does not implement code and does not authorize deleting legacy ECS modules. Follow-up implementation should land through focused tasks under `tasks/backlog/ecs/`, `tasks/backlog/runtime/`, or the relevant architecture/physics/runtime backlog.

## Goal interpretation for `src/ecs`

The promoted ECS layer should own:

- entity/component storage and lifecycle primitives;
- canonical CPU-only scene intent components;
- deterministic mutation, command, and event application seams;
- hierarchy and transform propagation that is testable without graphics/runtime/app dependencies;
- CPU-only data contracts that runtime can snapshot/export to graphics, physics, serialization, editor, and app layers; and
- structural tests that prevent upward imports or runtime/graphics/physics-world sidecars from entering canonical ECS components.

The promoted ECS layer should not own:

- graphics/RHI handles, GPU residency, render-pass objects, or live renderer state;
- runtime composition, scene-manager policy, or editor/app behavior;
- asset loading services or live asset-service traffic;
- physics solver state, broadphase proxies, contacts, islands, or fixed-step runtime wiring; or
- platform/window/input concepts.

## Current capability inventory

### Strong foundations already present

- **Scene storage wrapper:** `Extrinsic.ECS.Scene.Registry` wraps `entt::registry` with typed `Create`, `Destroy`, `IsValid`, `Clear`, and explicit `Raw()` access.
- **Entity handles:** `Extrinsic.ECS.Scene.Handle` defines the promoted `EntityHandle` and `InvalidEntityHandle` alias.
- **Default entity bootstrap:** `Extrinsic.ECS.Scene.Bootstrap` creates/entities with `MetaData`, local transform, world matrix, and hierarchy components.
- **Hierarchy mutation:** `Extrinsic.ECS.Hierarchy.Structure` and `Extrinsic.ECS.Hierarchy.Mutation` provide linked-list parent/child/sibling maintenance, attach/detach, cycle rejection, invariant validation, and world-position preservation across reparenting.
- **Transform propagation:** `Extrinsic.ECS.System.TransformHierarchy` recomputes world matrices for dirty subtrees, clears `Transform::IsDirtyTag`, and emits `Transform::WorldUpdatedTag`.
- **Boundary hardening:** `tests/contract/ecs/Test.ECS.LayeringBoundaries.cpp` rejects higher-layer imports and prohibited runtime/graphics/physics-world symbols in `src/ecs`.
- **Render-adjacent data contracts:** `AssetInstance`, `ProceduralGeometryRef`, `DirtyTags`, culling bounds/proxy components, light components, selection tags, and `ShadowCaster` exist as CPU-only scene data.
- **Physics authoring descriptors:** `Collider` now stores explicit sphere,
  capsule, and box/OBB child-shape descriptors with local poses, material,
  filtering, trigger/contact-offset metadata, and `RigidBody` stores static /
  kinematic / dynamic body intent without solver state.

### Current promoted module surface

- Scene: `Extrinsic.ECS.Scene.Handle`, `Extrinsic.ECS.Scene.Registry`, `Extrinsic.ECS.Scene.Bootstrap`.
- Hierarchy: `Extrinsic.ECS.Hierarchy.Structure`, `Extrinsic.ECS.Hierarchy.Mutation`.
- Components: transform, world matrix, hierarchy, metadata, geometry sources, culling local/world/proxy, asset instance, collider, rigid body, light, procedural geometry reference, selection, shadow caster, dirty tags.
- Systems: `Extrinsic.ECS.System.TransformHierarchy`, `Extrinsic.ECS.System.RenderSync`.

### Important current limitations

- `Extrinsic.ECS.System.RenderSync` now owns CPU-only transform GPU-dirty tag
  forwarding (`HARDEN-066`); runtime extraction still owns graphics sidecars
  and drains the GPU-sync signal.
- Runtime activation from a promoted simulate-phase system bundle is covered by
  `RUNTIME-091` for `TransformHierarchy`, `BoundsPropagation`, and
  `RenderSync`.
- Promoted ECS has CPU-only event payloads and a documented command ownership
  seam from `HARDEN-063`; a generic command buffer / undo-redo / scheduling
  queue remains deliberately outside ECS and belongs to runtime/editor if a
  consumer needs it.
- `GeometrySources` now owns per-domain `Geometry::PropertySet` components and
  promoted population helpers for mesh/graph/point-cloud data (`HARDEN-065`).
- `Collider` / `RigidBody` authoring is CPU-contracted by `HARDEN-064`;
  runtime ECS-to-physics synchronization and live physics-world state remain
  outside ECS and are CPU-contracted by retired `PHYSICS-001`.
- Several current ECS tests still exercise the legacy `ECS` module rather than only `Extrinsic.ECS.*`, so they are not retirement evidence for promoted ECS.

## Gap matrix

| Area | Current state | Missing system/component | Impact | Priority / owner |
| --- | --- | --- | --- | --- |
| Deterministic mutation | `HARDEN-063` documents the command seam: ECS owns `Registry::{Create,Destroy,Clear}`, `Scene::{EmplaceDefaults,CreateDefault}`, `Hierarchy::{Attach,Detach}`, transform data + dirty markers, and selection/hover data carriers; runtime/editor/app own queueing, undo/redo, recursive delete/orphan policy, input interpretation, and event dispatch. | Generic command buffer / transaction queue remains a runtime/editor concern if needed. | ECS has deterministic low-level mutation primitives without importing higher-layer command history or editor behavior. | Done — [`HARDEN-063`](../../tasks/done/HARDEN-063-ecs-events-and-command-seams.md). |
| Events | `Extrinsic.ECS.Events` promotes CPU-only payloads for selection, hover, entity-spawned, and geometry-modified events; GPU-pick and upload-failed events remain runtime/graphics-owned. | Runtime/editor dispatchers, subscription, and queueing remain outside ECS. | Runtime/editor workflows have promoted payload shapes without coupling ECS to dispatch policy. | Done — [`HARDEN-063`](../../tasks/done/HARDEN-063-ecs-events-and-command-seams.md). |
| System scheduling | `TransformHierarchy::RegisterSystem` can add a FrameGraph pass. Runtime `Engine` lets applications add passes during `OnSimTick`. | Promoted simulate-phase system bundle or runtime-owned registration path that consistently schedules transform propagation before extraction. | Transform update parity exists in isolation but not as default runtime behavior. | P0 — `runtime`; follow-up from [`HARDEN-061`](../../tasks/done/HARDEN-061-ecs-hierarchy-transform-system-parity.md). |
| Render sync / export seam | `DirtyTags::DirtyTransform` exists; runtime extraction consumes renderable state and graphics components. `RenderSync` is empty. | CPU-only ECS render-sync/export seam or explicit retirement of `RenderSync`; policy for stamping/clearing GPU-sync dirty tags from `WorldUpdatedTag`. | Dirty-domain ownership is split and easy to regress; render extraction remains runtime-specific without an ECS-side snapshot contract. | P0/P1 — `ecs` + `runtime`; must preserve no graphics imports in ECS. |
| Geometry source authoring | Promoted `GeometrySources` stores non-owning `ObserverPtr<PropertySet>` and view/count helpers. Legacy had `PopulateFromMesh`, `PopulateFromGraph`, `PopulateFromCloud`. | Promoted population/copy/move helpers or a runtime-owned ingest contract that creates stable ECS geometry-source components from geometry containers. | Asset/runtime ingest cannot rely on promoted ECS as authoritative geometry scene data; legacy population helpers remain a retirement blocker. | P0 — likely `ecs` + `geometry` + `runtime`; listed in migration parity matrix. |
| Geometry dirty domains | `DirtyTags` has coarse GPU/topology/attribute transform markers. | Deterministic mutation helpers that stamp dirty domains when geometry properties/topology change; clear ownership of who clears them. | Geometry edits can miss render/selection/cache updates unless every caller remembers tags. | P0/P1 — `ecs` for tags/helpers, `runtime` for extraction clearing. |
| Bounds/culling update | Local/world culling components and proxy components exist. | System to recompute world bounds from local bounds + world matrix; diagnostics for missing/stale bounds; optional culling-proxy rebuild seam. | Extraction/rendering can consume stale world bounds; culling behavior is not fully ECS-testable. | P1 — `ecs` for CPU bounds propagation, `graphics` for actual culling. |
| Entity lifecycle cleanup | `Registry::Destroy` destroys one entity if valid. Hierarchy detach exists. | Recursive destroy or explicit destruction policy for child closure, component cleanup hooks, and orphan handling diagnostics. | Runtime/editor deletion must duplicate hierarchy cleanup policy; stale child links or sidecars become likely. | P1 — `ecs` command/lifecycle seam, runtime sidecars remain runtime-owned. |
| Stable identity and serialization metadata | `MetaData` stores only `EntityName`; `AssetInstance::Source::AssetId` is raw `std::uint32_t`. | Stable entity UUID/local scene ID, optional prefab/source provenance, serialization schema/version tags, and typed asset-ID decision. | Scene save/load, diffing, undo/redo, hot reload, and external references lack promoted ECS identity contracts. | P1 — `ecs` + `assets` architecture decision; AssetId typing was deferred by [`HARDEN-062`](../../tasks/done/HARDEN-062-ecs-layering-and-component-boundary-hardening.md). |
| Selection and picking commands | Selection tags and cached selected primitive indices exist. Legacy runtime selection modules still own behavior. | Pure selection mutation commands, multi-select mode contract, hover/pick result application seam, and selection-changed events or runtime ownership decision. | Editor/runtime selection workflows remain legacy/runtime-specific and are not ECS-retirement-ready. | P1 — `ecs` for data/commands if kept pure; `runtime/editor` for input and GPU readback. |
| Physics authoring | `HARDEN-064` adds `Collider::ShapeDescriptor` for sphere/capsule/box/OBB child shapes plus `RigidBody` static/kinematic/dynamic body intent. ECS docs and contract tests still forbid solver handles. | Collision broadphase/narrowphase and solver/island/sleep behavior remain outside ECS. | ECS can represent first-phase rigid-body authoring without owning a solver; `PHYSICS-001` adds the runtime sidecar/world synchronization without leaking handles into ECS. | Done for ECS — [`HARDEN-064`](../../tasks/done/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md); reference method done — [`METHOD-001`](../../tasks/done/METHOD-001-rigid-body-dynamics-reference-backend.md); world/bridge done — [`PHYSICS-001`](../../tasks/done/PHYSICS-001-physics-world-state-and-runtime-sync.md); collision done — [`PHYSICS-002`](../../tasks/done/PHYSICS-002-collision-broadphase-narrowphase-contract.md); solver/island/sleep done — [`PHYSICS-003`](../../tasks/done/PHYSICS-003-constraints-islands-and-solver-diagnostics.md). |
| Light authoring | Directional/point/spot/ambient light structs exist with minimal color/intensity fields. | Range/attenuation, cone angles, shadow/cascade policy, enabled state, temperature/units policy, and CPU validation helpers. | Lighting can be extracted but lacks an authoring-grade component contract. | P1/P2 — `ecs` for CPU descriptors, graphics for render interpretation. |
| Component enabled/visibility state | Shadow caster and selection tags exist; no general enabled/disabled layer. | Entity active/enabled tag, render visibility, simulation participation, and propagation policy through hierarchy. | Systems must infer participation from component presence; editor hide/disable workflows need ad hoc tags. | P2 — `ecs` data, runtime/editor policy. |
| DEC / method caches | Legacy `ECS:Components.DEC` wraps computed DEC operators. Promoted ECS has no DEC cache component. | Ownership decision for method/cache components: promote CPU-only method cache components, move to `methods`, or keep in runtime/editor sidecars. | Geometry/method workflows that cache per-entity computations remain legacy or ad hoc. | P2 — `methods` + `ecs` ownership decision. |
| Demo/gameplay systems | Legacy `AxisRotator` component/system exists; promoted ECS omits it. | Explicit retire decision or move to sandbox/app/runtime sample systems. | Legacy ECS cannot retire until demo behavior is either ported or declared non-goal. | P2 — likely `app`/`runtime`, not canonical ECS. |

## Missing systems by priority

### P0 — required for promoted ECS to become the canonical scene authority

1. **Command/event seam**
   - Define pure ECS commands for deterministic mutation: create, destroy, bootstrap, transform edit, component add/remove/replace, attach/detach.
   - Define an application phase: immediate only, deferred command buffer, or both with explicit ordering.
   - Define promoted event payload ownership or move event types to runtime/editor/graphics extraction.
   - Add tests for command ordering, invalid entities, duplicate component policies, parent/child mutation, and event emission.

2. **Runtime scheduling activation for transform propagation**
   - Runtime should consistently register or invoke promoted transform hierarchy propagation in the fixed-step path before render extraction.
   - Keep the registration in runtime or app-owned composition; do not make ECS import runtime.
   - Add integration tests proving dirty transforms are propagated during a headless frame without app-specific manual setup.

3. **Geometry-source population and dirty-domain helpers**
   - Promote or replace legacy `PopulateFromMesh`, `PopulateFromGraph`, and `PopulateFromCloud` behavior.
   - Decide whether `GeometrySources` should own copied `PropertySet`s, borrow them, or represent stable geometry asset IDs only.
   - Add mutation helpers that stamp `DirtyVertexPositions`, `DirtyVertexAttributes`, `DirtyEdgeTopology`, `DirtyFaceTopology`, and `GpuDirty` consistently.

4. **Render-sync/export policy**
   - Decide whether `Extrinsic.ECS.System.RenderSync` becomes a CPU-only aggregation pass, a tag-forwarding pass, or is retired.
   - Document who stamps `DirtyTags::DirtyTransform` from `Transform::WorldUpdatedTag`, and who clears each tag.
   - Keep renderer/GPU residency in runtime/graphics only.

### P1 — needed for engine-scale authoring and integration

1. **Lifecycle/destruction policy**
   - Add recursive destroy or a command-layer destruction policy that handles hierarchy children deterministically.
   - Emit events or diagnostics for destroyed/orphaned entities if events are promoted.

2. **Stable identity / serialization metadata**
   - Add a scene-stable ID distinct from `entt::entity` if scene serialization, undo/redo, prefab, or hot-reload references need it.
   - Decide whether raw `std::uint32_t` asset IDs remain ECS-local or whether the `ecs` dependency contract is widened to allow typed `Asset.Registry` handles.

3. **Bounds propagation system**
   - Recompute world culling bounds after transform/world changes.
   - Validate local bounds availability and finite values.
   - Keep actual frustum/occlusion culling in graphics or renderer systems.

4. **Selection command/event model**
   - Treat selection tags as data, but define deterministic operations for replace/add/toggle/clear and primitive-selection caches.
   - Keep GPU pick readback and input interpretation outside ECS.

5. **Physics authoring components** (done for ECS)
   - `HARDEN-064` expands `Collider` and adds `RigidBody` authoring descriptors.
   - `PHYSICS-001` adds CPU-contracted world/runtime synchronization after
     retired `METHOD-001` reference dynamics while preserving the
     no-solver-handles-in-ECS invariant.
   - Remaining physics behavior is broadphase/narrowphase (`PHYSICS-002`) and
     solver/island/sleep diagnostics (`PHYSICS-003`).

### P2 — useful but not retirement-critical

1. **Authoring quality-of-life components**
   - General enabled/disabled state, visibility flags, tags/layers/categories, and optional provenance records.

2. **Light authoring completeness**
   - Add range/attenuation/cone/shadow descriptors and validation helpers if runtime scenes need ECS-authored lighting beyond the current minimal fields.

3. **Method/cache components**
   - Decide whether caches such as DEC operators belong in ECS components, method-owned sidecars, or runtime caches keyed by entity/stable ID.

4. **Legacy demo behavior retirement**
   - Move `AxisRotator`-style behavior to app/runtime sample code or retire it as a non-goal.

## Missing components summary

### P0/P1 canonical ECS component candidates

- `StableId` / `SceneId` component for serialization and external references.
- `EntityState` or `EnabledTag` / `DisabledTag` with clear hierarchy propagation semantics.
- `RenderableIntent` only if it remains CPU-only and asset/geometry-ID based; otherwise keep render-specific components in `graphics` and runtime extraction.
- `GeometrySourceOwner` or an owning alternative to the current borrowed `GeometrySources` model, if ECS should be authoritative for loaded/editable geometry.
- Additional physics authoring components only if `PHYSICS-*` or method work
  identifies data that remains ECS authoring intent rather than live solver
  state; `RigidBody` plus expanded `Collider` descriptors are present.
- `Light` fields for range, attenuation, spot cone, shadow intent, and validation if ECS is the light authoring source.

### Components that should probably not be promoted into canonical ECS

- GPU scene slots, bindless indices, buffer/texture handles, renderer residency handles.
- Physics solver body handles, broadphase proxy IDs, contact caches, island IDs, solver indices.
- Platform/window/input state.
- Editor widget/UI state, except for pure data tags that are intentionally shared with runtime/editor workflows.

## Test and verification gaps

- Promoted ECS unit coverage exists for scene registry, bootstrap, hierarchy, and transform hierarchy, but several `tests/unit/ecs` files still import legacy `ECS` and `Core` umbrella modules. These tests are useful compatibility coverage, but they are not promoted-retirement evidence.
- Promoted collider/rigid-body authoring coverage exists in
  `tests/unit/ecs/Test.ECS.ColliderAuthoring.cpp`. Remaining promoted gaps are
  command buffer semantics and any future selection mutation commands not owned
  by runtime/editor.
- The ECS contract test catches upward imports and prohibited symbols, but it does not prove behavioral readiness of render extraction, runtime scheduling, serialization, or physics integration.

Recommended focused test additions as gaps are closed:

- `tests/unit/ecs/Test.ECS.Commands.cpp` — deterministic command application and invalid-handle behavior.
- `tests/unit/ecs/Test.ECS.Events.cpp` — pure event payload construction/dispatch if events are promoted.
- `tests/unit/ecs/Test.ECS.GeometrySourcesPopulate.cpp` — mesh/graph/point-cloud population and dirty-domain stamping.
- `tests/unit/ecs/Test.ECS.BoundsPropagation.cpp` — local-to-world bounds recompute and stale/missing bounds diagnostics.
- `tests/unit/ecs/Test.ECS.SelectionCommands.cpp` — replace/add/toggle/clear selection semantics if owned by ECS.
- `tests/unit/ecs/Test.ECS.ColliderAuthoring.cpp` — added by `HARDEN-064` for
  expanded collider/rigid-body descriptors.
- `tests/integration/runtime/Test.RuntimeEcsSystemBundle.cpp` — runtime fixed-step activation of promoted ECS systems.

## Recommended next task order

1. **Finish [`HARDEN-063`](../../tasks/done/HARDEN-063-ecs-events-and-command-seams.md)** (done) — define event and command ownership. This unlocks deterministic mutation, selection decisions, and lifecycle policy.
2. **Implement [`RUNTIME-091`](../../tasks/done/RUNTIME-091-promoted-ecs-system-bundle-activation.md)** (done) — register/invoke `TransformHierarchy` by default in fixed-step runtime composition before extraction.
3. **Implement [`HARDEN-065`](../../tasks/done/HARDEN-065-ecs-geometry-source-population-and-dirty-domains.md)** (done) — decide owning vs borrowed `GeometrySources` and port/rewrite population helpers with dirty-domain tests.
4. **Implement [`HARDEN-066`](../../tasks/done/HARDEN-066-ecs-render-sync-export-policy.md)** (done) — either implement a CPU-only tag/export pass or retire the placeholder and document runtime extraction as the sole owner.
5. **Implement [`HARDEN-067`](../../tasks/done/HARDEN-067-ecs-bounds-propagation-system.md)** (done) — keep world culling bounds synchronized after promoted transform updates.
6. **Implement [`HARDEN-064`](../../tasks/done/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md)** (done) — expand collider and add rigid-body authoring components without solver handles.
7. **Implement [`HARDEN-068`](../../tasks/done/HARDEN-068-ecs-stable-identity-and-scene-metadata.md)** (done) — define ECS-owned stable identity/metadata before runtime scene serialization depends on entity references. `HARDEN-068-Impl-A` landed the `StableId` payload module; `HARDEN-068-Impl-B/C` remain identified-only follow-ups that open only when a concrete consumer demands them.

## Bottom line

`src/ecs` has a solid promoted foundation for registry ownership, default entity bootstrap, hierarchy mutation, transform propagation, and layer-boundary enforcement. The largest remaining pieces are not more component structs; they are deterministic seams and higher-layer integrations: command application where runtime/editor need it, event dispatch ownership, runtime scheduling activation, render extraction policy, and higher-level physics collision/solver behavior after the ECS authoring contract and `PHYSICS-001` bridge. Closing those gaps will make ECS a reliable scene-authority layer while preserving the repository contract that runtime wires systems and graphics/physics own their sidecars.
