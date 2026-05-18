# ECS Architecture

`ecs` owns entity/component data modeling and deterministic command/application semantics.

## Responsibilities

- Entity/component storage and mutation APIs.
- Scheduling-safe command/event application boundaries.
- Snapshot/export seams for rendering and runtime wiring.

## Dependencies

- Allowed: `core` and geometry handles/types only when explicitly justified.
- Disallowed: direct dependency on graphics/runtime/app internals.

## Stable identity and scene metadata

The volatile `entt::entity` value is unsuitable as a serialized
identifier: it is recycled across entity destruction/creation, is
insertion-order-dependent, and does not survive scene save/load,
prefab references, or hot reload. ECS therefore owns a separate
durable identity contract, recorded in
[`tasks/active/HARDEN-068-ecs-stable-identity-and-scene-metadata.md`](../../tasks/active/HARDEN-068-ecs-stable-identity-and-scene-metadata.md)
slice 1:

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
Generator helpers and the optional `Runtime::StableIdRegistry` lookup
sidecar land in **HARDEN-068-Impl-B**, only when a concrete
serializer/selection consumer demands them.
