# GRAPHICS-076E — Default-recipe pixel-readback parity harness

## Status

- Status: done on 2026-05-29. The readback hook/counter and CPU contract seam
  had already landed in `cc06edef`; the remaining promoted-Vulkan pixel parity
  blocker was split to `GRAPHICS-076F` and is now resolved. The opt-in
  `DefaultRecipeSurfaceGpuSmoke.ReferenceTriangleDebugViewReadbackMatchesMinimalHarnessSamples`
  smoke reads the canonical default-recipe backbuffer and matches the four
  `MinimalTriangleReadback` samples without using the MinimalDebug readback
  hook or counter.

## Goal
- Add a default-recipe backbuffer readback harness that proves the canonical default recipe renders the reference triangle with deterministic sample-point parity, without relying on the `MinimalDebugSurface` readback seam.

## Non-goals
- No new default-recipe pass bodies; `GRAPHICS-076` owns command-stream operational proof.
- No deletion of the `MinimalDebugSurface` scaffold; `GRAPHICS-081` owns scaffold retirement.
- No broad renderer API reshaping beyond the narrow readback hook/counter needed for default-recipe acceptance.
- No performance or image-quality claims beyond the four deterministic sample points.

## Context
- Owner/layer: `graphics/renderer` with opt-in `gpu;vulkan` integration coverage.
- `GRAPHICS-076` Slice D graduates the default recipe from `Skipped` to `Passed` using the recipe-selector command-stream proof (`DefaultRecipeSurfaceGpuSmoke.RecipeSelectorReachesOperationalVulkanCommandStream`).
- The existing readback hook, `IRenderer::SetMinimalDebugBackbufferReadbackBuffer(...)`, and the diagnostic counter `RenderGraphFrameStats::MinimalDebugBackbufferReadbackCopyCount` are intentionally scoped to `FrameRecipeKind::MinimalDebug`.
- This task exists so `GRAPHICS-076` can close without silently expanding Slice D into a renderer API change while preserving a named follow-up for default-recipe pixel parity before or during `GRAPHICS-081` scaffold retirement.
- 2026-05-28 status: the renderer API (`IRenderer::SetDefaultRecipeBackbufferReadbackBuffer(...)` + `RenderGraphFrameStats::DefaultRecipeBackbufferReadbackCopyCount`), recording wiring (`Graphics.Renderer.cpp` lines ~2286–2314), `tests/contract/graphics/Test.DefaultRecipeBackbufferReadback.cpp`, and the `DefaultRecipeSurfaceGpuSmoke.ReferenceTriangleDebugViewReadbackMatchesMinimalHarnessSamples` opt-in Vulkan fixture all landed via commit `cc06edef` ("Add default recipe readback seam"). The Vulkan readback parity (the `ReferenceTriangleDebugViewReadbackMatchesMinimalHarnessSamples` smoke returning black at the interior sample) is also tracked separately as `GRAPHICS-076F` per the cc06edef commit message.
- 2026-05-29 status: [`BUG-013`](../../done/BUG-013-backbuffer-readback-contract-vtable-segv.md) is **resolved (not reproducible on a clean `ci` build)** — both `DefaultRecipeBackbufferReadbackContract.ConfiguredHandleRecordsReadbackTripletOnce` and `MinimalDebugBackbufferReadbackContract.ConfiguredHandleRecordsReadbackTripletOnce` pass on the default CPU gate (CTest #25/#87, label `contract`; 225/225 in `IntrinsicGraphicsContractCpuTests`). The reported SEGV was a stale incremental module-BMI artifact; see `src/graphics/rhi/README.md` for the clean-rebuild rule. The CPU contract gate is **no longer blocked**; the remaining open work here is the default-recipe pixel-parity assertions below.

## Required changes
- [x] Add a default-recipe readback hook, or generalize the existing readback hook with recipe-specific diagnostics, so a caller can arm a host-visible backbuffer copy under `FrameRecipeKind::Default` only.
- [x] Add a default-recipe diagnostic counter (for example `DefaultRecipeBackbufferReadbackCopyCount`) so the test can assert the copy triplet recorded without conflating MinimalDebug and default-recipe readbacks.
- [x] Ensure the readback triplet preserves the default present path's final backbuffer layout and does not alter normal renderer behavior when the hook is not armed.
- [x] Reuse `tests/support/MinimalTriangleReadback.hpp` sample points/tolerances for byte-identical parity with the MinimalDebug harness.

## Tests
- [x] Add `contract;graphics` coverage for the default-recipe readback hook/counter fail-closed behavior when no buffer is armed and when the device is non-operational.
- [x] Add or extend an opt-in `gpu;vulkan;graphics` smoke that reads back the default-recipe backbuffer and checks the four deterministic sample points.
- [x] Preserve the existing `DefaultRecipeSurfaceGpuSmoke.RecipeSelectorReachesOperationalVulkanCommandStream` recipe-selector proof as a separate test path.

## Docs
- [x] Update `src/graphics/vulkan/README.md` and `src/graphics/renderer/README.md` with the default-recipe readback hook and diagnostic counter.
- [x] Update `GRAPHICS-081` notes if this task becomes a prerequisite for scaffold deletion.

## Acceptance criteria
- [x] The default recipe can be armed for readback on a Vulkan-capable host and returns the expected four-sample reference-triangle pattern.
- [x] MinimalDebug readback counters stay zero under the default recipe; default-recipe readback counters stay zero when the hook is not armed.
- [x] CPU/null tests prove the API and diagnostics fail closed without requiring Vulkan.
- [x] The opt-in `gpu;vulkan;graphics` readback smoke remains outside the default CPU gate.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests
LSAN_OPTIONS=suppressions=$PWD/lsan.supp ctest --test-dir build/ci-vulkan --output-on-failure -R 'DefaultRecipeSurfaceGpuSmoke' --timeout 120
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Adding ImGui overlay code (`GRAPHICS-079` / `RUNTIME-090`).
- Treating the MinimalDebug readback hook/counter as evidence for default-recipe pixel parity.
- Weakening the existing default-recipe command-stream smoke.

## Maturity
- Target: `Operational` on Vulkan-capable hosts for default-recipe pixel readback parity; `CPUContracted` everywhere else.
- This follow-up is explicitly not required for `GRAPHICS-076` Slice D's recipe-selector command-stream graduation, but may be required before or during `GRAPHICS-081` scaffold retirement depending on reviewer acceptance.
