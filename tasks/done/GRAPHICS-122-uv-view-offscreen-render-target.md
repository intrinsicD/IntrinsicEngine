---
id: GRAPHICS-122
theme: I
depends_on: [UI-036]
maturity_target: Operational
completed_on: 2026-07-15
---
# GRAPHICS-122 — GPU-shaded UV view offscreen render target (optional upgrade)

## Status
- Completed on 2026-07-15 at `Operational` maturity on an operational
  promoted-Vulkan device, with CPU/null contracts preserving the explicit
  `CpuLayout` fallback.
- Commit: implementation slices `c5655359`, `871d09eb`, `86d0d1d4`, and `d0e173dd`
  delivered the config controls, face diagnostics, typed recipe declaration,
  concrete retained UV-view module, existing-registry ownership,
  runtime/config/UI composition, semantic Vulkan coverage, and synchronized
  documentation.

## Goal
- Add an optional GPU-rendered UV view: render the selected mesh's UV layout (positions substituted by `(u, v, 0)`) to a dedicated offscreen color target with an orthographic UV-space camera — supporting a checker/texel-density background, a texture background, and a per-face distortion heatmap shaded on the GPU — and expose it through the split view delivered by retired `UI-036` via a bindless `ImGui::Image`, providing a GPU-shaded path intended for dense meshes beyond the CPU `ImDrawList` layout.

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
- This is the delivered operational upgrade of the UV split view; the CPU
  `ImDrawList` layout delivered by retired `UI-036` remains the fallback.
- Recipe/frame composition follows the P5 recipe-driven rule: the UV pass is a named, gated pass, not a hardcoded branch. Scope it as a minimal dedicated target render, not an extension of the main scene recipe's single-camera assumptions.
- Frame-ordering exception: `UvViewPass` explicitly depends on `CullingPass`
  because that attachment-free predecessor publishes pending managed
  `GpuWorld` vertex/index upload barriers before UV geometry consumption.
  ImGui sampling remains resource-driven through `UvViewColor`.

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
  CPU/GPU tests, shaders, and synchronized docs. No new CMake target, registry
  abstraction, runtime service, job queue, ECS component/entity, or general
  camera/viewport contract. The existing `RenderSubsystemRegistry` owns one
  optional `UvView` entry, avoiding a renderer member or parallel lifetime
  seam.
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
- [x] Add a concrete UV-view offscreen render path (ortho UV fit + typed,
      gated `UvViewPass`) that renders the selected mesh's
      `v:texcoord`-space geometry, procedural/texture background, wireframe,
      and optional per-face distortion shading to a retained sampled target
      sized to the pane; gate on `RHI::IDevice::IsOperational()`.
- [x] Extend `ParameterizationDiagnostics` with a face-storage-aligned
      conformal-distortion payload, including deterministic invalid-face
      sentinels and analytic CPU tests; the GPU heatmap consumes this payload.
- [x] Expose the target's bindless index by extending the delivered runtime UV view model so the delivered panel can `ImGui::Image` it; fall back to the CPU layout on a non-operational device with the active mode reported.
- [x] Extend the parameterization view config delivered by retired `RUNTIME-176` with the render-mode, background-mode, and heatmap toggles; route them through the config lane.
- [x] Extend the panel delivered by retired `UI-036` to render the GPU target when `GpuShaded` is active and operational, else the CPU layout — one panel, honest mode reporting.

## Tests
- [x] Opt-in `gpu;vulkan` coverage renders four disjoint asymmetric UV
      triangle islands into one retained 128×128 target and semantically reads
      back checker plus low/mid/high/invalid face-aligned heatmap values,
      blue/orange texel-density cells, and all quadrants of a real bindless
      texture. A second smoke applies GPU/checker mode through Agent/CLI config,
      selects `ReferenceTriangle`, opens the contributed EditorShell window,
      records `UvViewPass` and `ImGuiPass`, and observes ImGui consume the
      completed user texture.
- [x] Fallback: on the Null/non-operational device the UV view reports `CpuLayout` mode and the panel uses the `ImDrawList` path (default CPU gate).
- [x] Config round-trip: the view render-mode/background-mode/heatmap toggles round-trip through `Core.Config.EngineLoad` (contract test).

## Docs
- [x] Document the UV-view render modes and the derived-view rationale in the sandbox UI docs; cross-link `ADR-0025`.
- [x] Update `docs/architecture/frame-graph.md` (or the renderer README) with the gated UV pass; refresh `docs/api/generated/module_inventory.md` for new modules.

## Acceptance criteria
- [x] On a Vulkan-capable host the UV split view can render a GPU-shaded UV target (checker/texel-density/texture background + optional distortion heatmap), cited from a `gpu;vulkan` run.
- [x] On a non-operational device the view falls back to the CPU `ImDrawList` layout with honest mode reporting.
- [x] Render mode/background mode are choosable through config/agent/UI; layering holds (geometry stays RHI-free; the UV pass lives in graphics, wired by runtime).

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

Final evidence (2026-07-15):

- The Clang 23 ASan/UBSan `ci` configure/build completed, and the focused
  config/diagnostic/recipe/registry/panel selection passed 77/77.
- The default CPU-supported gate passed 3,780/3,780 in 407.98 seconds;
  `IntrinsicBenchmarkSmoke.Run` and its validation fixture both passed.
- The final-tree `ci-vulkan` configure/build completed. The exact
  `gpu;vulkan` `UvView` selection passed 2/2 without skips in 9.09 seconds:
  direct semantic readback in 4.02 seconds and the real
  Agent/CLI → `ReferenceTriangle` → EditorShell → ImGui path in 3.62 seconds
  (`/tmp/graphics122-vulkan-uv-final-tree.log`).
- Strict layering, task policy/validation, task-state links, doc links,
  test-layout, module-inventory, session-brief, skill-mirror, clean-workshop
  automation, and diff-hygiene checks passed. Independent final review found
  no remaining actionable defects.

## Forbidden changes
- No general multi-viewport/second-scene-camera contract change (that is the deferred `ADR-0025` Option D).
- No new ECS UV entity; no hardcoded UV pass outside the recipe composition.
- No blocking readback on the poll thread; no removal of the CPU `ImDrawList` fallback.

## Maturity
- Achieved: `Operational` — direct semantic target readback and the real
  Agent/CLI → `ReferenceTriangle` → EditorShell → ImGui runtime path executed
  on operational promoted Vulkan. CPU/null paths retain explicit CPU-layout
  fallback.
