# RUNTIME-090 — `Extrinsic.Runtime.ImGuiAdapter` (Dear ImGui platform/renderer adapter)

## Goal
- Open the runtime-side Dear ImGui adapter declared by `GRAPHICS-013CQ`: a new module `Extrinsic.Runtime.ImGuiAdapter` (planned home: `src/runtime/ImGui/Runtime.ImGuiAdapter.cppm`) that owns the ImGui context lifecycle, drives the platform input pump (window events / mouse / keyboard), walks `ImDrawData` after `ImGui::Render()`, and constructs `ImGuiOverlayFrame` records submitted via `ImGuiOverlaySystem::SubmitFrame(...)` once per frame after `ImGui::Render()` and before `IRenderer::PrepareFrame()`.

## Non-goals
- No graphics-side `imgui.h` import.
- No editor panels content (the adapter only plumbs ImGui itself).
- No mutation of `ImGuiOverlayFrame` field set or `ImGuiOverlaySystem` semantics.
- No frame buffer copy/blit / present fast path.

## Context
- Status: in-progress (Slice B). Slice A landed (PR #962, commits `3bd20f2` +
  `4676a1d`); this slice wires the adapter into `Engine::RunFrame` and exposes
  the editor hook.
- Owner/agent + branch: `claude/intrinsicengine-agent-onboarding-01gFi` (Slice B;
  Slice A was `claude/intrinsicengine-agent-onboarding-qu8wV`).
- Next verification step: build `IntrinsicTests` and run the `ImGuiAdapter*`
  `contract;runtime` cases (including the new engine-wiring cases) under the
  default CPU gate.
- Owner/layer: `runtime`.
- Overlay-system ownership decision (Slice B): the live
  `Graphics::ImGuiOverlaySystem` instance is owned by `Engine` (runtime owns
  composition; `runtime -> graphics` is an allowed edge) and handed to the
  adapter by reference. This keeps RUNTIME-090 entirely runtime-scoped (no
  graphics interface change) and does not preempt `GRAPHICS-079`, which owns the
  renderer-side `Pass.ImGui` consumption + GPU font-atlas allocation. Nonblocking
  handoff note for `GRAPHICS-079`: when `Pass.ImGui` is wired, the runtime
  composition will hand this same engine-owned overlay instance to the renderer
  (producer and consumer share one instance), rather than the renderer
  constructing its own.
- Planning anchor: `tasks/done/GRAPHICS-013CQ-imgui-present-backend-clarifications.md` ("the runtime-side Dear ImGui platform/renderer adapter, **not** graphics, walks the `ImDrawData` produced by `ImGui::Render()` and constructs `ImGuiOverlayFrame` records, then calls `ImGuiOverlaySystem::SubmitFrame(...)`").
- Today: `imgui_lib` is already linked into `ExtrinsicPlatform` for the GLFW backend; `ImGuiOverlaySystem` accepts overlay records but no producer exists; `Pass.ImGui` is unwired (`GRAPHICS-079`).
- DPI/font rebuilds re-run `ImGuiOverlaySystem::Shutdown()`/`Initialize()` cycle.
- ImGui version in tree is `1.92.5` (`external/cache/imgui-src`, see
  `cmake/Dependencies.cmake`). 1.92 ships the dynamic texture system
  (`ImTextureID == ImU64`, `ImTextureRef`); the adapter sets
  `ImGuiBackendFlags_RendererHasTextures` so ImGui owns the font atlas lifecycle
  internally and `NewFrame()`/`Render()` run headless without a pre-built atlas
  or `SetTexID()`. The only `NewFrame` sanity asserts are `DeltaTime > 0` after
  frame 0 and `DisplaySize >= 0`. The graphics-owned retained font atlas upload
  is `GRAPHICS-079`, not this task.
- Design decision: the adapter translates `Platform::Event` variants into ImGui
  IO directly rather than linking `imgui_impl_glfw`, so it stays
  backend-agnostic and works with the `Null` window. `imgui_lib` is linked
  **PRIVATE** to `ExtrinsicRuntime` so `imgui.h` and the transitive `glfw`/`volk`
  link deps stay out of the runtime's public module surface; the `.cppm`
  interface exposes no ImGui types.

## Required changes
### Slice A (landed) — standalone adapter module + contract tests (`Scaffolded`)
- [x] Add `src/runtime/ImGui/Runtime.ImGuiAdapter.cppm` exporting `Extrinsic.Runtime.ImGuiAdapter` with (no `imgui.h` in the interface):
  - `class ImGuiAdapter` constructed with `(Platform::IWindow&, ImGuiOverlaySystem&)`,
  - `Initialize()` creates the ImGui context and configures the platform IO (display size, framebuffer scale, clipboard, mouse cursor backend flags),
  - `BeginFrame(double deltaSeconds)` pumps platform events into ImGui's IO and calls `ImGui::NewFrame()`,
  - `EndFrame()` invokes the editor hook, calls `ImGui::Render()`, walks `ImDrawData`, builds an `ImGuiOverlayFrame`, calls `overlaySystem.SubmitFrame(frame)`,
  - `Shutdown()` destroys the context,
  - `RebuildForDisplayChange()` performs exactly one `Shutdown()`+`Initialize()` cycle on the overlay system and the ImGui context (DPI/font rebuild),
  - `SetEditorCallback(std::function<void()>)` editor hook called between `BeginFrame` and `EndFrame`,
  - `GetDiagnostics()` returning an `ImGuiAdapterDiagnostics` value type for testable observables.
- [x] Add `src/runtime/ImGui/Runtime.ImGuiAdapter.cpp` implementation unit (owns all `imgui.h` usage); register both files in `src/runtime/CMakeLists.txt` and link `imgui_lib` **PRIVATE** to `ExtrinsicRuntime`.
- [x] Mouse position/button/wheel, character input, and window-resize events are translated into ImGui IO; every drained event is counted in diagnostics. Key-code translation (GLFW→`ImGuiKey`) is deferred to the editor input-binding slice and recorded as a non-goal of Slice A.

### Slice B (this slice) — Engine wiring (`CPUContracted`)
- [x] Wire `Engine` to construct `ImGuiAdapter` after `Window` and `Renderer` exist; call `BeginFrame()` after `Window::PollEvents` (and the minimize/resize early returns, to keep the `NewFrame()`/`Render()` pairing intact) and before `OnVariableTick`; call `EndFrame()` after `OnVariableTick` and before `IRenderer::PrepareFrame()`. `Engine` owns the `Graphics::ImGuiOverlaySystem` instance and tears the adapter down first in `Shutdown()`.
- [x] Expose the editor hook from `Engine` (`SetImGuiEditorCallback`) plus a const `GetImGuiAdapter()` observer so future editor code can render panels and tests can read the produce-path diagnostics without modifying the adapter.

## Tests
### Slice A (landed)
- [x] `contract;runtime` test: with a `FakeWindow : Platform::IWindow` test double (deterministic extent/events), `ImGuiAdapter::Initialize()` succeeds and `BeginFrame()`/`EndFrame()` produce a zero-list `ImGuiOverlayFrame` (no input, no panels); diagnostics report one produced frame.
- [x] `contract;runtime` test: an editor hook that issues one ImGui window's draw (`imgui.h` included directly in the test) makes `EndFrame()` produce an overlay frame whose `DrawLists` are non-empty with matching vertex/index/command counts and `UsesUserTexture == false`.
- [x] `contract;runtime` test: the editor hook is invoked exactly once per `BeginFrame`/`EndFrame` pair.
- [x] `contract;runtime` test: synthesized cursor/button/scroll/char events are forwarded to ImGui IO (`PumpedEventCount` matches) and a window-resize event updates the reported display size.
- [x] `contract;runtime` test: `RebuildForDisplayChange()` runs the `ImGuiOverlaySystem::Shutdown()/Initialize()` cycle exactly once (`ContextRebuilds == 1`).
- [x] No `gpu`/`vulkan` test in this slice.

### Slice B (this slice) — `Test.ImGuiAdapterEngineWiring.cpp`
- [x] `contract;runtime` test: a headless `Engine` (Null device/window) initialized then driven through a bounded `Engine::Run()` (stub app calls `RequestExit()` after N variable ticks) produces exactly one `ImGuiOverlayFrame` per engine frame (`GetImGuiAdapter().GetDiagnostics().FramesProduced == N`); the adapter is initialized after `Initialize()`.
- [x] `contract;runtime` test: an editor callback registered via `Engine::SetImGuiEditorCallback` is invoked once per engine frame (`EditorCallbackInvocations == N`) and, when it issues an ImGui panel draw (`imgui.h` included directly), the produced overlay frame reports non-empty draw lists.
- [x] No `gpu`/`vulkan` test in this slice (the GPU font-atlas upload + `Pass.ImGui` execution remain `GRAPHICS-079`).

## Docs
- [x] Update `src/runtime/README.md` to add the `Extrinsic.Runtime.ImGuiAdapter` module row (Slice A current state; Slice B refreshes it to record the `Engine` wiring + editor-hook exposure + overlay ownership).
- [x] Refresh `docs/api/generated/module_inventory.md` after adding the module (no surface change in Slice B; regenerated to confirm no diff).

## Acceptance criteria
- [x] One `ImGuiOverlayFrame` is produced per `BeginFrame`/`EndFrame` pair when the adapter is initialized (Slice A).
- [x] No graphics-side `imgui.h` import; `imgui.h` stays out of the `.cppm` interface and is `PRIVATE` to `ExtrinsicRuntime` (Slice A).
- [x] One `ImGuiOverlayFrame` is produced per engine frame when the adapter is wired into `Engine` (Slice B).
- [x] No regression in `ImGuiOverlaySystem` semantics or `Pass.ImGui` wiring (`GRAPHICS-079`).

## Slice plan
- **Slice A (this slice).** Standalone `Extrinsic.Runtime.ImGuiAdapter` module
  (context lifecycle, event→IO pump, `ImDrawData`→`ImGuiOverlayFrame` walk,
  editor hook, diagnostics) + `contract;runtime` tests driven by a
  `FakeWindow` test double. Preserves the CPU/null gate; no `Engine` wiring.
  Closes `Scaffolded`. Defers Engine frame-loop wiring + editor-hook exposure to
  Slice B.
- **Slice B.** Wire the adapter into `Engine::RunFrame` (BeginFrame after
  `PollEvents`, EndFrame before `PrepareFrame`) and expose the editor hook from
  `Engine`; add the per-engine-frame `contract;runtime` coverage. Closes
  `CPUContracted`.

## Maturity
- Target: `CPUContracted` (this task is CPU/null only; the GPU font-atlas upload
  and pass execution are owned by `GRAPHICS-079`).
- Slice A closes `Scaffolded → CPUContracted` for the standalone adapter module
  (its produce path is fully CPU-contract-tested); Slice B extends
  `CPUContracted` to the wired-into-`Engine` frame path.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ImGuiAdapter' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Importing `imgui.h` from `src/graphics/*`.
- Exposing ImGui types (`imgui.h`) through the `.cppm` module interface.
- Mutating `ImGuiOverlayFrame` field set or pass semantics.
- Adding editor-panel content here.

## Next verification step
- Slice A: land the standalone adapter module, exercise the `ImGuiAdapter*`
  `contract;runtime` cases. Slice B: Engine frame-loop wiring.
