# HARDEN-079 — Core module implementation splits

## Goal
- Move worthy non-trivial, non-template implementations out of promoted `src/core/*.cppm` module interfaces into matching `.cpp` implementation units, and clean up includes/imports that become implementation-only.

## Non-goals
- No core API behavior changes.
- No public type, layout, module-name, or dependency-boundary changes.
- No broad geometry, graphics, runtime, platform, ECS, physics, or legacy cleanup in this task.
- No attempt to move templates or constexpr bodies that must remain visible to importers.

## Context
- Status: done (retired 2026-06-07; implementation split landed in `bfcd2751`).
- Owner/agent: Codex.
- Branch / PR: current branch / TBD.
- Next verification step: none; retired after a clean no-cache rebuild, explicit benchmark-smoke target build, and default CPU gate on 2026-06-07.
- Owning subsystem/layer: `core` (`core -> nothing`). Filed under architecture because there is no dedicated core backlog directory.
- `AGENTS.md` requires `.cppm` interfaces to stay focused on declarations, small inline accessors, and templates. Non-trivial control-flow, container traversal/mutation, diagnostics assembly, and implementation-only imports belong in `.cpp` implementation units.
- Static audit on 2026-06-06 identified these promoted core cleanup targets:
  `src/core/Core.BoundedHeap.cppm`,
  `src/core/Core.Dag.Scheduler.Hazards.cppm`,
  `src/core/Core.Error.cppm`,
  `src/core/Core.FrameGraph.cppm`,
  `src/core/Core.FrameLoop.cppm`,
  `src/core/Core.HandleLease.cppm`,
  `src/core/Core.Hash.cppm`,
  `src/core/Core.Memory.Polymorphic.cppm`,
  `src/core/Core.ResourcePool.cppm`,
  `src/core/Core.Tasks.LocalTask.cppm`, and
  `src/core/Core.Telemetry.cppm`.
- `Core.FrameGraph.cppm` already has `src/core/Core.FrameGraph.cpp`; its audited target is `HashTypeSig` at line 55. Prefer moving targets into existing implementation units before creating new ones.

## Required changes
- [x] For targets that already have matching implementation units, move audited non-trivial bodies into the existing `.cpp`: `Core.FrameGraph`, `Core.Memory.Polymorphic`, `Core.Tasks.LocalTask`, and `Core.Telemetry`.
- [x] For targets without matching implementation units, add `.cpp` files only where the moved bodies justify a separate implementation unit: `Core.BoundedHeap`, `Core.Dag.Scheduler.Hazards`, `Core.Error`, `Core.FrameLoop`, `Core.HandleLease`, `Core.Hash`, and `Core.ResourcePool`.
- [x] Register any newly added `.cpp` files as private sources in `src/core/CMakeLists.txt`.
- [x] Keep template-dependent storage/container helpers in `.cppm` where importer visibility is required; document any intentionally retained non-moved body in this task before retirement.
- [x] Remove `inline` from moved non-template exported declarations where no longer required.
- [x] Clean up global-module-fragment includes in touched `.cppm` files, moving implementation-only headers into matching `.cpp` files.
- [x] Audit imports in touched `.cppm` files and move implementation-only imports into `.cpp` when the public surface no longer requires them.

## Tests
- [x] Existing core unit and contract tests remain green.
- [x] Add no new behavior tests unless the split exposes an untested core contract seam or a bug.
- [x] If any task/frame-loop behavior is touched, run focused frame-graph/task tests in addition to the default CPU gate.

## Docs
- [x] Update architecture docs only if a public core contract or module surface wording changes.
- [x] Regenerate `docs/api/generated/module_inventory.md` if module imports/surfaces change.
- [x] Update this task with completion notes before retirement.

## Acceptance criteria
- [x] Touched core `.cppm` files expose declarations, templates, and small inline accessors only, or record a justified retained exception.
- [x] Any new `.cpp` implementation units are registered in CMake and compile under the `ci` preset.
- [x] Touched `.cppm` files no longer include/import implementation-only dependencies.
- [x] Core tests pass without changed expectations.
- [x] The change preserves `core -> nothing` layering.

## Progress notes
- 2026-06-07: Current implementation-split slice completed locally: moved non-trivial non-template bodies into matching `.cpp` implementation units, registered new private sources in CMake, and retained importer-visible templates, constexpr helpers, ABI structs, and small accessors in module interfaces where required.
- Focused target builds passed for the touched subsystem, and `docs/api/generated/module_inventory.md` was regenerated with `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`.
- 2026-06-07 retirement verification: the previous rendergraph ASan blocker was reproduced as stale C++23 module layout state from ccache/incremental module artifacts, not a source defect in the implementation split. A `CCACHE_DISABLE=1` `IntrinsicTests` rebuild plus an explicit `IntrinsicBenchmarkSmoke` build restored a clean default CPU gate; final `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` passed 2816/2816.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Core|FrameGraph|TaskGraph|FrameLoop|Telemetry|ResourcePool' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not alter core behavior, scheduling semantics, hash values, frame-loop ordering, or telemetry counters.
- Do not move templates or constexpr importer-required definitions into `.cpp`.
- Do not introduce dependencies from core to any other engine layer.
- Do not mix this mechanical implementation split with semantic refactors.
