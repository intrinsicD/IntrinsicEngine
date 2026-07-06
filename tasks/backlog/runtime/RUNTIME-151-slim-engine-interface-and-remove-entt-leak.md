---
id: RUNTIME-151
theme: F
depends_on:
  - RUNTIME-146
  - RUNTIME-147
  - RUNTIME-148
  - RUNTIME-149
  - RUNTIME-150
maturity_target: Operational
---
# RUNTIME-151 — Slim the Engine module interface and remove the entt leak

## Goal
- After the `RUNTIME-146..150` extractions land, shrink
  `Runtime.Engine.cppm` to the composition-root surface (lifecycle,
  subsystem accessors, frame diagnostics): remove the
  `entt` includes from the global module fragment by moving StableId
  signal tracking behind a runtime-owned binding type, and cut the
  interface import list to what the remaining declarations need.

## Non-goals
- No public API redesign beyond deletions already owned by
  `RUNTIME-146..150` — this task removes implementation leakage, it does
  not move more facades.
- No wholesale pimpl of `Engine` unless the entt/member cleanup below
  proves insufficient; a full pimpl is a separate decision (ADR-worthy)
  and out of scope here.
- No change to `StableEntityLookup` resolution semantics or diagnostics.

## Context
- Owning subsystem/layer: `runtime`.
- The interface leak: `src/runtime/Runtime.Engine.cppm` includes
  `entt/entity/registry.hpp` and `entt/signal/sigh.hpp` in its global
  module fragment solely for private members — three
  `entt::scoped_connection` fields and the `OnStableIdConstruct/Update/
  Destroy(entt::registry&, entt::entity)` private methods. Every importer
  of `Extrinsic.Runtime.Engine` re-parses entt headers because of
  private plumbing.
- Fix shape: extend `Extrinsic.Runtime.StableEntityLookup` (or add a
  sibling type in that module) with a scene-binding object that owns the
  scoped connections and the signal callbacks, constructed by `Engine`
  around scene creation/replacement. `Engine` then holds one value member
  of that type and the entt headers leave the interface.
- Compile-cost evidence: `Runtime.Engine.cppm` was already a top-10
  compile hotspot at 215 lines / 25 imports
  (`tools/analysis/build_time_baseline_2026-04-05.md`); it is ~1,070
  lines today. After `RUNTIME-146..150` remove moved types, this task
  re-audits every remaining `import` in the interface and drops the ones
  only implementation units need.
- In-src importers to re-verify after slimming:
  `Runtime.SandboxDefaultPolicies`, `Runtime.SandboxEditorUi`,
  `src/app/Sandbox/*`.
- Part of the `Runtime.Engine` decomposition series (`RUNTIME-146..151`);
  this is the only semantic slice and must land last.

## Required changes
- [ ] Add the scene-binding type to the `StableEntityLookup` module owning
      the three `entt::scoped_connection`s and the construct/update/destroy
      callbacks; `Engine` constructs/rebinds it where
      `ConnectStableEntityLookupTracking` /
      `RebuildStableEntityLookupAfterSceneReplacement` run today.
- [ ] Remove `entt/entity/registry.hpp` and `entt/signal/sigh.hpp` from
      the `Runtime.Engine.cppm` global module fragment and delete the
      private entt-typed members/methods from the class body.
- [ ] Re-audit the interface `import` list; move imports only needed by
      implementation units into those units.
- [ ] Record before/after interface line count and import count in this
      task file at retirement.

## Tests
- [ ] `Test.SelectionStableLookupComposition.cpp` and
      `Test.SelectionSnapshotExtraction.cpp` pass with only import/name
      updates (incremental tracking and scene-replacement rebuild behavior
      preserved).
- [ ] Default CPU gate stays green:
      `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine'`.

## Docs
- [ ] Regenerate module inventories per `intrinsicengine-docs-sync`.
- [ ] Update `tasks/backlog/runtime/README.md` status line on retirement.

## Acceptance criteria
- [ ] `Runtime.Engine.cppm` contains no `entt` include and no
      `entt`-typed declaration.
- [ ] Interface import count is reduced versus the pre-series baseline
      (recorded numbers in this file).
- [ ] StableId lookup maintenance behavior (event-driven incremental
      updates, whole-scene rebuild at replacement) is unchanged per the
      named tests.
- [ ] CPU gate and layering check pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
grep -c "^import" src/runtime/Runtime.Engine.cppm
```

## Forbidden changes
- Starting this task before `RUNTIME-146..150` are retired (it would
  collide with every one of them in `Runtime.Engine.{cppm,cpp}`).
- Changing lookup/selection semantics while moving the tracking.
- Introducing a compatibility re-export shim for moved names.

## Maturity
- Target: `Operational` — behavior-preserving; the operational engine loop
  and lookup tests gate it. No new capability follow-up is owed.
