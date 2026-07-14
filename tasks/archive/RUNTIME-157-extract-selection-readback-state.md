---
id: RUNTIME-157
theme: F
depends_on: [RUNTIME-156]
maturity_target: Operational
completed: 2026-07-09
---
# RUNTIME-157 — Extract selection readback state out of Engine

## Goal
- Move selection pick readback correlation state, per-readback primitive refinement cache ownership, and the graphics-selection readback drain bridge out of `Runtime.Engine.cppm` / `Runtime.Engine.FrameLoop.cppm` into a focused runtime module while preserving `Engine` as the frame-loop composition caller and public compatibility facade.

## Non-goals
- Changing `SelectionController` request, coalescing, in-flight pick, hit/no-hit, or ECS tag semantics.
- Changing `PrimitiveSelectionRefinement`'s hint validation, depth cursor reconstruction, orthographic-radius policy, CPU fallback behavior, or public result types.
- Changing renderer `SelectionSystem` queue semantics, pick request encoding, `RenderFrameInput::Pick`, or readback timing.
- Changing Sandbox editor primitive-detail models or public `Engine::GetLastRefinedPrimitiveSelection*()` accessors.
- Changing scene-document save/load/new/close behavior beyond routing refined-primitive cache clearing through the new module.

## Context
- Owner: `runtime`; this is runtime-owned frame-readback state bridging graphics pick output, live ECS selection mutation, and editor-facing primitive detail cache.
- `Runtime.Engine.cppm` still declares `m_LastRefinedPrimitive`, `m_LastRefinedPrimitiveGeneration`, a private `InFlightPickContext`, and `m_InFlightPickContexts`.
- `Runtime.Engine.FrameLoop.cppm` still owns `BuildPickReadbackContextForFrame(...)`, `RememberPickReadbackContextForFrame(...)`, `DrainPendingSelectionPickForFrame(...)`, `ApplySelectionReadbackToController(...)`, `RefineSelectionReadbackForFrame(...)`, and `DrainCompletedSelectionReadbacksForFrame(...)`.
- `Runtime.SceneDocument` reaches into Engine-owned raw optional/generation pointers to clear the editor-facing primitive cache during scene replacement.
- This follows the RUNTIME-146 through RUNTIME-156 decomposition pattern: `Engine` keeps frame phase ordering and public facade compatibility, while subsystem-local state and policy move behind runtime-owned modules.

## Required changes
- [x] Add `Extrinsic.Runtime.SelectionReadback` with the private in-flight pick context records, bounded context retention, pending-pick drain, completed-readback drain, selection-controller application, primitive-refinement cache update, and refined-cache clearing.
- [x] Update `Runtime.Engine.cppm` to import and store the new selection readback state object instead of the refined primitive optional/generation and in-flight context vector.
- [x] Update `Runtime.Engine.cpp` / `Runtime.Engine.FrameLoop.cppm` so `RunFrame()` delegates pending pick drain and completed readback drain to the new module.
- [x] Update `Runtime.SceneDocument` dependencies so scene replacement clears the refined primitive cache through the new module instead of raw optional/generation pointers.
- [x] Add the new module to `src/runtime/CMakeLists.txt`.

## Tests
- [x] Add or update runtime source-contract coverage proving in-flight pick context records, pending-pick drain, completed-readback drain, and refined primitive cache mutation no longer live in `Runtime.Engine.cppm` / `Runtime.Engine.FrameLoop.cppm`.
- [x] Preserve existing primitive-selection refinement, selection correlation, scene-lifecycle, and sandbox acceptance coverage.
- [x] Run the default CPU-supported correctness gate before retirement.

## Docs
- [x] Update `src/runtime/README.md` to document `Extrinsic.Runtime.SelectionReadback` and revise the `PrimitiveSelectionRefinement`, `SelectionController`, `SceneDocument`, and frame-maintenance current-state wording.
- [x] Update `tasks/backlog/runtime/README.md` with the factual decomposition state.
- [x] Update `tasks/backlog/README.md` if the Theme F Engine-decomposition summary changes.
- [x] Regenerate `docs/api/generated/module_inventory.md`.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening and after retirement.

## Acceptance criteria
- [x] `Runtime.Engine.cppm` contains no `InFlightPickContext`, `m_InFlightPickContexts`, `m_LastRefinedPrimitive`, or `m_LastRefinedPrimitiveGeneration` members.
- [x] `Runtime.Engine.FrameLoop.cppm` no longer owns the pick readback context builder, in-flight context retention, selection readback drain loop, selection-controller readback application, or refined-primitive cache mutation.
- [x] `Runtime.Engine.cpp` delegates pending-pick drain, completed-readback drain, refined primitive cache accessors, and scene-document refined-cache clearing through `Extrinsic.Runtime.SelectionReadback`.
- [x] Existing readback behavior remains unchanged: a drained pick captures issuing-frame context, completed readbacks apply to `SelectionController` by sequence, newest refined primitive wins, background clears the cache, empty drains retain the cache, and scene replacement clears the exposed primitive cache.
- [x] Strict task, docs, layering, and test-layout checks pass, aside from pre-existing warning-mode root/task-state findings if unchanged by this slice.

## Verification
```bash
python3 tools/agents/generate_session_brief.py
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'SelectionReadback|PrimitiveSelectionRefinement|SelectionStableLookupComposition|SelectionReadbackCorrelation|RuntimeSceneLifecycle|RuntimeEngineLayering' --timeout 90
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_state_links.py --root .
python3 tools/repo/check_root_hygiene.py --root .
python3 tools/repo/check_pr_contract.py
git diff --check
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Moving `SelectionController` ownership out of `Engine`.
- Changing `SelectionController` public API, renderer `SelectionSystem` API, or `PrimitiveSelectionRefinement` result semantics.
- Changing frame phase ordering, render extraction order, or maintenance/readback timing.
- Reverting unrelated dirty worktree changes.

## Maturity
- Target: `Operational`.
- This slice closes at `Operational` when the live `Engine::RunFrame()` path delegates selection readback state/drain/cache ownership to the new runtime module and focused selection/readback/layering tests plus the default CPU gate pass.

## Status
- Retired on 2026-07-09 at maturity `Operational`.
- `Extrinsic.Runtime.SelectionReadback` now owns in-flight pick readback context retention, pending pick drain, completed readback FIFO drain, `SelectionController` hit/no-hit application, context-aware primitive refinement, and the editor-facing refined primitive cache/generation.
- Verification passed:
  - `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
  - `ctest --test-dir build/ci --output-on-failure -R 'SelectionReadback|PrimitiveSelectionRefinement|SelectionStableLookupComposition|SelectionReadbackCorrelation|RuntimeSceneLifecycle|RuntimeEngineLayering' --timeout 90` (73/73)
  - `cmake --build --preset ci --target IntrinsicTests`
  - `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` (3646/3646)
  - strict task/docs/layering/test-layout validators and `git diff --check`
- Warning-mode checks remain unchanged: `check_task_state_links.py` still reports retired `ARCH-007`..`ARCH-013` links in `tasks/backlog/architecture/README.md`, and `check_root_hygiene.py` still reports root entries `ara/` and `imgui.ini`.
- PR/commit: pending.
