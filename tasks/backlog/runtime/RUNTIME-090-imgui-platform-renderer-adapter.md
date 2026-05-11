# RUNTIME-090 — `Extrinsic.Runtime.ImGuiAdapter` (Dear ImGui platform/renderer adapter)

## Goal
- Open the runtime-side Dear ImGui adapter declared by `GRAPHICS-013CQ`: a new module `Extrinsic.Runtime.ImGuiAdapter` (planned home: `src/runtime/ImGui/Runtime.ImGuiAdapter.cppm`) that owns the ImGui context lifecycle, drives the platform input pump (window events / mouse / keyboard), walks `ImDrawData` after `ImGui::Render()`, and constructs `ImGuiOverlayFrame` records submitted via `ImGuiOverlaySystem::SubmitFrame(...)` once per frame after `ImGui::Render()` and before `IRenderer::PrepareFrame()`.

## Non-goals
- No graphics-side `imgui.h` import.
- No editor panels content (the adapter only plumbs ImGui itself).
- No mutation of `ImGuiOverlayFrame` field set or `ImGuiOverlaySystem` semantics.
- No frame buffer copy/blit / present fast path.

## Context
- Status: not started.
- Owner/layer: `runtime`.
- Planning anchor: `tasks/done/GRAPHICS-013CQ-imgui-present-backend-clarifications.md` ("the runtime-side Dear ImGui platform/renderer adapter, **not** graphics, walks the `ImDrawData` produced by `ImGui::Render()` and constructs `ImGuiOverlayFrame` records, then calls `ImGuiOverlaySystem::SubmitFrame(...)`").
- Today: `imgui_lib` is already linked into `ExtrinsicPlatform` for the GLFW backend; `ImGuiOverlaySystem` accepts overlay records but no producer exists; `Pass.ImGui` is unwired (`GRAPHICS-079`).
- DPI/font rebuilds re-run `ImGuiOverlaySystem::Shutdown()`/`Initialize()` cycle.

## Required changes
- [ ] Add `src/runtime/ImGui/Runtime.ImGuiAdapter.cppm` exporting `Extrinsic.Runtime.ImGuiAdapter` with:
  - `class ImGuiAdapter` constructed with `(Platform::IWindow&, ImGuiOverlaySystem&)`,
  - `Initialize()` creates the ImGui context and configures the platform IO (mouse cursor, clipboard, display size, framebuffer scale),
  - `BeginFrame()` pumps platform events into ImGui's IO and calls `ImGui::NewFrame()`,
  - `EndFrame()` calls `ImGui::Render()`, walks `ImDrawData`, builds `ImGuiOverlayFrame` records, calls `overlaySystem.SubmitFrame(records)`,
  - `Shutdown()` destroys the context.
- [ ] Wire `Engine` to construct `ImGuiAdapter` after `Window` and `Renderer` exist; call `BeginFrame()` after `Window::PollEvents` and before `OnVariableTick`; call `EndFrame()` after `OnVariableTick` and before `IRenderer::PrepareFrame()`.
- [ ] Provide a small editor-friendly hook (`std::function<void()>`) that the adapter calls between `BeginFrame` and `EndFrame`, so future editor code can render panels without modifying the adapter.

## Tests
- [ ] `contract;runtime` test: with the `Null` platform window, `ImGuiAdapter::Initialize()` succeeds and `EndFrame()` produces a zero-list `ImGuiOverlayFrame` (no input, no panels).
- [ ] `contract;runtime` test: with a mock IO that synthesizes one ImGui window draw, `EndFrame()` produces an overlay frame whose `DrawLists` and `UsesUserTexture` flags match the expected shape.
- [ ] `contract;runtime` test: DPI/font rebuild triggers `ImGuiOverlaySystem::Shutdown()/Initialize()` exactly once.
- [ ] No `gpu`/`vulkan` test in this slice.

## Docs
- [ ] Update `src/runtime/README.md` to flip the planned `ImGuiAdapter` umbrella row to current state.
- [ ] Refresh `docs/api/generated/module_inventory.md` after adding the module.

## Acceptance criteria
- [ ] One `ImGuiOverlayFrame` is produced per engine frame when the adapter is initialized.
- [ ] No graphics-side `imgui.h` import.
- [ ] No regression in `ImGuiOverlaySystem` semantics or `Pass.ImGui` wiring (`GRAPHICS-079`).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Importing `imgui.h` from `src/graphics/*`.
- Mutating `ImGuiOverlayFrame` field set or pass semantics.
- Adding editor-panel content here.

## Next verification step
- Land the adapter + Engine wiring, exercise the contract tests above.
