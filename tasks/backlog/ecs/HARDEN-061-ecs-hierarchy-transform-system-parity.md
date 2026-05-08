# HARDEN-061 — Promote ECS hierarchy and transform system parity

## Goal
- Promote hierarchy mutation and transform hierarchy update behavior from legacy ECS into `src/ecs` with non-legacy tests.

## Non-goals
- No scene bootstrap API work beyond consuming the contract from `HARDEN-060` if it exists.
- No render extraction, graphics sync, or GPU residency behavior.
- No physics transform ownership or rigid-body integration.

## Context
- Owner/layer: `ecs`.
- Promoted `src/ecs/Components/ECS.Component.Hierarchy.cppm` currently defines only data fields.
- Promoted `src/ecs/Systems/ECS.System.TransformHierarchy.*` is an empty placeholder.
- Legacy behavior exists in `src/legacy/ECS/Components/ECS.Components.Hierarchy.*`, `src/legacy/ECS/Systems/ECS.HierarchyTraversal.*`, and `src/legacy/ECS/Systems/ECS.Systems.Transform.*`.
- `docs/migration/nonlegacy-parity-matrix.md` lists hierarchy traversal and scene bootstrap as ECS retirement blockers.

## Required changes
- Add promoted hierarchy `Attach`/`Detach` APIs or an equivalent explicit structural mutation API in `src/ecs`.
- Preserve cycle rejection, self-parent rejection, reparenting, null-parent detach, sibling-chain maintenance, and child-count invariants.
- Implement promoted transform hierarchy update over local transform, world matrix, hierarchy, and dirty/world-updated tags.
- Define the promoted system registration contract against `Extrinsic.Core.FrameGraph` only if the current promoted frame-graph surface supports it; otherwise keep registration as a separate follow-up and document the blocker.
- Replace stale `src_new` comments in promoted ECS system files with current-state documentation.

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

