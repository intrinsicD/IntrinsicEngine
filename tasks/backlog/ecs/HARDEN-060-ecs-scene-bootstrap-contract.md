# HARDEN-060 — Promote ECS scene bootstrap contract

## Goal
- Promote the default entity creation/bootstrap contract from legacy `ECS:Scene`/`ECS:SceneBootstrap` into `src/ecs` with focused tests.

## Non-goals
- No physics or simulation behavior.
- No runtime scene-manager, serializer, editor, or graphics residency changes.
- No mechanical deletion of legacy ECS modules.

## Context
- Owner/layer: `ecs`.
- `src/ecs/ECS.Scene.Registry.cppm` currently exposes only create/destroy/clear plus explicit `Raw()` access.
- Legacy `src/legacy/ECS/ECS.SceneBootstrap.*` defines the default entity contract: name metadata, local transform, world matrix, dirty transform marker, and hierarchy component.
- Promoted ECS must remain reusable and must not import graphics, platform, runtime, or app layers.

## Required changes
- Wire existing promoted ECS registry coverage, including `tests/unit/ecs/Test.ECS.SceneRegistry.cpp`, into the appropriate ECS test target before relying on it as verification evidence.
- Add a promoted scene bootstrap API under `src/ecs` that creates or initializes an entity with the documented default components.
- Decide and document the promoted naming component contract (`Components::MetaData::EntityName` versus legacy `NameTag::Component`).
- Reconcile dirty-transform naming between promoted `DirtyTags::DirtyTransform`, `Transform::WorldUpdatedTag`, and legacy `Transform::IsDirtyTag` before porting tests.
- Keep `Registry::Raw()` as the explicit privileged escape hatch; do not hide broad entt access behind convenience APIs unless each API has a tested lifecycle contract.
- Update `src/ecs/README.md` and `docs/migration/nonlegacy-parity-matrix.md` if the public module surface changes.

## Tests
- Add or update `tests/unit/ecs/Test.ECS.SceneBootstrap.cpp`.
- Cover default component presence, custom entity name, dirty-transform marker, world matrix initialization, hierarchy initialization, invalid-handle behavior, and interaction with `Registry::Destroy`/`Registry::Clear`.

## Docs
- Update `src/ecs/README.md` with the scene bootstrap module/API.
- Update migration parity notes only with factual current state and remaining blockers.

## Acceptance criteria
- Promoted ECS can create a fully initialized scene entity without importing legacy `ECS`.
- Default entity semantics are tested outside legacy `Test_RuntimeECS.cpp` compatibility coverage.
- No new dependency edge from `ecs` to graphics, platform, runtime, or app is introduced.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicECSTests
ctest --test-dir build/ci -L 'ecs' --output-on-failure --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing this semantic promotion with legacy file deletion.
- Adding physics components or rigid-body integration in this task.
- Adding graphics-owned handles or GPU residency state to ECS components.

## Execution log
- 2026-05-09: Wired `tests/unit/ecs/Test.ECS.SceneRegistry.cpp` into `ECSTestObjs`/`IntrinsicECSTests` with the `ExtrinsicECS` module dep so promoted scene-registry coverage runs in the default ECS test target. Remaining required changes (scene bootstrap API, naming/dirty-tag reconciliation, README/parity docs) are unstarted.
