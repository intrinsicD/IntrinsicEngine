---
id: BUG-027
theme: G
depends_on: [RORG-031F]
---
# BUG-027 — Sandbox drag/drop, close, and mesh primitive-view regression

## Goal
- Restore the promoted sandbox end-to-end UI path so OS dropped OBJ/OFF mesh files materialize through runtime import, become the active selection for editor controls, the window close event exits the engine loop, and mesh edge/vertex primitive-view toggles are reachable for the imported mesh.

## Non-goals
- No native file dialogs; path entry and dropped-path platform events remain the supported boundary.
- No new graphics primitive-view renderer path; reuse `Runtime.MeshPrimitiveViewPacker` and `RenderExtractionCache` settings.
- No persistent legacy overlay-entity factory revival.
- No broad file-format sweep beyond the reported OBJ/OFF mesh drop path.

## Context
- Symptom: dragging mesh files such as OBJ/OFF into the sandbox does not produce the expected visible/editor-controllable mesh workflow, clicking the window close button appears to do nothing, and users cannot visualize mesh vertices/edges after import.
- Expected behavior: platform `WindowDropEvent` reaches `Engine`, geometry decode/materialization runs through runtime-owned import state, the imported mesh is selected so the promoted mesh primitive-view controls can enable edge/vertex renderables, and `WindowCloseEvent` stops the engine loop.
- Impact: the current promoted EditorUI has the underlying render/extraction seams, but the end-to-end sandbox workflow is broken at runtime wiring and lacks regression coverage for platform-event OBJ/OFF drops.
- Completed: 2026-06-11 at `CPUContracted`. The fix wires `WindowCloseEvent` to `Engine::RequestExit()`, records the entity handle produced by standalone geometry materialization, and selects the imported mesh after dropped/direct geometry import so the existing Sandbox Editor primitive-view controls operate on it. A second close-path diagnosis found the live X-button still failed because `Engine::RunFrame()` polled platform events and then continued into clock/simulation/renderer work before re-checking `ShouldClose()`. `RunFrame()` now delegates the platform phase to `Core::ExecutePlatformBeginFrameContract(...)`, requests exit, and returns before renderer work when the poll step observes a close request. A narrow `Engine::DispatchPlatformEventForTest(...)` contract seam replays platform events through the same runtime handler installed as the window listener, avoiding backend-specific test downcasts.
- PR/commit: pending local commit.

## Required changes
- [x] Add event-level regression coverage for platform-driven OBJ and OFF mesh drops through the runtime platform-event handler.
- [x] Wire `WindowCloseEvent` to `Engine::RequestExit()` / loop stop explicitly.
- [x] Wire `Engine::RunFrame()` through the promoted platform begin-frame contract so a close observed during `PollEvents()` aborts before ImGui/render work.
- [x] Select materialized standalone geometry imports so selected-entity mesh primitive-view UI can control the imported mesh.
- [x] Preserve the existing runtime-owned mesh primitive-view extraction path and avoid graphics-owned ECS mutation.

## Tests
- [x] Add/extend `contract;runtime` coverage for platform drop events importing OBJ and OFF meshes through `Engine::Run()`.
- [x] Add/extend `contract;runtime` coverage proving dropped mesh selection exposes mesh primitive-view settings and uploads edge/vertex views.
- [x] Add/extend `contract;runtime` coverage proving a close event stops `Engine::Run()`.
- [x] Add/extend frame-loop/layering coverage proving platform close after `PollEvents()` stops before the renderer contract.

## Docs
- [x] Update `tasks/backlog/bugs/index.md` and `tasks/SESSION-BRIEF.md` for the new bug task.
- [x] Regenerate `docs/api/generated/module_inventory.md` after the runtime module surface change; no inventory diff was produced.
- [x] Update runtime/app docs only if the public behavior description changes; no standalone docs change was required beyond this retired bug record.

## Acceptance criteria
- [x] Dropped OBJ and OFF files create mesh-domain entities through the platform event path.
- [x] The last dropped mesh is the selected entity and mesh primitive-view commands can enable edge and vertex views.
- [x] The close event exits the engine loop without relying on GLFW-specific polling side effects, including the live timing where the close is observed during `PollEvents()`.
- [x] Fix does not introduce layering violations.

## Verification
Commands actually run for retirement (2026-06-11):

```bash
cmake --build --preset ci --target IntrinsicRuntimeTests
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|RuntimeFrameLoopContract|NullPlatform' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
git diff --check
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'RuntimeEngineLayering\\.RunFrame(Delegates|Stops)|RuntimeFrameLoopContract\\.PlatformBeginFrameStops' --timeout 60
```

The focused CTest run passed 35/35 tests. The default CPU-supported CTest
gate passed 2959/2959 tests. The second-pass close-path regression initially
failed in `RuntimeEngineLayering.RunFrameDelegatesToPromotedContractsInDocumentedBroadPhaseOrder`
and `RuntimeEngineLayering.RunFrameStopsAfterPlatformCloseBeforeRendererContract`;
after the fix, the focused close-path CTest passed 4/4 tests.

## Forbidden changes
- Shipping a fix without a regression test when one is feasible.
- Importing graphics renderer internals into runtime/editor UI code.
- Recreating legacy overlay entities or storing live graphics/RHI handles in ECS.

## Maturity
- Reached: `CPUContracted`; Vulkan visual proof of primitive-view rendering stays with existing graphics/runtime operational smokes.
