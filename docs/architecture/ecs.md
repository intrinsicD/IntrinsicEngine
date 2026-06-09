# ECS Architecture

`ecs` owns entity/component data modeling and deterministic command/application semantics.

## Responsibilities

- Entity/component storage and mutation APIs.
- Scheduling-safe command/event application boundaries.
- Snapshot/export seams for rendering and runtime wiring.

## Dependencies

- Allowed: `core` and geometry handles/types only when explicitly justified.
- Disallowed: direct dependency on graphics/runtime/app internals.

## Physics authoring boundary

Physics layer ownership is accepted in
[ADR-0019](../adr/0019-physics-layer-ownership-and-ecs-integration.md).
ECS owns authoring intent only: collider/body descriptors, local collider
poses, material/filtering/trigger intent, enabled state, and CPU-only geometry
descriptors when explicitly justified. Physics-world handles, broadphase
proxies, contact caches, islands, solver indices, runtime sidecars, graphics
handles, and RHI handles are forbidden in canonical ECS components.

`HARDEN-064` shipped this ECS-side authoring surface as
`Extrinsic.ECS.Component.Collider` and `Extrinsic.ECS.Component.RigidBody`.
Collider authoring supports sphere, capsule, and box/OBB shape descriptors with
explicit child local poses, material/filtering/trigger/contact-offset metadata,
and per-shape enabled state. Rigid-body authoring stores static/kinematic/
dynamic intent, mass policy, velocities, damping, gravity scale, sleep flags,
CCD intent, and contact participation. ECS classifies valid authoring
combinations but does not create live physics-world state.

Runtime owns ECS-to-physics synchronization, fixed-step scheduling, live handle
sidecars, physics-to-ECS transform writeback, and contact/event routing.
Compound colliders are explicit child-shape descriptors under a collider/body;
the ECS scene hierarchy is not a compound-collider tree.

## Legacy component compatibility decisions

`HARDEN-081` closes the remaining legacy ECS component/system compatibility
questions without adding compatibility wrappers or widening the ECS dependency
surface:

- Legacy `ECS::Components::NameTag::Component` maps to the promoted
  `Extrinsic::ECS::Components::MetaData::EntityName` bootstrap naming contract.
  No separate `NameTag` module or alias is promoted.
- Legacy `AxisRotator` component/system behavior is demo/sample behavior, not
  canonical ECS. Future sample rotation belongs in `runtime` or `app` behind a
  concrete task; ECS owns only transform data, dirty markers, and the promoted
  transform hierarchy/render-sync systems.
- Legacy `ECS::Components::DEC` operator caches are not canonical ECS state.
  `Geometry.DEC` remains the operator implementation owner, while methods,
  runtime tools, or editor workflows that need cached operators must hold their
  own sidecars keyed by ECS identity.
- Shared system-feature tokens and catalogs are not promoted into ECS.
  `CORE-002` retired the global catalog shape, and runtime activates promoted
  ECS systems explicitly through `Extrinsic.Runtime.EcsSystemBundle`.

Remaining bare `import ECS` consumers are legacy subtrees or compatibility
tests and are cleanup work for `LEGACY-012` / mechanical subtree deletion
tasks, not feature blockers that justify new promoted ECS compatibility APIs.

## Stable identity and scene metadata

The volatile `entt::entity` value is unsuitable as a serialized
identifier: it is recycled across entity destruction/creation, is
insertion-order-dependent, and does not survive scene save/load,
prefab references, or hot reload. ECS therefore owns a separate
durable identity contract, recorded in
[`tasks/done/HARDEN-068-ecs-stable-identity-and-scene-metadata.md`](../../tasks/done/HARDEN-068-ecs-stable-identity-and-scene-metadata.md):

| # | Decision | Pick |
|---|---|---|
| 1 | Identity shape | **128-bit UUID-shaped `StableId`** (`struct StableId { std::uint64_t High; std::uint64_t Low; }`) with `kInvalidStableId == StableId{0u, 0u}`, `IsValid`, `operator<=>`, and an explicit `StableIdHash` (mirrors the `Extrinsic.Core.StrongHandle` exported-hasher pattern). |
| 2 | Mandatory vs optional | **Optional sparse `entt`-component.** Default scene bootstrap does not assign one; authoring/runtime opts in. |
| 3 | Reference resolution | **ECS owns only the value type + equality/hash.** Any `StableId → entt::entity` lookup sidecar lives in `runtime/`, not `ecs/`. Persisted-to-disk or crossing-hot-reload references store `StableId`; in-process references may still store `entt::entity`. |
| 4 | Component placement | **Separate `Extrinsic.ECS.Component.StableId` module.** `MetaData` stays the bootstrap naming contract; future serialization-adjacent metadata lands as additional **focused** components, not as extensions to `MetaData`. |
| 5 | `AssetInstance::Source::AssetId` typing | **Defer.** Raw `std::uint32_t` retained per `HARDEN-062`; widening to a typed `Extrinsic::Assets::AssetId` is a future `ARCH-*` task that owns the `ecs → assets` allowlist change. HARDEN-068 explicitly does not widen the contract. |

**Status.** `HARDEN-068-Impl-A` (slice 2) added the payload module
`Extrinsic.ECS.Component.StableId`
(`src/ecs/Components/ECS.Component.StableId.cppm`) exporting
`Extrinsic::ECS::Components::StableId`, `kInvalidStableId`, `IsValid`,
defaulted equality / `operator<=>`, and the exported `StableIdHash`
functor. The payload is a pure CPU value type with no `entt`,
`geometry`, `assets`, `runtime`, `graphics`, or `platform` imports, so
serializers, editors, and runtime helpers can consume it without taking
on those dependencies. The targeted contract test
`tests/contract/ecs/Test.ECS.LayeringBoundaries.cpp`
(`StableIdPayloadStaysCpuOnly`) guards the no-`entt` rule on this one
file; the existing directory-recursive sweep continues to enforce the
shared `ecs -> {core, geometry}` import constraint. CPU-only
`unit;ecs` payload tests live in
`tests/unit/ecs/Test.ECS.StableIdentity.cpp` and cover default
construction, sentinel equality, `operator<=>`, hashability across the
module boundary, swapped-halves hash distinguishability, and the
trivially-copyable / standard-layout / 16-byte size invariants.
Generator helpers and the optional runtime lookup sidecar land
when a concrete serializer/selection consumer demands them. The
selection path is that consumer: `RUNTIME-092` Slice A realised the
Decision-3 sidecar as `Extrinsic.Runtime.StableEntityLookup`
(`src/runtime/Runtime.StableEntityLookup.cppm`), a runtime-owned
`StableId -> entt::entity` winner-map with deterministic
smallest-render-id duplicate policy and lazy stale invalidation.
`RUNTIME-092` Slice B then wired that sidecar into the runtime frame
path: `Engine` owns the lookup, rebuilds it each frame before the
selection pick-readback drain, and routes `SelectionController`
render-id resolution through it, so durable selection survives
`entt::entity` recycling. ECS still owns only the value type; the lookup
map lives entirely in `runtime/`. See `src/runtime/README.md` for the
duplicate/stale policy and frame-wiring notes.
