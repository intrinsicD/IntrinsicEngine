# RUNTIME-092 — Runtime stable entity lookup sidecar

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
- [ ] Add a runtime `StableEntityLookup` module that can rebuild or incrementally maintain maps from `ECS::Components::StableId` to live `entt::entity` and from render/extraction stable IDs to live entities.
- [ ] Define duplicate `StableId` policy: reject, pick first deterministically with diagnostics, or keep all and require disambiguation; implement the selected policy.
- [ ] Provide APIs for selection/runtime consumers: resolve by `StableId`, resolve by extracted stable render ID, enumerate selected live entities from stored stable IDs, and invalidate stale entries.
- [ ] Wire the lookup update into the runtime frame or extraction lifecycle before selection consumption.
- [ ] Add diagnostics for duplicate stable IDs, missing IDs, stale entity handles, and rebuild/update counts.
- [ ] Decide whether reference-scene entities receive generated stable IDs in runtime or remain transient; document the sandbox default.

## Tests
- [ ] Add `contract;runtime` coverage for resolving an entity with a valid `StableId`.
- [ ] Add duplicate-ID coverage matching the selected policy and diagnostics.
- [ ] Add stale entity destruction coverage proving lookups invalidate deterministically.
- [ ] Add extraction/render-ID compatibility coverage for current `entt::entity`-backed stable render IDs.
- [ ] No `gpu`/`vulkan` test in this slice.

## Docs
- [ ] Update `src/runtime/README.md` with stable lookup ownership and duplicate/stale policies.
- [ ] Update `docs/architecture/ecs.md` or `docs/architecture/runtime.md` if present/needed to cross-link the `HARDEN-068` deferred runtime lookup decision.
- [ ] Refresh `docs/api/generated/module_inventory.md` if new modules are added.

## Acceptance criteria
- [ ] Runtime provides a deterministic lookup seam for selection/UI consumers without adding lookup state to ECS or graphics.
- [ ] Duplicate and stale stable-ID states are diagnosed and tested.
- [ ] The sidecar composes with `RUNTIME-089` selection and later serialization/editor work.

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
- Target: `CPUContracted` runtime lookup sidecar.
- `Operational` user-visible selection durability is owned by `RUNTIME-089`, UI tasks, and final sandbox acceptance.

