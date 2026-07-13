---
id: GRAPHICS-122
theme: I
depends_on: [UI-036]
maturity_target: Operational
---
# GRAPHICS-122 — GPU-shaded UV view offscreen render target (optional upgrade)

## Goal
- Add an optional GPU-rendered UV view: render the selected mesh's UV layout (positions substituted by `(u, v, 0)`) to a dedicated offscreen color target with an orthographic UV-space camera — supporting a checker/texel-density background, a texture background, and a per-face distortion heatmap shaded on the GPU — and expose it to the `UI-036` split view via a bindless `ImGui::Image`, so the UV view scales to dense meshes and gains GPU shading beyond the CPU `ImDrawList` layout.

## Non-goals
- No replacement of the `UI-036` `ImDrawList` layout — that remains the default, dependency-free path; this is an opt-in upgrade for texel-density/texture/heatmap shading and very dense meshes (per `ADR-0025`, Option B).
- No second scene camera/viewport or multi-view renderer contract change — this is a small dedicated UV render path into an offscreen target, not a general multi-viewport system (that larger lift, `ADR-0025` Option D, stays deferred).
- No new ECS UV entity — the UV view renders the existing mesh entity's residency in UV space (per `ADR-0025`, the UV view is a derived view, not a separate entity).

## Context
- Owner/layer: `src/graphics/renderer` (the offscreen UV pass + ortho UV camera) wired by `src/runtime`; presented through the existing ImGui overlay. The overlay path already forwards per-draw bindless textures (`Graphics.ImGuiOverlaySystem` selects `textureBindlessIndex`), and `ImGui::Image((ImTextureID)bindlessIndex, size)` works end-to-end, so the panel can host the target with no new descriptor plumbing.
- Render shape: reuse the mesh's `GeometrySources` residency; supply a UV-space vertex position (from `v:texcoord`) and an orthographic camera fit to the UV bounds; draw chart fills + wireframe + a background (grid/checker/texel-density or a bound texture) into an offscreen `RGBA8`/`RGBA16F` target sized to the UV pane. The per-face distortion scalar (from `ParameterizationDiagnostics`) drives an optional heatmap.
- This is the operational upgrade of the UV split view; the CPU `ImDrawList` layout from `UI-036` is the `Operational` baseline, and this task is gated on it so the panel, view model, and runtime facade already exist.
- Recipe/frame composition follows the P5 recipe-driven rule: the UV pass is a named, gated pass, not a hardcoded branch. Scope it as a minimal dedicated target render, not an extension of the main scene recipe's single-camera assumptions.

## Control surfaces
- Config/UI/Agent: the UV-view render mode (`CpuLayout` vs `GpuShaded`) and background mode (grid/checker/texel-density/texture) are added to the `EngineConfig.sandbox.parameterization` view sub-section (owned by `RUNTIME-176`, extended here) so the choice round-trips through config/agent/UI; the `UI-036` panel exposes the toggle.

## Backends
- Backend axis: `gpu_vulkan_graphics` (a graphics-pipeline UV render). Falls back to the `UI-036` CPU `ImDrawList` layout when no operational device is present, with the mode reported honestly.

## Required changes
- [ ] Add a dedicated UV-view offscreen render path (ortho UV camera + a gated UV pass) that renders the selected mesh's `v:texcoord`-space geometry + background + optional distortion heatmap to an offscreen target sized to the pane; gate on `RHI::IDevice::IsOperational()`.
- [ ] Expose the target's bindless index through the runtime UV view model so `UI-036` can `ImGui::Image` it; fall back to the CPU layout on a non-operational device with the active mode reported.
- [ ] Extend the `RUNTIME-176` parameterization view config with the render-mode and background-mode toggles; route them through the config lane.
- [ ] `UI-036` renders the GPU target when `GpuShaded` is active and operational, else the CPU layout — one panel, honest mode reporting.

## Tests
- [ ] Opt-in `tests/gpu/graphics/Test.UvViewRenderTarget.cpp` labeled `gpu;vulkan`: on a Vulkan-capable host the UV target renders a non-empty image (non-background fraction above a threshold; expected clear color elsewhere) for a parameterized disk mesh, following the readback-smoke authoring pattern.
- [ ] Fallback: on the Null/non-operational device the UV view reports `CpuLayout` mode and the panel uses the `ImDrawList` path (default CPU gate).
- [ ] Config round-trip: the view render-mode/background-mode toggles round-trip through `Core.Config.EngineLoad` (contract test).

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
- Target: `Operational` on Vulkan-capable hosts (`gpu;vulkan` readback smoke cited); the CPU `ImDrawList` layout from `UI-036` is the fallback everywhere else. This task is an optional upgrade and is not required for the parameterization family to be engine-integrated and choosable.
