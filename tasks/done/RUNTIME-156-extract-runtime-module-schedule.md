---
id: RUNTIME-156
theme: F
depends_on: [RUNTIME-155]
maturity_target: Operational
completed: 2026-07-09
---
# RUNTIME-156 — Extract runtime-module schedule out of Engine

## Goal
- Move runtime-module sim-system/frame-hook registration records, deterministic ordering, and per-frame dispatch out of `Runtime.Engine.cppm` / `Runtime.Engine.cpp` into a focused runtime module while preserving `Engine` as the module lifecycle and service-registration composition root.

## Non-goals
- Moving `Engine::AddModule(...)`, module ownership, duplicate-name rejection, `IRuntimeModule::OnRegister(...)`, `IRuntimeModule::OnResolve(...)`, or shutdown ordering.
- Changing built-in service registration, `ServiceRegistry` phases, command/event/job/world ownership, or module boot error handling.
- Changing frame phase order, fixed-step registration timing, `FrameGraph` pass names, sim-system dependency semantics, or frame-hook sort order.
- Adding new runtime-module phases, services, or module APIs.

## Context
- Owner: `runtime`; `Engine` remains the concrete composition root and lifecycle driver, but registered module contributions are schedule state rather than general engine state.
- Before this slice, `Runtime.Engine.cppm` declared private `RuntimeModuleSimSystemRecord` and `RuntimeModuleFrameHookRecord` structs, stored their vectors and registration sequence counter, and exposed private helpers for registration, topological sort, fixed-step pass insertion, and frame-hook dispatch.
- Before this slice, `Runtime.Engine.cpp` contained the sim-system dependency graph/topological sort, frame-hook ordering, context construction, and per-frame dispatch loops.
- This follows the RUNTIME-146 through RUNTIME-155 decomposition pattern: keep the stable public module API and lifecycle facade on `Engine`, move subsystem-local state/policy behind a runtime-owned module.

## Required changes
- [x] Add `Extrinsic.Runtime.ModuleSchedule` with the private contribution records, registration sequence, deterministic sim-system dependency ordering, frame-hook ordering, fixed-step pass insertion, and frame-hook dispatch.
- [x] Update `Runtime.Engine.cppm` to import the new module and store one schedule object instead of private contribution records/vectors/counter.
- [x] Update `Runtime.Engine.cpp` so module registration/resolution callbacks and `RunFrame()` delegate contribution registration, schedule finalization, sim-system pass insertion, and frame-hook dispatch to the new module.
- [x] Keep runtime-module public API compatibility for modules that register sim systems and frame hooks through `EngineSetup`.
- [x] Add the new module to `src/runtime/CMakeLists.txt`.

## Tests
- [x] Add or update runtime source-contract coverage proving runtime-module contribution records, dependency graph sorting, and frame-hook dispatch no longer live in `Runtime.Engine.cppm` / `Runtime.Engine.cpp`.
- [x] Preserve `Test.RuntimeModule` behavioral coverage for service registration, sim-system dependency ordering, frame-hook ordering, and shutdown announcement.
- [x] Run the default CPU-supported correctness gate before retirement.

## Docs
- [x] Update `src/runtime/README.md` to document `Extrinsic.Runtime.ModuleSchedule` and revise the Engine description.
- [x] Update `tasks/backlog/runtime/README.md` with the factual decomposition state.
- [x] Regenerate `docs/api/generated/module_inventory.md`.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening and after retirement.

## Acceptance criteria
- [x] `Runtime.Engine.cppm` contains no `RuntimeModuleSimSystemRecord`, `RuntimeModuleFrameHookRecord`, runtime-module contribution vectors, or registration sequence counter.
- [x] `Runtime.Engine.cpp` contains no sim-system dependency graph/topological sort, frame-hook record sort, or direct runtime-module frame-hook dispatch loop.
- [x] `Engine` still owns module objects, built-in service provisioning, `OnRegister` / `OnResolve` sequencing, and shutdown calls.
- [x] Existing runtime-module behavior remains unchanged under focused tests and the default CPU gate.
- [x] Strict task, docs, layering, and test-layout checks pass, aside from pre-existing warning-mode root/task-state findings if unchanged by this slice.

## Status
- Completed on 2026-07-09 at maturity `Operational`.
- `Extrinsic.Runtime.ModuleSchedule` now owns runtime-module sim-system/frame-hook records, deterministic dependency ordering, frame-hook ordering, fixed-step pass insertion/context construction, and frame-hook dispatch.
- `Runtime.Engine` still owns module objects, built-in service provisioning, `OnRegister` / `OnResolve` sequencing, and shutdown calls while delegating schedule contribution storage/finalization/dispatch.
- `Runtime.Engine.cppm` no longer declares the contribution records/vectors/sequence counter; `Runtime.Engine.cpp` no longer contains the dependency graph/toposort, record pass insertion loop, or hook dispatch loop.
- Focused runtime-module/layering coverage passed, `IntrinsicTests` built, and the default CPU-supported gate passed 3644/3644.
- Root hygiene still reports pre-existing warning-mode entries `ara/` and `imgui.ini`; unchanged by this slice.
- PR/commit: pending.

## Verification
```bash
python3 tools/agents/generate_session_brief.py
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeModule|RuntimeEngineLayering' --timeout 90
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_root_hygiene.py --root .
python3 tools/repo/check_pr_contract.py
git diff --check
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Moving `IRuntimeModule` or changing `EngineSetup` public API.
- Changing module registration/resolution/shutdown lifecycle order.
- Changing fixed-step, variable-tick, UI-build, render-extraction, maintenance, or shutdown frame ordering.
- Reverting unrelated dirty worktree changes.

## Maturity
- Target: `Operational`.
- This slice closes at `Operational` when the Engine boot/frame path delegates runtime-module schedule ownership and dispatch to the new module and focused runtime-module/layering tests plus the default CPU gate pass.
