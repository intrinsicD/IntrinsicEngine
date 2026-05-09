# HARDEN-061 — Promote ECS hierarchy and transform system parity

## Goal
- Promote hierarchy mutation and transform hierarchy update behavior from legacy ECS into `src/ecs` with non-legacy tests.

## Non-goals
- No scene bootstrap API work beyond consuming the contract from `HARDEN-060` if it exists.
- No render extraction, graphics sync, or GPU residency behavior.
- No physics transform ownership or rigid-body integration.

## Context
- Status: in-progress.
- Owner/layer: `ecs`.
- Branch: `claude/setup-agentic-workflow-MinZP`.
- Promoted `src/ecs/Components/ECS.Component.Hierarchy.cppm` currently defines only data fields.
- Promoted `src/ecs/Systems/ECS.System.TransformHierarchy.*` is an empty placeholder.
- Legacy behavior exists in `src/legacy/ECS/Components/ECS.Components.Hierarchy.*`, `src/legacy/ECS/Systems/ECS.HierarchyTraversal.*`, and `src/legacy/ECS/Systems/ECS.Systems.Transform.*`.
- `docs/migration/nonlegacy-parity-matrix.md` lists hierarchy traversal and scene bootstrap as ECS retirement blockers.
- HARDEN-060 deferred dirty-transform observer/tag selection to this task; promoted `Components::Transform` exports `WorldUpdatedTag` and `Components::DirtyTags::DirtyTransform` (GPU-sync), but no CPU recompute marker exists yet.

## Contract decisions (Slice 1)
- **CPU recompute marker:** the promoted system uses `Extrinsic::ECS::Components::Transform::IsDirtyTag` (mirrors legacy `Transform::IsDirtyTag` semantics). It is distinct from the GPU-sync `Components::DirtyTags::DirtyTransform`; emitting that GPU-sync tag remains a runtime/render-sync responsibility and is out of scope here.
- **Pass name:** the promoted FrameGraph pass name is the string literal `"TransformUpdate"`. The legacy `Core.SystemFeatureCatalog` is not promoted; introducing a promoted catalog is deferred. `RegisterSystem(Extrinsic::Core::FrameGraph&, entt::registry&)` is exposed by the promoted system module and declares `Read<Transform::Component>`, `Read<Hierarchy::Component>`, `Write<Transform::WorldMatrix>`, `Write<Transform::IsDirtyTag>`, `Write<Transform::WorldUpdatedTag>`, and `Signal("TransformUpdate")`. Slice 1 ships the helper; activating it from a promoted simulate-phase bundle is Slice 2 work.
- **Module layering:** structural primitives (linked-list mutation, descendant walk, invariant check) live in a new `Extrinsic.ECS.Hierarchy.Structure` module that depends only on the hierarchy component and scene handle; the public mutation API in `Extrinsic.ECS.Hierarchy.Mutation` depends additionally on the transform component module so reparenting can preserve world position. This mirrors the legacy `:HierarchyStructure` / `:Components.Hierarchy` split inside the promoted layer and keeps structural-only callers off the transform/glm dependency.

## Slice plan
- **Slice 1 (this PR)** — Promote hierarchy mutation + transform traversal:
  - Add `Components::Transform::IsDirtyTag` to `Extrinsic.ECS.Component.Transform`.
  - Add module `Extrinsic.ECS.Hierarchy.Structure` (`AttachToParent`, `DetachFromParent`, `IsDescendant`, `ValidateInvariants`).
  - Add module `Extrinsic.ECS.Hierarchy.Mutation` (`Attach`, `Detach`) with cycle rejection, sibling-chain maintenance, world-position preservation across reparenting, and singular-parent fallback.
  - Implement `Extrinsic.ECS.System.TransformHierarchy::OnUpdate(entt::registry&)` (root-rooted DFS, parent-aware dirty propagation, `WorldUpdatedTag` emission, `IsDirtyTag` clearing) and `RegisterSystem(FrameGraph&, registry&)`.
  - Wire new modules into `src/ecs/CMakeLists.txt`, `Components/CMakeLists.txt`, `Systems/CMakeLists.txt`; add `ExtrinsicCore` link scope to the systems target.
  - Add `tests/unit/ecs/Test.ECS.Hierarchy.cpp` and `tests/unit/ecs/Test.ECS.TransformHierarchy.cpp`; wire both into `ECSTestObjs`.
  - Update `src/ecs/README.md`, `src/ecs/Systems/README.md`, and the ECS row of `docs/migration/nonlegacy-parity-matrix.md`.
  - Regenerate `docs/api/generated/module_inventory.md`.
- **Slice 2 (follow-up)** — Activate the promoted `RegisterSystem` from the promoted simulate-phase bundle once that surface lands; introduce a promoted system-feature catalog or pass-token registry if shared identifiers are needed; reconcile GPU-sync `DirtyTags::DirtyTransform` emission with the promoted update.

## Required changes
- Add promoted hierarchy `Attach`/`Detach` APIs and structural primitives in `src/ecs`.
- Preserve cycle rejection, self-parent rejection, reparenting, null-parent detach, sibling-chain maintenance, child-count invariants, and singular-parent fallback in mutation.
- Implement promoted transform hierarchy update over local transform, world matrix, hierarchy, `IsDirtyTag`, and `WorldUpdatedTag`.
- Expose the promoted `RegisterSystem` against `Extrinsic.Core.FrameGraph`; initial activation is gated on Slice 2's simulate-phase bundle wiring.
- Replace stale migration-era comments in promoted ECS system files with current-state documentation.

## Tests
- Add or update `tests/unit/ecs/Test.ECS.Hierarchy.cpp`.
- Add or update `tests/unit/ecs/Test.ECS.TransformHierarchy.cpp`.
- Port the relevant non-legacy assertions from `tests/unit/ecs/Test_RuntimeECS.cpp`: attach/detach, reparenting, cycle detection, dirty propagation, world matrix update, singular parent rejection, and world-position preservation.

## Docs
- Update `src/ecs/README.md` and `src/ecs/Systems/README.md` with factual status for hierarchy/transform systems.
- Update `docs/migration/nonlegacy-parity-matrix.md` when promoted tests prove parity.

## Acceptance criteria
- Promoted ECS hierarchy mutations and transform traversal work without importing legacy `ECS`.
- Transform updates are deterministic for root and child entities and clear/emit the documented dirty tags.
- Existing legacy compatibility tests may remain, but promoted tests cover the new source surface directly.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicECSTests
ctest --test-dir build/ci -L 'ecs' --output-on-failure --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Deleting or moving legacy ECS files as part of this semantic port.
- Introducing runtime, graphics, platform, or app imports into `src/ecs`.
- Adding rigid-body or general physics stepping to the transform system.
- Emitting `Components::DirtyTags::DirtyTransform` (GPU-sync) from the promoted CPU traversal — that remains a render-sync/runtime concern.

## Execution log
- 2026-05-09: Promoted task to `tasks/active/`. Recorded contract decisions and Slice 1 plan above.
- 2026-05-09: Slice 1 implemented:
  - Added CPU recompute marker `Components::Transform::IsDirtyTag` in `Extrinsic.ECS.Component.Transform` and clarified the GPU-sync separation in module comments.
  - Added module `Extrinsic.ECS.Hierarchy.Structure` (linked-list mutation primitives, descendant walk, invariant check) and module `Extrinsic.ECS.Hierarchy.Mutation` (`Attach`/`Detach` with cycle rejection, sibling-chain maintenance, world-position preservation, singular-parent identity fallback).
  - Implemented `Extrinsic.ECS.System.TransformHierarchy::OnUpdate` (root-rooted DFS, parent-aware dirty propagation, `WorldUpdatedTag` emission, `IsDirtyTag` clearing) and `RegisterSystem(FrameGraph&, registry&)` declaring `Read<Transform::Component>`, `Read<Hierarchy::Component>`, `Write<Transform::WorldMatrix>`, `Write<Transform::IsDirtyTag>`, `Write<Transform::WorldUpdatedTag>`, and `Signal("TransformUpdate")`.
  - Wired new modules into `src/ecs/CMakeLists.txt`; `src/ecs/Systems/CMakeLists.txt` now links `ExtrinsicCore` for the FrameGraph import.
  - Added `tests/unit/ecs/Test.ECS.Hierarchy.cpp` (attach/detach/cycle/sibling-chain/reparent/world-preservation/singular-parent/Validate/IsDescendant) and `tests/unit/ecs/Test.ECS.TransformHierarchy.cpp` (root recompute, clean-skip, child = parent × local, parent-dirty propagation, GPU-sync isolation, transformless-entity safety); both wired into `ECSTestObjs`.
  - Updated `src/ecs/README.md`, `src/ecs/Systems/README.md`, ECS row of `docs/migration/nonlegacy-parity-matrix.md`, and the backlog convergence map; regenerated `docs/api/generated/module_inventory.md`.
  - Structural checks clean in this session: `tools/agents/check_task_policy.py --strict`, `tools/repo/check_layering.py --strict`, `tools/repo/check_test_layout.py --strict`, `tools/docs/check_doc_links.py`.

## Next verification step
- CI must run `cmake --preset ci` + `cmake --build --preset ci --target IntrinsicECSTests` + `ctest --test-dir build/ci -L 'ecs' --output-on-failure --timeout 60`. Local sandbox lacks `clang-20` (default-preset compiler) so the C++ build is deferred to CI for this slice.

