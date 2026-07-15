---
id: GRAPHICS-122
theme: I
depends_on: [UI-036]
maturity_target: Operational
---
# GRAPHICS-122 — GPU-shaded UV view offscreen render target (optional upgrade)

## Status
- In progress on branch `codex/arch-006-completion`; owner: Codex.
- `UI-036` is retired at `Operational`; its single parameterization window,
  pointer-free UV view model, and CPU `ImDrawList` path are the delivered
  integration and fallback surfaces extended by this task.
- One bounded implementation slice adds a concrete graphics UV-view module,
  one typed dedicated offscreen pass/resource, runtime composition, config/UI
  parity, a face-storage-aligned geometry diagnostic, CPU contracts, and an
  opt-in Vulkan readback smoke.
- Promotion and right-sizing validation are clean; implementation begins with
  the CPU config/diagnostic contracts before renderer integration.

## Goal
- Add an optional GPU-rendered UV view: render the selected mesh's UV layout (positions substituted by `(u, v, 0)`) to a dedicated offscreen color target with an orthographic UV-space camera — supporting a checker/texel-density background, a texture background, and a per-face distortion heatmap shaded on the GPU — and expose it through the split view delivered by retired `UI-036` via a bindless `ImGui::Image`, so the UV view scales to dense meshes and gains GPU shading beyond the CPU `ImDrawList` layout.

## Non-goals
- No replacement of the CPU `ImDrawList` layout delivered by retired `UI-036` — that remains the default, dependency-free path; this is an opt-in upgrade for texel-density/texture/heatmap shading and very dense meshes (per `ADR-0025`, Option B).
- No second scene camera/viewport or multi-view renderer contract change — this is a small dedicated UV render path into an offscreen target, not a general multi-viewport system (that larger lift, `ADR-0025` Option D, stays deferred).
- No new ECS UV entity — the UV view renders the existing mesh entity's residency in UV space (per `ADR-0025`, the UV view is a derived view, not a separate entity).

## Context
- Owner/layer: `src/graphics/renderer` (the offscreen UV pass + ortho UV camera) wired by `src/runtime`; presented through the existing ImGui overlay. The overlay path already forwards per-draw bindless textures (`Graphics.ImGuiOverlaySystem` selects `textureBindlessIndex`), and `ImGui::Image((ImTextureID)bindlessIndex, size)` works end-to-end, so the panel can host the target with no new descriptor plumbing.
- Render shape: reuse the selected surface's `GpuWorld` geometry residency;
  supply a UV-space vertex position from its resident `v:texcoord` stream and
  fit the UV bounds in clip space. Draw chart fills + wireframe + a background
  (grid/checker/texel-density or the selected material's resident albedo)
  into an offscreen `RGBA8` target sized to the UV pane. Extend
  `ParameterizationDiagnostics` with a face-storage-aligned conformal
  distortion payload so the optional GPU heatmap consumes the same canonical
  CPU diagnostic formula instead of defining a renderer-owned metric.
- This is the operational upgrade of the UV split view; the CPU `ImDrawList` layout delivered by retired `UI-036` is the `Operational` baseline. Its dependency is satisfied as of 2026-07-15, so the delivered panel, view model, and runtime facade are ready to extend in this active task.
- Recipe/frame composition follows the P5 recipe-driven rule: the UV pass is a named, gated pass, not a hardcoded branch. Scope it as a minimal dedicated target render, not an extension of the main scene recipe's single-camera assumptions.

## Right-sizing
- **Element:** a general preview-renderer, second-view interface, or new
  service/queue/registry would create a hypothetical seam for one UV panel and
  is flagged by the single-consumer and one-adapter heuristics.
- **Simpler alternative:** add one concrete `Graphics.UvView` module with plain
  request/state records and owned target/pipeline/diagnostic-upload resources.
  Integrate its one typed `UvViewPass`/`UvViewColor` declaration into the
  existing frame recipe and command router; runtime resolves the selected
  surface's existing `GpuGeometryHandle`, selected-material texture view, and
  current face-aligned diagnostic payload through the existing Sandbox editor
  context.
- **Blast radius:** the existing core config schema/serializer, one graphics
  module plus frame-recipe/renderer wiring, the existing runtime
  parameterization facade, the existing app method-panel module, focused
  CPU/GPU tests, shaders, and synchronized docs. No new CMake target,
  subsystem registry entry, runtime service, job queue, ECS component/entity,
  or general camera/viewport contract.
- **Resource/lifetime rule:** the graphics module owns its retained sampled
  target and upload buffers; requests are copied at submission, frame recording
  borrows only stable owned data, and unavailable/invalid GPU resources report
  CPU fallback deterministically.
- **Reintroduction trigger:** introduce a general multi-view/preview seam only
  when a second independent rendered editor view needs distinct camera,
  lifetime, and scheduling policy.

## Control surfaces
- Config/UI/Agent: extend the `EngineConfig.sandbox.parameterization` model delivered by retired `RUNTIME-176` with UV-view render mode (`CpuLayout` vs `GpuShaded`), background mode (grid/checker/texel-density/texture), and distortion-heatmap enablement; then extend the panel delivered by retired `UI-036` with the same controls so all choices round-trip through config/agent/UI.

## Backends
- Backend axis: `gpu_vulkan_graphics` (a graphics-pipeline UV render). Falls back to the CPU `ImDrawList` layout delivered by retired `UI-036` when no operational device is present, with the mode reported honestly.

## Required changes
- [ ] Add a concrete UV-view offscreen render path (ortho UV fit + typed,
      gated `UvViewPass`) that renders the selected mesh's
      `v:texcoord`-space geometry, procedural/texture background, wireframe,
      and optional per-face distortion shading to a retained sampled target
      sized to the pane; gate on `RHI::IDevice::IsOperational()`.
- [ ] Extend `ParameterizationDiagnostics` with a face-storage-aligned
      conformal-distortion payload, including deterministic invalid-face
      sentinels and analytic CPU tests; the GPU heatmap consumes this payload.
- [ ] Expose the target's bindless index by extending the delivered runtime UV view model so the delivered panel can `ImGui::Image` it; fall back to the CPU layout on a non-operational device with the active mode reported.
- [ ] Extend the parameterization view config delivered by retired `RUNTIME-176` with the render-mode, background-mode, and heatmap toggles; route them through the config lane.
- [ ] Extend the panel delivered by retired `UI-036` to render the GPU target when `GpuShaded` is active and operational, else the CPU layout — one panel, honest mode reporting.

## Tests
- [ ] Opt-in `tests/integration/graphics/Test.UvViewGpuSmoke.cpp` labeled `gpu;vulkan` (mirroring `Test.DefaultRecipeSurfaceGpuSmoke.cpp` / `Test.ImGuiSurfaceGpuSmoke.cpp`): on a Vulkan-capable host the UV target renders a non-empty image (non-background fraction above a threshold; expected clear color elsewhere) for a parameterized disk mesh, following the readback-smoke authoring pattern.
- [ ] Fallback: on the Null/non-operational device the UV view reports `CpuLayout` mode and the panel uses the `ImDrawList` path (default CPU gate).
- [ ] Config round-trip: the view render-mode/background-mode/heatmap toggles round-trip through `Core.Config.EngineLoad` (contract test).

## Docs
- [ ] Document the UV-view render modes and the derived-view rationale in the sandbox UI docs; cross-link `ADR-0025`.
- [ ] Update `docs/architecture/frame-graph.md` (or the renderer README) with the gated UV pass; refresh `docs/api/generated/module_inventory.md` for new modules.

## Acceptance criteria
- [ ] On a Vulkan-capable host the UV split view can render a GPU-shaded UV target (checker/texel-density/texture background + optional distortion heatmap), cited from a `gpu;vulkan` run.
- [ ] On a non-operational device the view falls back to the CPU `ImDrawList` layout with honest mode reporting.
- [ ] Render mode/background mode are choosable through config/agent/UI; layering holds (geometry stays RHI-free; the UV pass lives in graphics, wired by runtime).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'UvView|Parameterization' -LE 'gpu|vulkan' --timeout 120
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'UvView' --timeout 180
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No general multi-viewport/second-scene-camera contract change (that is the deferred `ADR-0025` Option D).
- No new ECS UV entity; no hardcoded UV pass outside the recipe composition.
- No blocking readback on the poll thread; no removal of the CPU `ImDrawList` fallback.

## Maturity
- Target: `Operational` on Vulkan-capable hosts (`gpu;vulkan` readback smoke cited); the CPU `ImDrawList` layout delivered by retired `UI-036` is the fallback everywhere else. This task is an optional upgrade and is not required for the parameterization family to be engine-integrated and choosable.
