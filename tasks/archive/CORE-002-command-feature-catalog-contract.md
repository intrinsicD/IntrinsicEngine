# CORE-002 — Command and feature catalog contract

## Goal
- Replace the useful parts of legacy `Core.Commands`, `Core.FeatureRegistry`, and `Core.SystemFeatureCatalog` with layer-appropriate promoted contracts or explicit retirement decisions.

## Non-goals
- No engine-wide mutable service locator in `core`.
- No graphics, runtime, ECS, or UI imports from `src/core`.
- No migration of editor undo/redo behavior in this task; runtime/editor command history is owned by `RUNTIME-102`.
- No legacy module-name compatibility wrapper.

## Context
- Owner/layer: `core` contracts plus architecture decision; any concrete runtime/editor command use belongs to `runtime` or `ui`.
- The promoted code already has `Core.CallbackRegistry`, `Core.Dag.TaskGraph`, `Core.Dag.Scheduler`, `Core.FrameLoop`, and configuration modules. Those should be reused instead of copying the legacy global feature registry design.
- The legacy feature catalog mixed render passes, ECS systems, panels, geometry operators, GPU memory presets, and shader hot reload toggles. Under `AGENTS.md`, those responsibilities are now split across `runtime`, `graphics`, `ecs`, `ui`, and config.
- This task is a retirement gate for `LEGACY-005` and a blocker-input for `LEGACY-011`.

## Value gate
- Current state: promoted core already has callback, config, frame-loop, DAG, scheduler, task, and utility seams.
- Improvement: keep `core` dependency-free while preserving only reusable descriptors/helpers that have a concrete promoted consumer.
- Scope decision: do not recreate the legacy feature catalog. If no current consumer needs a dependency-free core contract, record retirement and route command behavior to `RUNTIME-102`.

## Required changes
- [x] Inventory remaining non-legacy tests or code importing `Core.Commands`, `Core.FeatureRegistry`, `Core.SystemFeatureCatalog`, `Core.InplaceFunction`, legacy profiling helpers, or utility containers.
- [x] Decide which behavior is still needed as promoted `core` API, which belongs in `runtime`/`ui`, and which is explicitly retired as legacy-only.
- [x] If a narrow promoted `core` contract is needed, implement only value-type descriptors or dependency-free helpers under `src/core` with unit tests.
- [x] Route runtime/editor command history requirements to `RUNTIME-102` instead of adding `entt` or editor state to `core`.
- [x] Update legacy retirement docs with the final keep/remove decision.

## Tests
- [x] Add or update `unit;core` coverage for any new promoted `core` descriptors/helpers.
- [x] Add a regression grep or task note proving no promoted `src/core` file imports ECS, runtime, graphics, platform, assets, or UI.
- [x] Preserve existing `Core.CallbackRegistry`, DAG, frame-loop, and config tests.

## Docs
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` for the `core` row.
- [x] Update `src/core/README.md` if the public core surface changes.
- [x] Regenerate `docs/api/generated/module_inventory.md` if modules are added, removed, or renamed.

## Acceptance criteria
- [x] Legacy command/catalog behavior has one of three outcomes: promoted narrow core API, reassigned runtime/UI task, or explicit retirement decision.
- [x] No promoted core module depends on `entt`, ImGui, graphics, runtime, ECS, assets, or platform.
- [x] `LEGACY-005` no longer has unnamed command/catalog blockers after this task retires.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L 'unit' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Status
- Completed: 2026-06-09 at `CPUContracted` / explicit retirement decision.
- PR/commit: this retirement commit.
- Inventory: the promoted-code consumers found for this slice were
  `src/geometry/Geometry.Octree.cpp` (`Utils.BoundedHeap`),
  `src/geometry/Geometry.SDFContact.cppm` (`Core.Telemetry`), and
  `src/geometry/Geometry.HtexPatch.cpp` (`Core.Profiling.Macros.hpp`).
  They now import `Extrinsic.Core.BoundedHeap` and
  `Extrinsic.Core.Telemetry` through a layer-valid `geometry -> core` edge.
- Decision: no promoted global command or feature catalog is added. Legacy
  `Core.Commands` undo/redo behavior is owned by retired `RUNTIME-102`;
  render passes, ECS systems, editor panels, geometry operators, GPU memory
  presets, and shader hot-reload toggles stay with their owning runtime,
  graphics, ECS, UI, or config layers.
- Remaining bare legacy imports in legacy subtrees and compatibility tests are
  not command/catalog feature blockers. They are consumer cleanup/deletion
  blockers for `LEGACY-005`, `LEGACY-012`, and the relevant subtree tasks.
- Verification:
  - `cmake --preset ci` passed.
  - `cmake --build --preset ci --target IntrinsicTests` passed.
  - `ctest --test-dir build/ci --output-on-failure -L 'unit' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` passed (`1987/1987`).
  - `python3 tools/repo/check_layering.py --root src --strict` passed.
  - `python3 tools/repo/check_test_layout.py --root . --strict` passed.
  - `python3 tools/agents/validate_tasks.py --root tasks --strict` passed.
  - `python3 tools/agents/check_task_policy.py --root . --strict` passed.
  - `python3 tools/agents/check_task_state_links.py --root . --strict` passed.
  - `python3 tools/docs/check_doc_links.py --root .` passed.
  - `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main` passed.
  - `git diff --check` passed.

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Adding a global mutable feature registry that lower layers or plugins mutate at runtime.
- Importing higher layers from `src/core`.

## Maturity
- Reached: `CPUContracted` for the retained promoted utility/telemetry seam;
  explicit `Retired` / reassigned decision for global command/catalog behavior.
- No `Operational` follow-up is owed for dependency-free core contracts.
