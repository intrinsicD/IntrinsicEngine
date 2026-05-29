# GRAPHICS-076F — Default-recipe debug-view readback returns black

## Status

- Status: done on 2026-05-29. The failure reproduced before the fix with the
  known black interior sample. The root cause was twofold: the fixture submitted
  its transient debug triangle before `BeginFrame()` cleared per-frame snapshot
  state, and promoted Vulkan's temporary sampled-framegraph descriptor bridge
  rewrote bindless slot 0 for both `DebugViewPass` and `Present` before the
  command buffer submitted. The fix seeds the triangle after `BeginFrame()` and
  reserves slot 1 for DebugView plus slot 2 for Present through
  `ICommandContext::BindFrameSampledTextureAt(...)`; postprocess keeps slot 0.
- Verification: `cmake --build --preset ci-vulkan --target
  IntrinsicGraphicsVulkanSmokeTests`; `ctest --test-dir build/ci-vulkan
  --output-on-failure -R 'DefaultRecipeSurfaceGpuSmoke' --timeout 120`; `cmake
  --build --preset ci --target IntrinsicGraphicsContractTests`; `ctest
  --test-dir build/ci --output-on-failure -R
  'DefaultRecipeBackbufferReadbackContract' -LE
  'gpu|vulkan|slow|flaky-quarantine' --timeout 60`.

## Goal
- Fix the promoted-Vulkan default-recipe debug-view pixel-readback path so `DefaultRecipeSurfaceGpuSmoke.ReferenceTriangleDebugViewReadbackMatchesMinimalHarnessSamples` returns the MinimalDebug reference-triangle sample colors instead of the cleared backbuffer.

## Non-goals
- No weakening, disabling, or relabeling of the existing `DefaultRecipeSurfaceGpuSmoke.ReferenceTriangleDebugViewReadbackMatchesMinimalHarnessSamples` smoke.
- No reuse of the MinimalDebug readback hook or `MinimalDebugBackbufferReadbackCopyCount` as default-recipe evidence.
- No broad descriptor-system redesign beyond the smallest fix needed to make the current default-recipe present/debug-view sampling path truthful.
- No RenderDoc-only acceptance; the bug must be locked by an agent-runnable `ctest` command.

## Context
- Owner/layer: `graphics/renderer` plus backend-local `graphics/vulkan` descriptor/readback wiring.
- Filed on 2026-05-28 from GRAPHICS-076E debugging before committing a known-red patch.
- Symptom: the focused opt-in Vulkan smoke records all expected default-recipe passes and the default-recipe readback copy, but the interior sample reads `0,0,0,255` instead of the MinimalDebug reference color `140,51,217,255`.
- Repro command used:

  ```bash
  ctest --test-dir build/ci-vulkan --output-on-failure -R 'DefaultRecipeSurfaceGpuSmoke.ReferenceTriangleDebugViewReadbackMatchesMinimalHarnessSamples' --timeout 120
  ```

- Current failure excerpt:

  ```text
  Sample interior_center (pixel 64,64, inside=true) expected linear RGBA=(140,51,217,255) actual linear RGBA=(0,0,0,255) raw bytes=(0,0,0,255) backbuffer format=6 pass statuses=[CullingPass=Recorded, DepthPrepass=Recorded, SurfacePass=Recorded, LinePass=Recorded, PointPass=Recorded, PostProcessHistogramPass=Recorded, PostProcessPass=Recorded, PostProcessPass=Recorded, PostProcessAAEdgePass=Recorded, PostProcessAABlendPass=Recorded, PostProcessAAResolvePass=Recorded, DebugViewPass=Recorded, ImGuiPass=SkippedUnavailable, Present=Recorded]
  ```

- Debugger findings from `gdb` probes on 2026-05-28:
  - The warmup frames bind default-recipe sampled inputs in order (`SceneDepth`, `SceneColorHDR`, `SceneColorLDR`, then `Present`).
  - The readback frame reaches explicit `DebugViewPass` binding for `SceneColorHDR` before `RecordDebugViewPass`.
  - The readback frame later binds `DebugViewRGBA` for `Present` and records `CopyTextureToBuffer` against the backbuffer.
  - The copy is therefore happening, but the sampled/presented image remains black at the deterministic interior point.
  - A likely root cause is that the submitted `DebugTrianglePacket` only drives the transient-debug surface overlay, while the test currently asks `DebugViewPass` to preview `SceneColorHDR`; if the lit/default surface path has no renderable geometry and/or the descriptor slot is overwritten by unrelated read-texture binding, `SceneColorHDR` remains black.

## Required changes
- [x] Reproduce the failure under `gdb` or an equivalent debugger and preserve a concise before/after finding in the commit/PR notes.
- [x] Identify whether the root cause is missing source pixels (`SceneColorHDR` never receives the submitted triangle), descriptor-slot overwrite/staleness, present-source alias selection, or synchronization/layout ordering.
- [x] Apply the smallest fix that keeps default-recipe readback independent from the MinimalDebug scaffold.
- [x] Remove any temporary debugger/log instrumentation before completion.

## Tests
- [x] `ctest --test-dir build/ci-vulkan --output-on-failure -R 'DefaultRecipeSurfaceGpuSmoke.ReferenceTriangleDebugViewReadbackMatchesMinimalHarnessSamples' --timeout 120` passes on a Vulkan-capable host.
- [x] `ctest --test-dir build/ci-vulkan --output-on-failure -R 'DefaultRecipeSurfaceGpuSmoke' --timeout 120` preserves the existing recipe-selector smoke.
- [x] `ctest --test-dir build/ci --output-on-failure -R 'DefaultRecipeBackbufferReadbackContract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` remains green for the CPU/null contract seam.

## Docs
- [x] Update `tasks/backlog/rendering/GRAPHICS-076E-default-recipe-pixel-readback.md` or retire it only after the readback smoke is green.
- [x] Update renderer/Vulkan docs if the fix changes the supported default-recipe readback or descriptor-binding behavior.

## Acceptance criteria
- [x] The interior sample matches the MinimalDebug reference-triangle color within `MinimalTriangleReadback` tolerance.
- [x] The exterior sample points still match the clear color.
- [x] `DefaultRecipeBackbufferReadbackCopyCount` increments only for the armed default-recipe readback path.
- [x] `MinimalDebugBackbufferReadbackCopyCount` remains zero under the default recipe.
- [x] The focused Vulkan smoke is no longer known-red.

## Verification
```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'DefaultRecipeSurfaceGpuSmoke' --timeout 120

cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -R 'DefaultRecipeBackbufferReadbackContract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Disabling, skipping, weakening, or relabeling the failing Vulkan smoke instead of fixing the pixel path.
- Treating command-stream recording alone as pixel-readback parity.
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated renderer, UI, or postprocess feature work.

## Maturity
- Target: `Operational` on Vulkan-capable hosts for the default-recipe pixel-readback path; `CPUContracted` everywhere else.
- Final state: `Operational` on the verified Vulkan-capable host for the default-recipe pixel-readback path; `CPUContracted` on CPU/null hosts.
