---
id: RUNTIME-158
theme: F
depends_on: [RUNTIME-157]
maturity_target: Operational
completed: 2026-07-09
---
# RUNTIME-158 — Extract frame pacing diagnostics out of Engine

## Goal
- Move the wide runtime frame-pacing diagnostics record and ImGui/render-graph diagnostics mirroring out of `Runtime.Engine.cppm` / `Runtime.Engine.cpp` into a focused runtime diagnostics module while preserving `Engine::GetLastFramePacingDiagnostics()` as the public compatibility facade.

## Non-goals
- Changing the `RuntimeFramePacingDiagnostics` field names, JSON frame-pacing report schema, or `Engine::GetLastFramePacingDiagnostics()` API.
- Changing frame phase ordering, early-return behavior, timing source, or which phases contribute elapsed microsecond counters.
- Changing `ImGuiAdapterDiagnostics`, renderer `RenderGraphFrameStats`, ImGui overlay production, or render-graph execution.
- Fixing the open `BUG-064` ci-vulkan headless display issue.
- Completing the remaining selected-entity async job routing in `RUNTIME-138`.

## Context
- Owner: `runtime`; frame-pacing diagnostics are copied runtime observability data spanning the frame loop, ImGui adapter producer diagnostics, and renderer render-graph timing counters.
- `Runtime.Engine.cppm` currently exports the full `RuntimeFramePacingDiagnostics` struct and stores the last sample directly on `Engine`.
- `Runtime.Engine.cpp` currently contains the `RunFrame()` `publishPacingSample` lambda that manually copies every ImGui adapter and render-graph counter into the sample.
- `Runtime.Engine.FrameLoop.cppm` already receives a `RuntimeFramePacingDiagnostics*` for render-contract phase timings, so the type should stay public but not be declared by the Engine interface module itself.
- This follows the RUNTIME-146 through RUNTIME-157 decomposition pattern: `Engine` keeps frame phase timing writes and the public accessor, while diagnostics-specific type definitions and counter mirroring move behind a runtime-owned module.

## Required changes
- [x] Add `Extrinsic.Runtime.FramePacingDiagnostics` exporting `RuntimeFramePacingDiagnostics` and pure mirroring helpers for `ImGuiAdapterDiagnostics` and `Graphics::RenderGraphFrameStats`.
- [x] Update `Runtime.Engine.cppm` and `Runtime.Engine.FrameLoop.cppm` to import the new diagnostics module instead of declaring the diagnostics record in the Engine interface.
- [x] Update `Runtime.Engine.cpp` so `RunFrame()` delegates ImGui and render-graph counter mirroring to the new module.
- [x] Add the new module to `src/runtime/CMakeLists.txt`.

## Tests
- [x] Add or update runtime source-contract coverage proving `RuntimeFramePacingDiagnostics` and ImGui/render-graph counter-copy policy no longer live in `Runtime.Engine.cppm` / `Runtime.Engine.cpp`.
- [x] Preserve `ImGuiAdapterEngineWiring.FramePacingDiagnosticsPopulateOnNullBackend` and sandbox frame-pacing acceptance coverage.
- [x] Run the default CPU-supported correctness gate before retirement.

## Docs
- [x] Update `src/runtime/README.md` to document `Extrinsic.Runtime.FramePacingDiagnostics` and revise the `Engine`/frame-pacing current-state wording.
- [x] Update `tasks/backlog/runtime/README.md` with the factual decomposition state.
- [x] Update `tasks/backlog/README.md` if the Theme F Engine-decomposition summary changes.
- [x] Regenerate `docs/api/generated/module_inventory.md`.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening and after retirement.

## Acceptance criteria
- [x] `Runtime.Engine.cppm` no longer declares `RuntimeFramePacingDiagnostics` fields directly; it imports the diagnostics module and stores only the last sample value.
- [x] `Runtime.Engine.cpp` no longer manually assigns each ImGui adapter/render-graph diagnostics field; it calls `Extrinsic.Runtime.FramePacingDiagnostics` helpers.
- [x] Existing frame-pacing behavior remains unchanged: early-return samples still publish, ImGui adapter counters mirror exactly, render-graph compile/execute timing mirrors exactly, and the final total elapsed microseconds still covers the frame-so-far.
- [x] Strict task, docs, layering, and test-layout checks pass, aside from pre-existing warning-mode root/task-state findings if unchanged by this slice.

## Verification
```bash
python3 tools/agents/generate_session_brief.py
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'FramePacingDiagnostics|ImGuiAdapterEngineWiring|RuntimeSandboxAcceptance|RuntimeEngineLayering' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
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
- Changing frame-loop phase order, command/event/job drain timing, render extraction order, or maintenance timing.
- Changing `intrinsic.frame_pacing.v1` report field names or semantics.
- Moving ImGui adapter ownership, renderer ownership, or platform/window ownership out of `Engine`.
- Reverting unrelated dirty worktree changes.

## Maturity
- Target: `Operational`.
- This slice closes at `Operational` when the live `Engine::RunFrame()` path publishes the same diagnostics through the extracted runtime diagnostics module and focused frame-pacing/layering tests plus the default CPU gate pass.

## Status
- Retired on 2026-07-09 at maturity `Operational`.
- `Extrinsic.Runtime.FramePacingDiagnostics` now owns the exported frame-pacing diagnostics record and the pure ImGui adapter / render-graph counter mirroring helpers.
- `Runtime.Engine` keeps `Engine::GetLastFramePacingDiagnostics()` as the public compatibility facade and only composes phase timing writes plus helper delegation.
- Verification passed:
  - `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
  - `ctest --test-dir build/ci --output-on-failure -R 'FramePacingDiagnostics|ImGuiAdapterEngineWiring|RuntimeSandboxAcceptance|RuntimeEngineLayering' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120` (17/17)
  - `cmake --build --preset ci --target IntrinsicRuntimeIntegrationTests`
  - `ctest --test-dir build/ci --output-on-failure -R 'RuntimeEngineLayering' --timeout 120` (18/18)
  - `cmake --build --preset ci --target IntrinsicTests`
  - `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` (3646/3646)
  - strict task/docs/layering/test-layout validators and `git diff --check`
- Warning-mode checks remain unchanged: `check_task_state_links.py` still reports retired `ARCH-007`..`ARCH-013` links in `tasks/backlog/architecture/README.md`, and `check_root_hygiene.py` still reports root entries `ara/` and `imgui.ini`.
- PR/commit: pending.
