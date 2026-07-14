---
id: RUNTIME-155
theme: F
depends_on: [RUNTIME-154]
maturity_target: Operational
completed: 2026-07-09
---
# RUNTIME-155 — Extract runtime input-action registry out of Engine

## Goal
- Move runtime input-action descriptor types, registration state, trigger checks, and per-frame dispatch out of `Runtime.Engine.cppm` / `Runtime.Engine.cpp` into a focused runtime module while preserving the existing `Engine` register/unregister facade.

## Non-goals
- Adding new input triggers, shortcuts, editor commands, or default sandbox policies.
- Changing the frame order of input-action dispatch relative to camera resolution, pre-render transform flush, selection submission, ImGui keyboard capture, or render extraction.
- Removing `Engine::RegisterInputAction(...)` or `Engine::UnregisterInputAction(...)`.
- Moving platform event handling, drop-file import handling, selection picking, or gizmo driving.

## Context
- Owner: `runtime`; `Engine` remains the concrete frame skeleton and composition root, but input-action storage and key-trigger dispatch are runtime command policy rather than general engine lifecycle code.
- `Runtime.Engine.cppm` currently exports the input-action binding/handle/context/service/descriptor types, declares the private record type, and stores the action vector plus next-handle counter on `Engine`.
- `Runtime.Engine.FrameLoop.cppm` currently contains the trigger predicate and dispatch loop, while `Runtime.Engine.cpp` owns registration/unregistration mutation.
- `RUNTIME-144` introduced this surface for sandbox default focus behavior; the behavior is already covered through `Test.RuntimeInputActions.cpp`.
- This follows the RUNTIME-146 through RUNTIME-154 decomposition pattern: keep stable public facade methods on `Engine`, move subsystem-local policy and state behind a runtime-owned module.

## Required changes
- [x] Add `Extrinsic.Runtime.InputActions` with the public input-action descriptor/service/context/handle types plus an owning registry/control class.
- [x] Move action registration, unregistration, trigger evaluation, failure logging, and per-frame dispatch into the new module.
- [x] Update `Runtime.Engine.cppm` to import the new module and store the registry/control object instead of `RuntimeInputActionRecord` vectors and a next-handle counter.
- [x] Update `Runtime.Engine.cpp` so `RegisterInputAction(...)`, `UnregisterInputAction(...)`, and `RunFrame()` delegate to the new module.
- [x] Keep `Engine` public API compatibility for sandbox default policies and tests that register input actions through `Engine`.
- [x] Add the new module to `src/runtime/CMakeLists.txt`.

## Tests
- [x] Update the runtime layering/source-contract test to prove input-action records, handle allocation, trigger evaluation, and dispatch loops no longer live in `Runtime.Engine.cppm`, `Runtime.Engine.cpp`, or the private frame-loop partition.
- [x] Preserve `RuntimeInputActions.DefaultFocusKeyDispatchesRegisteredAction`.
- [x] Preserve `RuntimeInputActions.NoDefaultInputActionsLeaveFocusKeyNoOp`.
- [x] Run the default CPU-supported correctness gate before retirement.

## Docs
- [x] Update `src/runtime/README.md` to document `Extrinsic.Runtime.InputActions` and revise the Engine description.
- [x] Update `tasks/backlog/runtime/README.md` with the factual decomposition state.
- [x] Regenerate `docs/api/generated/module_inventory.md`.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening and after retirement.

## Acceptance criteria
- [x] `Runtime.Engine.cppm` exports/imports input-action API through `Extrinsic.Runtime.InputActions` and has no private `RuntimeInputActionRecord`, action vector, or next-handle counter.
- [x] `Runtime.Engine.cpp` does not allocate input-action handles, erase action records, evaluate input-action triggers, or run the dispatch loop directly.
- [x] `Runtime.Engine.FrameLoop.cppm` no longer contains `RuntimeInputActionTriggered(...)` or `DispatchRuntimeInputActionsForFrame(...)`.
- [x] Sandbox default `F` focus behavior and no-default no-op behavior remain unchanged.
- [x] Strict task, docs, layering, and test-layout checks pass, aside from pre-existing warning-mode root/task-state findings if unchanged by this slice.

## Status
- Completed on 2026-07-09 at maturity `Operational`.
- `Extrinsic.Runtime.InputActions` now owns input-action descriptor/service/context/handle types, registration state, handle allocation, key-edge trigger checks, ImGui keyboard-capture suppression, callback failure logging, and per-frame dispatch.
- `Runtime.Engine` re-exports the input-action API for compatibility, stores a `RuntimeInputActionRegistry`, and delegates `RegisterInputAction(...)`, `UnregisterInputAction(...)`, and frame dispatch to that registry.
- `Runtime.Engine.FrameLoop.cppm` no longer contains the input-action trigger predicate or dispatch loop.
- First contract build caught missing implementation-unit includes for `std::erase_if` and `std::uint64_t`; adding `<vector>` and `<cstdint>` to `Runtime.InputActions.cpp` fixed the error.
- Root hygiene still reports the pre-existing warning-mode entries `ara/` and `imgui.ini`; no task-state or root-hygiene cleanup was part of this slice.
- PR/commit: pending.

## Verification
```bash
python3 tools/agents/generate_session_brief.py
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeInputActions|RuntimeEngineLayering' --timeout 90
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
- Adding, removing, or rebinding default sandbox input actions.
- Changing `F` focus dispatch timing, ImGui keyboard-capture suppression, or same-frame camera refresh behavior.
- Moving platform event/drop-file handling, selection readback, or gizmo logic in this slice.
- Reverting unrelated dirty worktree changes.

## Maturity
- Target: `Operational`.
- This slice closes at `Operational` when the Engine frame path delegates input-action registration and dispatch to the new module and focused input-action/layering tests plus the default CPU gate pass.
