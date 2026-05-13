# HARDEN-068 — Define ECS stable identity and scene metadata contract

## Goal
- Define promoted ECS components and policies for stable entity identity, scene-local references, and serialization-facing metadata without coupling ECS to runtime scene serialization or live asset services.

## Non-goals
- No scene serializer implementation.
- No prefab/editor UI system.
- No live `AssetService`, runtime scene-manager, graphics/RHI, or platform dependencies in ECS.
- No immediate migration of `AssetInstance::Source::AssetId` to typed asset handles unless an architecture decision explicitly widens the ECS dependency contract.

## Context
- Owner/layer: `ecs`, with runtime/assets participating in ownership decisions where serialization and asset identity cross layer boundaries.
- Source review: [`docs/reviews/2026-05-13-src-ecs-gap-analysis.md`](../../../docs/reviews/2026-05-13-src-ecs-gap-analysis.md).
- Current `MetaData` stores only `EntityName` and `AssetInstance::Source::AssetId` is a raw `std::uint32_t` by design from `HARDEN-062`.
- Scene save/load, undo/redo, prefab references, hot reload, and external references need stable identity distinct from volatile `entt::entity` values.
- Runtime should own serialization mechanics, but ECS can own CPU-only identity and metadata descriptors.

## Required changes
- [ ] Decide the stable entity identity shape: UUID, scene-local integer, generation pair, or another deterministic value type.
- [ ] Define whether stable IDs are mandatory on all scene entities or optional authoring metadata.
- [ ] Define scene-local reference semantics for hierarchy, selection caches, serialized references, and future prefab/source provenance.
- [ ] Decide whether additional metadata belongs in `MetaData` or separate components such as `StableId`, `SceneSource`, or `SerializationHints`.
- [ ] Revisit the `AssetInstance::Source::AssetId` typing decision from `HARDEN-062`; if typed handles are desired, create/update a separate architecture task to widen `ecs` dependencies before implementation.
- [ ] Keep serializer IO and runtime scene-manager behavior outside ECS.

## Tests
- [ ] Add `tests/unit/ecs/Test.ECS.StableIdentity.cpp` or equivalent tests for default construction, equality/hashability, invalid/sentinel values, and deterministic assignment helpers if provided.
- [ ] Add contract tests if needed to prevent live asset-service/runtime/graphics imports in identity metadata.
- [ ] Keep tests CPU-only and labeled `unit;ecs` or `contract;ecs`.

## Docs
- [ ] Update `src/ecs/README.md` and `src/ecs/Components/README.md` with the stable identity and metadata policy.
- [ ] Update [`docs/architecture/ecs.md`](../../../docs/architecture/ecs.md) if identity/reference semantics become part of the ECS architecture contract.
- [ ] Update [`docs/migration/nonlegacy-parity-matrix.md`](../../../docs/migration/nonlegacy-parity-matrix.md) if stable identity changes scene serialization retirement blockers.
- [ ] Regenerate [`docs/api/generated/module_inventory.md`](../../../docs/api/generated/module_inventory.md) if modules are added, removed, renamed, or moved.

## Acceptance criteria
- [ ] ECS has a documented stable identity contract suitable for runtime serialization and external references.
- [ ] Identity metadata remains CPU-only and layer-clean.
- [ ] The raw-vs-typed asset ID decision is either unchanged and documented or moved into an explicit architecture follow-up before code depends on it.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicECSTests IntrinsicEcsContractTests
ctest --test-dir build/ci -L 'ecs|contract' --output-on-failure --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Implementing runtime scene serialization or editor prefab workflows in ECS.
- Adding live asset services, runtime scene managers, graphics/RHI handles, or GPU residency state to ECS metadata.
- Widening the `ecs` dependency contract to `assets` without an architecture decision and tooling/test updates.
- Treating `entt::entity` values as stable serialized identifiers.
