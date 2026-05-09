# HARDEN-060 — Promote ECS scene bootstrap contract

## Goal
- Promote the default entity creation/bootstrap contract from legacy `ECS:Scene`/`ECS:SceneBootstrap` into `src/ecs` with focused tests.

## Non-goals
- No physics or simulation behavior.
- No runtime scene-manager, serializer, editor, or graphics residency changes.
- No mechanical deletion of legacy ECS modules.

## Context
- Status: done.
- Owner/layer: `ecs`.
- Branch: `claude/setup-agentic-workflow-uMvqx` (merged via PR #780).
- `src/ecs/ECS.Scene.Registry.cppm` currently exposes only create/destroy/clear plus explicit `Raw()` access.
- Legacy `src/legacy/ECS/ECS.SceneBootstrap.*` defines the default entity contract: name metadata, local transform, world matrix, dirty transform marker, and hierarchy component.
- Promoted ECS must remain reusable and must not import graphics, platform, runtime, or app layers.

## Contract decisions (Slice 1)
- **Naming**: the promoted bootstrap uses `Extrinsic::ECS::Components::MetaData` (field `EntityName`). Legacy `Components::NameTag::Component { Name }` stays in `src/legacy/ECS` until that subsystem retires; it is **not** ported into promoted ECS.
- **Dirty-transform marker**: bootstrap deliberately does **not** emplace any dirty-transform tag at create time. The legacy "needs world recompute" `Transform::IsDirtyTag` has no promoted equivalent yet; the GPU-sync `DirtyTags::DirtyTransform` and post-update `Transform::WorldUpdatedTag` are emitted by the promoted `TransformHierarchy` system, not by bootstrap. Selecting which CPU recompute marker (if any) the promoted system observes is owned by `HARDEN-061`. Tests assert this absence to lock the contract.
- **Default components emplaced**: `MetaData{name}`, `Transform::Component{}` (identity TRS), `Transform::WorldMatrix{}` (identity matrix), `Hierarchy::Component{}` (no parent/children).
- **Module export hygiene**: `Extrinsic.ECS.Component.MetaData` and `Extrinsic.ECS.Component.Hierarchy` currently declare their types in non-exported namespaces. Slice 1 adds `export` so external modules (the new bootstrap and the new test) can name these types. No type semantics change.

## Slice plan
- **Slice 1 (this PR)** — Promote bootstrap API:
  - Add `export` to the namespace blocks of `ECS.Component.MetaData.cppm` and `ECS.Component.Hierarchy.cppm` to make the existing types reachable from importers.
  - Add `src/ecs/ECS.Scene.Bootstrap.cppm` declaring `Extrinsic::ECS::Scene::EmplaceDefaults(Registry&, EntityHandle, std::string_view name)` and `CreateDefault(Registry&, std::string_view name)`.
  - Add `src/ecs/ECS.Scene.Bootstrap.cpp` with the implementation (no graphics, no runtime).
  - Wire the new module into `src/ecs/CMakeLists.txt`.
  - Add `tests/unit/ecs/Test.ECS.SceneBootstrap.cpp` and wire it into `ECSTestObjs`.
  - Update `src/ecs/README.md` and `docs/migration/nonlegacy-parity-matrix.md` for the new module surface.
- **Slice 2 (follow-up, gated by HARDEN-061)** — Reconcile dirty-transform observer/tag semantics when the promoted hierarchy system actually consumes/emits transform tags. Out of scope here.

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
- 2026-05-09: Promoted task to `tasks/active/`. Recorded contract decisions and the Slice 1 plan above.
- 2026-05-09: Slice 1 implemented:
  - Added `export` to `Extrinsic.ECS.Component.MetaData` and `Extrinsic.ECS.Component.Hierarchy` namespace blocks (no type changes).
  - Added module `Extrinsic.ECS.Scene.Bootstrap` (`EmplaceDefaults`, `CreateDefault`) under `src/ecs/`, wired into `ExtrinsicECS` CMake.
  - Added `tests/unit/ecs/Test.ECS.SceneBootstrap.cpp` covering default component presence, custom name, identity world matrix, default hierarchy linkage, absence of dirty-transform tags, invalid-handle behavior, and `Destroy`/`Clear` interactions; wired into `ECSTestObjs`.
  - Updated `src/ecs/README.md` to list the new module.
  - Updated `docs/migration/nonlegacy-parity-matrix.md` row for `ecs` to include `Extrinsic.ECS.Scene.Bootstrap` and to note bootstrap parity is now covered.

## Next verification step
- CI must run `cmake --preset ci` + `cmake --build --preset ci --target IntrinsicECSTests` + `ctest --test-dir build/ci -L 'ecs' --output-on-failure --timeout 60`. Local sandbox lacks `clang-20` (default-preset compiler) so the C++ build is deferred to CI for this slice; structural checks (`tools/agents/check_task_policy.py --strict`, `tools/repo/check_layering.py --strict`, `tools/repo/check_test_layout.py --strict`, `tools/docs/check_doc_links.py`) ran clean in this session.

## Completion
- Completed: 2026-05-09.
- Status: done.
- Implementation commit: `de626eb` (`HARDEN-060: promote ECS scene bootstrap contract (Slice 1)`), merged via PR #781 (`8f38403`).
- Slice 2 (dirty-transform observer/tag reconciliation) was deliberately scoped as "out of scope here" in the original slice plan and was effectively settled by `HARDEN-061` Slice 1's selection of `Components::Transform::IsDirtyTag` as the CPU recompute marker. The bootstrap contract continues to deliberately omit any dirty-transform tag at create time because newly created entities have identity local TRS = identity world matrix, so no recompute is required; `Test.ECS.SceneBootstrap` locks this absence.
- Verified in tree at retirement:
  - `src/ecs/ECS.Scene.Bootstrap.cppm` exports `Extrinsic::ECS::Scene::EmplaceDefaults(Registry&, EntityHandle, std::string_view)` and `CreateDefault(Registry&, std::string_view)`; impl in `src/ecs/ECS.Scene.Bootstrap.cpp` emplaces `MetaData{name}`, `Transform::Component{}`, `Transform::WorldMatrix{}`, and `Hierarchy::Component{}` only.
  - `Extrinsic.ECS.Component.MetaData` and `Extrinsic.ECS.Component.Hierarchy` namespace blocks are exported.
  - `tests/unit/ecs/Test.ECS.SceneBootstrap.cpp` is wired into `ECSTestObjs` and exercises default-component presence, custom name, identity world matrix, default hierarchy linkage, dirty-transform tag absence, invalid-handle behavior, and `Destroy`/`Clear` interactions.
  - `src/ecs/README.md` lists `Extrinsic.ECS.Scene.Bootstrap`; `docs/migration/nonlegacy-parity-matrix.md` ECS row records bootstrap parity.
  - `python3 tools/agents/check_task_policy.py --root . --strict` — passed at retirement.
  - `python3 tools/docs/check_doc_links.py --root .` — passed at retirement.
  - `python3 tools/repo/check_layering.py --root src --strict` — passed at retirement.
  - `python3 tools/repo/check_test_layout.py --root . --strict` — passed at retirement.
