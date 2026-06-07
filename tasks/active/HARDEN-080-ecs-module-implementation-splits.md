# HARDEN-080 — ECS module implementation splits

## Goal
- Move worthy non-trivial, non-template implementations out of promoted ECS `.cppm` interfaces into matching `.cpp` implementation units, and clean up includes/imports that become implementation-only.

## Non-goals
- No ECS component, scene, or registry behavior changes.
- No runtime sidecars, graphics handles, physics-world handles, live asset-service traffic, or platform/app dependencies.
- No broad core, geometry, graphics, runtime, platform, physics, or legacy cleanup in this task.
- No attempt to move templates or bodies that must remain visible to importers.

## Context
- Status: blocked (current implementation-split slice complete locally; retirement blocked by default CPU CTest failure on 2026-06-07).
- Owner/agent: Codex.
- Branch / PR: current branch / TBD.
- Next verification step: resolve the `src/graphics/framegraph/Graphics.RenderGraph.Compiler.cpp:1565` ASan heap-buffer-overflow default-gate blocker, then rerun the default CPU gate and retire this task if clean.
- Owning subsystem/layer: `ecs` (`ecs -> core`; geometry handles/types only when explicitly required).
- `AGENTS.md` requires non-trivial `.cppm` bodies to live in implementation units when they do not need importer visibility.
- Static audit on 2026-06-06 identified these promoted ECS cleanup targets:
  `src/ecs/Components/ECS.Component.GeometrySources.cppm`,
  `src/ecs/Components/ECS.Component.StableId.cppm`, and
  `src/ecs/ECS.Scene.Registry.cppm`.
- `ECS.Component.GeometrySources` includes domain/view builders with live registry reads; keep the public records visible, but move non-trivial query/build bodies when possible.

## Required changes
- [x] Add matching `.cpp` implementation units for audited ECS modules when none exist, or reuse existing implementation files if present.
- [x] Move non-template bodies for `GeometrySources` domain/view detection/building, `StableId` validity/hash helpers, and scene-registry create/clear helpers where they no longer need inline visibility.
- [x] Register any newly added `.cpp` files as private sources in `src/ecs/CMakeLists.txt`.
- [x] Keep component POD/value declarations in `.cppm`; move only implementation/control-flow bodies.
- [x] Clean up global-module-fragment includes in touched `.cppm` files, moving implementation-only headers to `.cpp`.
- [x] Audit imports in touched `.cppm` files and keep only public-surface imports in the interface.

## Tests
- [ ] Existing ECS unit/contract tests remain green.
- [ ] Existing runtime/graphics extraction tests that consume `GeometrySources` remain green if they are affected by compile fallout.
- [ ] Add no new behavior tests unless the split exposes an untested contract seam or a bug.

## Docs
- [x] Update ECS architecture docs only if public behavior or ownership wording changes.
- [x] Regenerate `docs/api/generated/module_inventory.md` if module imports/surfaces change.
- [x] Update this task with completion notes before retirement.

## Acceptance criteria
- [ ] Touched ECS `.cppm` files expose declarations/value types/templates only, or record a justified retained exception.
- [ ] Any new `.cpp` implementation units are registered in CMake and compile under the `ci` preset.
- [ ] Touched `.cppm` files no longer include/import implementation-only dependencies.
- [ ] ECS and affected extraction tests pass without changed expectations.
- [ ] The change preserves ECS layering and introduces no live higher-layer ownership.

## Progress notes
- 2026-06-07: Current implementation-split slice completed locally: moved non-trivial non-template bodies into matching `.cpp` implementation units, registered new private sources in CMake, and retained importer-visible templates, constexpr helpers, ABI structs, and small accessors in module interfaces where required.
- Focused target builds passed for the touched subsystem, and `docs/api/generated/module_inventory.md` was regenerated with `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`.
- Retirement blocker: current-session verification on 2026-06-07 passed configure, `IntrinsicTests` build, generated-inventory, task-policy, layering, test-layout, doc-link, and diff-whitespace checks. The default CPU CTest gate failed with 159 failed tests out of 2816; the failures reproduce the existing `src/graphics/framegraph/Graphics.RenderGraph.Compiler.cpp:1565` ASan heap-buffer-overflow during render-graph compile, reached through `src/graphics/framegraph/Graphics.RenderGraph.cpp:405`, and cascade through graphics/runtime tests.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ECS|GeometrySources|StableId|Scene' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not change ECS component semantics, stable identity behavior, or scene-registry ownership rules.
- Do not add runtime, graphics, RHI, platform, app, live asset-service, or physics-world dependencies.
- Do not move templates or importer-required definitions into `.cpp`.
- Do not mix this mechanical implementation split with semantic ECS hardening.
