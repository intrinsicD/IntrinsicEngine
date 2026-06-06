# BUG-016 — ExtrinsicSandbox operational frame reads back black

## Status

- Status: fixed (2026-06-05). Root cause localised and corrected; the app-default
  `gpu;vulkan` readback regression and the surface/transient/visualization smokes
  are green on this Vulkan-capable host.
- Reported: 2026-06-05 from a local `ExtrinsicSandbox` run after the promoted
  Vulkan device reached operational and the clustered validation cascade was
  cleared.
- Owner/layer: `graphics/renderer`, `graphics/vulkan`, `graphics/framegraph`.
  `app` and `runtime` composition remain out of scope except as evidence.

## Root cause and resolution

Localised by per-stage GPU readback bisection (the decisive tool named in this
task) plus tagged renderer/backend probes, all removed before close:

1. **Bindless bridge slot 0 was unstable (primary defect).** Every pass that
   sampled a single framegraph texture read it through shared bindless
   descriptor element 0 (`kFrameSampledDescriptorSlotDefault`). The whole frame
   is recorded into one command buffer against one bindless descriptor set and
   submitted once, so the *last* host-side write to a slot is what *every*
   recorded draw observes at submit. Two writers raced slot 0 after the tonemap:
   `VulkanCommandContext::TextureBarrier`/`SubmitBarriers` auto-bound *every*
   `->ShaderRead` transition into slot 0, and the `ImGuiPass` (which samples its
   own retained font-atlas/user-texture leases, never slot 0) also bound its
   `SceneColorLDR` read into slot 0. The late `SceneColorLDR ColorAttachment ->
   ShaderRead` transition before `Present` therefore left slot 0 pointing at
   `SceneColorLDR`, so the earlier-executing `PostProcessPass` tonemap sampled
   its own (cleared) output instead of `SceneColorHDR` and wrote black into
   `SceneColorLDR`. `Present` then sampled that black `SceneColorLDR`.
   - Fix: removed the legacy barrier-path auto-bind to slot 0
     (`src/graphics/vulkan/Backends.Vulkan.CommandContext.cpp`) — the renderer's
     explicit per-pass `BindFrameSampledTextureAt` is now the single authority —
     and stopped the `ImGuiPass` from clobbering the shared bridge slot
     (`src/graphics/renderer/Graphics.Renderer.cpp`).
2. **Recipe clear color was dropped at compile time (blue-background defect).**
   `CompiledRenderPassAttachment` carried only `Load`/`Store`/`Format`, and
   `BuildActiveRenderPassDesc` hardcoded a black clear, so the `BUG-015`
   default-recipe blue `SceneColorHDR` clear never reached the framebuffer.
   - Fix: propagate the recipe color clear through compilation
     (`Graphics.RenderGraph.Compiler.{cppm,cpp}`) and honor it in
     `BuildActiveRenderPassDesc` (`Graphics.Renderer.cpp`). The operational
     `ExtrinsicSandbox` frame now presents the light-blue scene background
     (BGRA8_SRGB corner ~ RGB(170,203,231)).

Outcome: `ExtrinsicSandbox` presents a non-black frame (blue clear + visible
ImGui) on a Vulkan-capable host; no validation/VUID errors; no crash.

## Goal
- Make the operational promoted-Vulkan `ExtrinsicSandbox` frame present a
  non-black image (blue default-recipe clear + working ImGui), and lock it with
  the existing app-default `gpu;vulkan` non-black backbuffer-readback regression.

## Non-goals
- Do not demote to the Null backend, disable validation, or relax the non-black
  readback assertion to hide the defect.
- Do not re-open the clustered queue-family ownership-transfer (QFOT) validation
  cascade fixed under `BUG-015`; this bug is the downstream image-content defect,
  not a validation-message defect.

## Context
- Symptom: with the promoted Vulkan device operational and every pass recording
  (`ClusterGridBuildPass`, `LightClusterAssignmentPass`, `SurfacePass`,
  postprocess/tonemap, `Present`, `ImGuiPass`), the sandbox-acceptance GPU smoke
  reads the backbuffer back entirely black (`nonBlackPixels == 0`) even though
  ImGui produces draw data (7 lists / ~1900 verts) and the blue scene-color clear
  is configured.
- Expected behavior: backbuffer readback shows the blue clear and ImGui content.
- Impact: blocks visual confirmation of the visible-triangle → mesh/graph/point
  cloud sandbox path even though the pipeline is validation-clean and operational.
- Regression window: regressed in the `GRAPHICS-039/040` window after
  `RUNTIME-095` verified a non-black frame on 2026-06-04.
- Prior art: `BUG-014` was the same *visible* symptom (black window) caused by a
  bindless descriptor-slot collision between framegraph bridge slots
  (DebugView/Present) and real texture leases. That fix reserved slots 0..2 and
  started leases at slot 3. This bug is a *separate* regression in the same
  output stage; re-audit the postprocess→present→ImGui descriptor/sampling path
  first.

## This is localizable in-environment (no interactive RenderDoc required)

The earlier note that this "needs a GPU frame capture (RenderDoc) to localise"
referred only to *interactive* GUI capture. This host runs Vulkan with the
Khronos validation layer available (the QFOT cascade was diagnosed from its
messages), so the defect can be bisected here with deterministic, headless tools.
Diagnose in this order; each step narrows the failing stage without a GUI:

1. **Per-stage GPU readback bisection (primary tool).** The engine already owns a
   backbuffer readback path (`CopyTextureToBuffer` + the app-default
   `gpu;vulkan` smoke that reports `nonBlackPixels`). Add temporary, opt-in
   per-attachment readback probes (gated behind an env var / debug flag, removed
   before close) after: `SurfacePass` (`SceneColorHDR`), the tonemap/postprocess
   output, the present blit target, and the ImGui target. Whichever attachment is
   the first to read back all-zero localises the failing stage. This is the
   decisive step and needs no external tooling.
2. **Validation-layer `debug_printf` shader probes.** Enable
   `VK_LAYER_KHRONOS_validation` `printf` and add `GL_EXT_debug_printf` prints to
   the tonemap / deferred-composition / present fragment shaders for a few fixed
   UVs (sampled color, sampled bindless slot index). Output is emitted to stdout
   headlessly, confirming whether the present/tonemap stage samples the expected
   slot and a non-zero texel.
3. **Bindless slot / descriptor audit.** Re-verify, at runtime, that the
   tonemap↔present↔ImGui sampled-image descriptors resolve to the intended
   reserved slots across the *first* operational frame (ordering/timing on cold
   start may differ from steady state), extending the `BUG-014` reservation
   contract assertions if a collision or stale-slot read is found.
4. **Optional: headless `renderdoccmd` capture.** If steps 1–3 are inconclusive,
   `renderdoccmd capture ./ExtrinsicSandbox` records a `.rdc` non-interactively
   and `renderdoccmd` can dump resources/textures from the command line — i.e.
   even RenderDoc itself runs here without the GUI. Treat this as a fallback, not
   a prerequisite.

## Required changes
- [x] Localise the first all-black stage via per-attachment readback bisection.
- [x] Root-cause and fix the postprocess/present/ImGui (or transient-resource)
      content defect with a durable engine-level correction, not a one-off.
- [x] Remove temporary diagnostic probes/flags before close.

## Tests
- [x] Extend (or rely on) the app-default `gpu;vulkan` regression so the
      operational sandbox frame asserts a non-black backbuffer readback with
      validation enabled. _(`RuntimeSandboxAcceptanceGpuSmoke.ExtrinsicSandboxDefaultConfigProducesVisibleFrameWithValidation`
      now also asserts a full-screen lit/blue background: `nonBlackPixels > totalPixels/2`.)_
- [x] Add focused coverage for whatever invariant the root cause violates
      (e.g. present/tonemap sampled-slot resolution) under `tests/`. _(The
      surface/transient/visualization `gpu;vulkan` readback smokes now validate
      the propagated blue scene clear; the ImGui-clobber + barrier-auto-bind
      regression is exercised end-to-end by the app-default present readback.)_
- [x] Preserve the default CPU-supported gate. _(255 graphics/framegraph/renderer
      CPU contract tests green; `MinimalTriangleReadbackHarness` green.)_

## Docs
- [x] Update `docs/architecture/rendering-three-pass.md` and/or
      `src/graphics/renderer/README.md` if the postprocess/present contract
      changes. _(Documented the explicit bridge-slot ownership / no-barrier-auto-bind
      rule and the recipe clear-color propagation.)_
- [x] Update `tasks/backlog/bugs/index.md` when root cause and fix are known.

## Acceptance criteria
- [x] `ExtrinsicSandbox` presents a non-black frame (blue clear + ImGui) on a
      Vulkan-capable host with validation enabled.
- [x] The non-black backbuffer readback is reliably covered by an automated
      `gpu;vulkan` regression.
- [x] Default CPU/null contract gate stays green; no layering violations.

## Verification
```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target ExtrinsicSandbox IntrinsicTests
LSAN_OPTIONS=suppressions=$PWD/lsan.supp timeout 20s ./build/ci-vulkan/bin/ExtrinsicSandbox

# Non-black backbuffer readback regression (gpu;vulkan label intersection):
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' --timeout 120

# Keep the default CPU gate green:
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Shipping a fix without a regression test when one is feasible.
- Relaxing or disabling the non-black readback assertion, validation layers, or
  demoting to Null to mask the defect.
- Moving postprocess/present descriptor policy into `runtime`/`app`.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; default CPU/null gate stays
  green.
