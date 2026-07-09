---
id: RUNTIME-159
theme: F
depends_on: [RUNTIME-158]
maturity_target: Operational
completed: 2026-07-09
---
# RUNTIME-159 — Extract ImGui editor bridge out of Engine

## Goal
- Move runtime-side Dear ImGui overlay/adapter/callback ownership, renderer overlay attachment, per-frame Begin/End bracketing, and capture/diagnostics access out of `Runtime.Engine.cppm` / `Runtime.Engine.cpp` into a focused runtime module while preserving `Engine::SetImGuiEditorCallback()` and `Engine::GetImGuiAdapter()` as public compatibility facades.

## Non-goals
- Changing `Extrinsic.Runtime.ImGuiAdapter`, `Extrinsic.Graphics.ImGuiOverlaySystem`, renderer `SetImGuiOverlaySystem(...)`, ImGui draw-data translation, font atlas retention, or overlay pass behavior.
- Changing frame phase ordering, minimized-frame early-return behavior, `OnVariableTick()` placement, or frame-pacing field semantics.
- Changing Sandbox editor panel content, window registration, capture policy, or `UI-034` / `ARCH-006` ownership.
- Completing selected-entity async cache routing from `RUNTIME-138`.

## Context
- Owner: `runtime`; this is runtime-owned editor/overlay composition that bridges the platform window, runtime ImGui adapter, graphics overlay system, and renderer consumer.
- `Runtime.Engine.cppm` currently imports `Extrinsic.Graphics.ImGuiOverlaySystem` and stores `m_ImGuiOverlay`, `m_ImGuiEditorCallback`, and `m_ImGuiAdapter` directly.
- `Runtime.Engine.cpp` currently constructs and initializes the adapter, re-applies the editor callback, attaches/detaches the overlay system on the renderer, calls `BeginFrame()` / `EndFrame()` directly, reads capture booleans directly, and mirrors diagnostics from the adapter.
- This follows the RUNTIME-146 through RUNTIME-158 decomposition pattern: `Engine` keeps frame phase ordering and public facade compatibility, while subsystem-local state and policy move behind runtime-owned modules.

## Required changes
- [x] Add `Extrinsic.Runtime.ImGuiEditorBridge` owning `Graphics::ImGuiOverlaySystem`, the editor callback, and the runtime `ImGuiAdapter`.
- [x] Move adapter initialize/shutdown, renderer overlay attach/detach, editor callback storage/application, BeginFrame/EndFrame calls, capture-state reads, and adapter diagnostics access behind the bridge.
- [x] Update `Runtime.Engine.cppm` to store the bridge instead of raw overlay/callback/adapter fields and to avoid importing `Extrinsic.Graphics.ImGuiOverlaySystem` directly.
- [x] Update `Runtime.Engine.cpp` so `Initialize()`, `Shutdown()`, `RunFrame()`, frame-pacing mirroring, `SetImGuiEditorCallback()`, and `GetImGuiAdapter()` delegate through the bridge.
- [x] Add the new module to `src/runtime/CMakeLists.txt`.

## Tests
- [x] Add or update runtime source-contract coverage proving overlay/callback/adapter ownership and direct `BeginFrame()` / `EndFrame()` / capture reads no longer live in `Runtime.Engine.cppm` / `Runtime.Engine.cpp`.
- [x] Preserve `ImGuiAdapterEngineWiring`, sandbox editor attachment, ImGui surface smoke, and frame-pacing diagnostics coverage.
- [x] Run the default CPU-supported correctness gate before retirement.

## Docs
- [x] Update `src/runtime/README.md` to document `Extrinsic.Runtime.ImGuiEditorBridge` and revise the `Engine`/ImGui current-state wording.
- [x] Update `tasks/backlog/runtime/README.md` with the factual decomposition state.
- [x] Update `tasks/backlog/README.md` if the Theme F Engine-decomposition summary changes.
- [x] Regenerate `docs/api/generated/module_inventory.md`.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening and after retirement.

## Acceptance criteria
- [x] `Runtime.Engine.cppm` no longer imports `Extrinsic.Graphics.ImGuiOverlaySystem` or declares `m_ImGuiOverlay`, `m_ImGuiEditorCallback`, or `m_ImGuiAdapter` fields.
- [x] `Runtime.Engine.cpp` no longer constructs `ImGuiAdapter`, calls adapter `BeginFrame()` / `EndFrame()` directly, reads adapter capture booleans directly, or attaches/detaches `ImGuiOverlaySystem` directly.
- [x] Existing behavior remains unchanged: pre-initialize callbacks are applied to the constructed adapter, one ImGui overlay frame is produced per non-minimized engine frame, capture booleans still gate viewport input, frame-pacing diagnostics still mirror adapter counters, and renderer overlay attachment is cleared before adapter/overlay teardown.
- [x] Strict task, docs, layering, and test-layout checks pass, aside from pre-existing warning-mode root/task-state findings if unchanged by this slice.

## Verification
```bash
python3 tools/agents/generate_session_brief.py
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'ImGuiEditorBridge|ImGuiAdapterEngineWiring|SandboxEditorUi|RuntimeEngineLayering|FramePacingDiagnostics' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_state_links.py --root .
python3 tools/repo/check_root_hygiene.py --root .
git diff --check
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Changing ImGui adapter behavior, overlay frame payloads, renderer overlay pass behavior, or font atlas retention policy.
- Changing frame phase ordering, command/event/job drain timing, render extraction order, or maintenance timing.
- Moving Sandbox panel content or selected-entity cache/job behavior.
- Reverting unrelated dirty worktree changes.

## Maturity
- Target: `Operational`.
- This slice closes at `Operational` when the live `Engine::RunFrame()` path delegates ImGui editor bridge ownership and frame bracketing to the new runtime module and focused ImGui/layering/frame-pacing tests plus the default CPU gate pass.

## Status
- Retired on 2026-07-09 at maturity `Operational`.
- `Extrinsic.Runtime.ImGuiEditorBridge` now owns the runtime `ImGuiAdapter`, shared `Graphics::ImGuiOverlaySystem`, stored editor callback, renderer overlay attach/detach, adapter initialize/shutdown, per-frame `BeginFrame()` / `EndFrame()` delegation, capture-state reads, and diagnostics access.
- `Runtime.Engine` keeps frame phase ordering plus `SetImGuiEditorCallback(...)` / `GetImGuiAdapter()` compatibility facades; raw ImGui overlay/callback/adapter ownership no longer lives in `Runtime.Engine.cppm` / `.cpp`.
- Verification passed:
  - `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
  - `ctest --test-dir build/ci --output-on-failure -R 'ImGuiEditorBridge|ImGuiAdapterEngineWiring|SandboxEditorUi|RuntimeEngineLayering|FramePacingDiagnostics' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180` (146/146)
  - `cmake --build --preset ci --target IntrinsicTests`
  - `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` (3646/3646)
  - strict task/docs/layering/test-layout validators
- Warning-mode checks remain unchanged: `check_task_state_links.py` still reports retired `ARCH-007`..`ARCH-013` links in `tasks/backlog/architecture/README.md`, and `check_root_hygiene.py` still reports root entries `ara/` and `imgui.ini`.
- PR/commit: pending.
