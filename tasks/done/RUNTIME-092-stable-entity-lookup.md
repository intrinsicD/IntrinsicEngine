# RUNTIME-092 — Runtime stable entity lookup sidecar

## Status
- State: done.
- Owner/agent: claude.
- Branch: `claude/intrinsicengine-agent-onboarding-8y1qR`.
- Maturity reached: `CPUContracted` (Slice A standalone sidecar + Slice B frame
  wiring and `SelectionController` composition).
- Completion date: 2026-05-31.
- PR/commit: Slice A — commits `4885933` + `55a4a65` (PR #957). Slice B —
  commit: _pending push to `claude/intrinsicengine-agent-onboarding-8y1qR`_.
  PR: _TBD_.
- Next verification step: none. `Operational` user-visible selection durability
  stays owned by `RUNTIME-089`, UI tasks, and final sandbox acceptance
  (`RUNTIME-095`).

## Slice plan
- **Slice A (this slice, done).** Standalone `Extrinsic.Runtime.StableEntityLookup`
  module: `StableId -> entt::entity` winner-map (`Rebuild`/`Track`/`Forget`),
  reversible render-id resolution, `ResolveByStableId`/`ResolveByRenderId`/
  `ResolveSelected`/`PruneStale`, deterministic smallest-render-id duplicate
  policy, lazy + bulk stale invalidation, and the diagnostics block. Pure-CPU
  `contract;runtime` coverage in `Test.StableEntityLookup.cpp`. Imports only the
  promoted ECS registry/handle + the `StableId` value type; adds no lookup state
  to ECS or graphics. Closes `Scaffolded`. Defers all frame/extraction wiring to
  Slice B.
- **Slice B (this slice, done).** Wired the lookup update into the runtime frame
  lifecycle before selection consumption: `Engine` owns a `StableEntityLookup`,
  attaches it to the `SelectionController` in `Initialize()`
  (`SetStableEntityLookup`), and calls `Rebuild(scene)` in `RunFrame` immediately
  before the pick-readback drain. Swapped the `SelectionController` render-id
  resolution seam (`ConsumeHit`, `SetSelectedByStableEntityId`) onto the sidecar's
  `ResolveByRenderId` (decode + live-registry validation) via an opt-in,
  non-owning `StableEntityLookup*`; with no lookup attached the controller falls
  back to the bare `ToEntityHandle` decode so standalone callers are unaffected.
  Documented the sandbox default: reference-scene entities remain transient and
  receive no generated `StableId` (the render-id path needs none). Added five
  `contract;runtime` composition cases in
  `Test.SelectionStableLookupComposition.cpp` (hit-resolves-through-lookup,
  recycled-slot-rejected-not-misapplied, programmatic-select-routes-through-lookup,
  no-lookup-fallback, rebuilt-durable-map-tracks-recycled-owner). Closes
  `Scaffolded → CPUContracted`.

## Goal
- Add a runtime-owned scene-local lookup sidecar that maps optional ECS `StableId` values and current `entt::entity` handles so selection, serialization-adjacent tooling, and sandbox UI can resolve durable IDs without widening ECS dependencies or putting lookup state in graphics.

## Non-goals
- No scene serializer or prefab implementation.
- No ECS-owned global lookup map; ECS owns only the `StableId` value type from `HARDEN-068`.
- No graphics-side stable-ID resolution.
- No change to `AssetInstance::Source::AssetId` typing.
- No editor UI in this task.

## Context
- Owner/layer: `runtime`; `HARDEN-068` explicitly deferred `StableId -> entt::entity` lookup to a runtime-side consumer task.
- `Runtime.RenderExtraction` currently uses `static_cast<std::uint32_t>(entt::entity)` as a stable renderable ID. That is acceptable for transient CPU contracts but not sufficient for serialized/editor-facing selection.
- `RUNTIME-089` can initially resolve by current entity ID, but durable selection and UI workflows need a named sidecar with deterministic stale/duplicate diagnostics.

## Required changes
- [x] Add a runtime `StableEntityLookup` module that can rebuild or incrementally maintain maps from `ECS::Components::StableId` to live `entt::entity` and from render/extraction stable IDs to live entities. _(Slice A: `StableId` winner-map + reversible render-id decode.)_
- [x] Define duplicate `StableId` policy: reject, pick first deterministically with diagnostics, or keep all and require disambiguation; implement the selected policy. _(Slice A: keep one deterministic winner — smallest `ToRenderId` wins — with `DuplicateStableIds` diagnostics.)_
- [x] Provide APIs for selection/runtime consumers: resolve by `StableId`, resolve by extracted stable render ID, enumerate selected live entities from stored stable IDs, and invalidate stale entries. _(Slice A: `ResolveByStableId`/`ResolveByRenderId`/`ResolveSelected`/`PruneStale` + lazy heal.)_
- [x] Wire the lookup update into the runtime frame or extraction lifecycle before selection consumption. _(Slice B: `Engine` rebuilds the lookup each frame in `RunFrame` before the pick-readback drain; `SelectionController` render-id resolution routes through the attached sidecar.)_
- [x] Add diagnostics for duplicate stable IDs, missing IDs, stale entity handles, and rebuild/update counts. _(Slice A: `StableEntityLookupDiagnostics`.)_
- [x] Decide whether reference-scene entities receive generated stable IDs in runtime or remain transient; document the sandbox default. _(Slice B decision: remain transient, no generated `StableId`. The sandbox selection render-id path decodes + validates the live handle and needs no `StableId`, so the default path stays allocation-free; durable `StableId` assignment lands with the first serializer/editor consumer. Documented in `src/runtime/README.md`.)_

## Tests
- [x] Add `contract;runtime` integration coverage that selection survives entity recycling. _(Slice B: `Test.SelectionStableLookupComposition.cpp` — recycled-slot render id rejected as stale not misapplied; rebuilt durable map tracks the recycled-id owner.)_
- [x] Add `contract;runtime` coverage for resolving an entity with a valid `StableId`.
- [x] Add duplicate-ID coverage matching the selected policy and diagnostics.
- [x] Add stale entity destruction coverage proving lookups invalidate deterministically.
- [x] Add extraction/render-ID compatibility coverage for current `entt::entity`-backed stable render IDs.
- [x] No `gpu`/`vulkan` test in this slice.

## Docs
- [x] Update `src/runtime/README.md` with stable lookup ownership and duplicate/stale policies.
- [x] Update `docs/architecture/ecs.md` or `docs/architecture/runtime.md` if present/needed to cross-link the `HARDEN-068` deferred runtime lookup decision. _(Updated `docs/architecture/ecs.md` Decision-3 status.)_
- [x] Refresh `docs/api/generated/module_inventory.md` if new modules are added.

## Acceptance criteria
- [x] Runtime provides a deterministic lookup seam for selection/UI consumers without adding lookup state to ECS or graphics. _(Slice A module; frame wiring in Slice B.)_
- [x] Duplicate and stale stable-ID states are diagnosed and tested.
- [x] The sidecar composes with `RUNTIME-089` selection and later serialization/editor work. _(Slice B: `Engine` attaches the lookup to the `SelectionController` and rebuilds it each frame; render-id resolution routes through `ResolveByRenderId`; proven by `Test.SelectionStableLookupComposition.cpp`.)_

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeTests IntrinsicECSTests IntrinsicEcsContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract;runtime|unit;ecs|contract;ecs' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Adding a registry-wide lookup map to `src/ecs`.
- Importing runtime/ECS lookup state from `src/graphics/*`.
- Treating `entt::entity` as a serialized durable ID.
- Widening `ecs -> assets` or adding asset-service coupling.

## Maturity
- Target: `CPUContracted` runtime lookup sidecar. **Reached.**
- Slice A closed `Scaffolded`: the standalone sidecar + APIs + `contract;runtime`
  coverage exist, but nothing in the runtime frame path yet consumed the sidecar.
- Slice B closed `Scaffolded → CPUContracted` by wiring the rebuild into the
  frame lifecycle (`Engine::RunFrame`) and routing `SelectionController` render-id
  resolution through the sidecar, with composition coverage proving selection
  survives entity recycling.
- `Operational` user-visible selection durability is owned by `RUNTIME-089`, UI tasks, and final sandbox acceptance (`RUNTIME-095`).

