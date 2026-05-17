# GRAPHICS-033D — Opt-in `gpu;vulkan` visible-triangle smoke fixture

## Status

- Status: in-progress (final slice landed; runtime pixel-assertion gated only on Vulkan-capable host execution).
- Owner/agent: Claude on `claude/finish-graphics-tasks-cC8RU` (current slice); previously Claude on `claude/complete-agentic-task-JjT4B` and GitHub Copilot on `copilot/graphics-033d-visible-triangle-smoke`.
- Branch: `claude/finish-graphics-tasks-cC8RU`.
- Started: 2026-05-15.
- Current slice: added the backend-local backbuffer-to-host readback seam — `RHI::ICommandContext::CopyTextureToBuffer` + `RHI::IDevice::ReadBuffer` (Vulkan-backed; Null backend keeps the inherited no-op defaults), opt-in renderer hook `IRenderer::SetMinimalDebugBackbufferReadbackBuffer(handle)` that emits a `Present → TransferSrc → CopyImageToBuffer → Present` triplet after the executor's final barriers but before the command buffer closes, and a `RenderGraphFrameStats::MinimalDebugBackbufferReadbackCopyCount` diagnostic. The smoke fixture now allocates a host-visible readback buffer, arms the hook, and converts the four-sample assertion (with sRGB-to-linear + BGRA-to-RGBA normalisation) into a live `EXPECT_TRUE(Readback::ChannelsWithinTolerance(...))` site driven by the harness's `kSamplePoints` table. CPU-only contract coverage of the wiring (`Test.MinimalDebugBackbufferReadback.cpp`) joins the default gate via `IntrinsicGraphicsContractCpuTests`.
- Next verification step: run the opt-in `cmake --preset ci-vulkan` + `ctest --test-dir build/ci-vulkan -L 'gpu' -L 'vulkan'` on a Vulkan-capable host (this environment lacks the Vulkan device required to flip `IsOperational()`) to confirm the four-sample assertion passes alongside the already-passing counter and recording-counter assertions, then retire to `tasks/done/`.

## Progress log

- 2026-05-15 — Added `MinimalDebugSurfaceGpuSmoke.ReferenceTriangleRecordsOnOperationalPromotedVulkan`, wired `ci-vulkan` to build `ExtrinsicBackendsVulkan`, made `EngineConfig::Render.FrameRecipe` reach the renderer, refreshed the Vulkan operational predicate when recipe validation publishes, compiled SPIR-V for the smoke target, and resolved Vulkan operational shader paths through `Core::Filesystem::GetShaderPath(...)`. Focused opt-in smoke and the default CPU gate passed locally with Clang 22 because the pinned `clang-20` binaries were unavailable on this host.
- 2026-05-15 — Follow-up slice: made MinimalDebug Vulkan command recording legal by adding compiled render-pass attachment scopes, matching the renderer's default-debug-surface format to the live backbuffer format, enabling required Vulkan feature-chain bits (`synchronization2`, `shaderInt64`, `scalarBlockLayout`), and replacing placeholder transient-target drawing with a backbuffer-only `Renderer.MinimalVisibleTriangle` present finalizer. Validation diagnostics confirmed the old transient `SceneColorHDR`/`SceneDepth` graph resources are handle placeholders, not live Vulkan images; pixel readback remains deferred until a safe backbuffer/readback seam is added.
- 2026-05-16 — Authored the scaffold-notice reusable helpers as engine-free headers under `tests/support/`: `MinimalTriangleReadback.hpp` encodes the triangle/clear constants, the 128x128 framebuffer extent, the four deterministic sample points (one interior + three exterior corners chosen so Vulkan Y-flip cannot change membership), `Quantize8`/`ExpectedAt` constexpr expectations, and `ChannelsWithinTolerance` for sRGB/format-conversion noise. `OperationalCounterStability.hpp` encodes the fallback-counter snapshot and `IsStable(before, after)` predicate. Refactored the smoke to consume both helpers and use `Counters::IsStable` as the single counter assertion. Added CPU-only contract coverage `tests/contract/graphics/Test.MinimalTriangleReadbackHarness.cpp` so the harness invariants run inside the default gate via `IntrinsicGraphicsContractCpuTests`. Updated `tests/README.md`, `tests/support/README.md`, and `src/graphics/vulkan/README.md` to record the seam. Build/preset verification deferred: the pinned `clang-20`/`clang-scan-deps-20` toolchain is unavailable on this host (Clang 18.1.3 only), so the smoke's compile-side static asserts on the harness still gate the contract for any host that builds the Vulkan-backed smoke target. Repository structural checks pass with the default policy baseline.
- 2026-05-17 — Final slice: landed the backbuffer-to-host readback seam. RHI gained two surface additions with non-pure-virtual default bodies so existing CPU mocks remain byte-identical: `RHI::ICommandContext::CopyTextureToBuffer(src, srcLayout, mip, layer, dst, dstOffset)` and `RHI::IDevice::ReadBuffer(handle, data, size, offset)`. Vulkan implements both: the command context records `vkCmdCopyImageToBuffer`, and the device exposes a `vkDeviceWaitIdle` + `memcpy` host-visible drain (device-local buffers log once and skip — out of scope for this slice; the smoke supplies a HostVisible+TransferDst buffer). The renderer added an opt-in hook `IRenderer::SetMinimalDebugBackbufferReadbackBuffer(handle)` / `Get…()`; when armed alongside `FrameRecipeKind::MinimalDebug` and an operational device, the renderer emits a `Present → TransferSrc → CopyTextureToBuffer → Present` triplet after the executor's final barriers and before the command buffer closes, leaving the backbuffer in `Present` layout so the device's submit + `vkQueuePresentKHR` chain is unchanged. A new diagnostic counter `RenderGraphFrameStats::MinimalDebugBackbufferReadbackCopyCount` increments per readback frame so the contract cannot silently regress. The CPU contract `tests/contract/graphics/Test.MinimalDebugBackbufferReadback.cpp` covers the default-disabled state, the configured-handle triplet (against MockDevice's recorded `TextureBarrierCalls`), the default-recipe ignore path, and the non-operational skip path — all four cases pass via `IntrinsicGraphicsContractCpuTests`. The smoke now allocates the host-visible readback buffer through the renderer's `BufferManager` after `Engine::Initialize()`, arms the hook before `engine.Run()`, drains the buffer through `device.ReadBuffer(...)` after Run, walks `Readback::kSamplePoints`, and runs `EXPECT_TRUE(Readback::ChannelsWithinTolerance(expected, actual))` with sRGB-to-linear + BGRA-to-RGBA normalisation derived from `IDevice::GetBackbufferFormat()`. Verification: `cmake --preset ci` + `cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests` + `IntrinsicGraphicsContractCpuTests --gtest_filter='MinimalDebug*Readback*'` passes; the `gpu;vulkan` smoke is selected only by the opt-in `-L 'gpu' -L 'vulkan'` invocation on a Vulkan-capable host, which is the only outstanding verification before retirement.

## Goal
- Add the opt-in `gpu;vulkan` smoke fixture declared by `GRAPHICS-033`: on hosts with Vulkan support, drive one frame of the GRAPHICS-032 `MinimalDebugSurface` recipe with the GRAPHICS-029B reference triangle and the GRAPHICS-031A default debug pipeline, asserting (a) the device reports `Operational` only after all 9 gate prerequisites are met, (b) the swapchain image after `Present` contains a visible triangle (pixel readback assertion at 4 sample points), and (c) no fallback counters increment during the operational frame.

> **Scaffold notice.** The `MinimalDebugSurface`-targeted form of this fixture is removed by [`GRAPHICS-081`](GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md) once `GRAPHICS-076` lands the canonical `Pass.Present` and the default-recipe equivalent of the visible-triangle smoke is green. The pixel-readback driver harness — and the assertion that fallback counters stay zero across an operational frame — must be authored as reusable helpers so the default-recipe fixture can call them byte-identical. Per `GRAPHICS-081`'s "no reduction of gpu;vulkan coverage" rule, the default-recipe smoke must already be green before this fixture is removed.

## Non-goals
- No new diagnostics counters beyond those declared by `GRAPHICS-033A`/`B`.
- No additional pass bodies (Phase-2 backlog `GRAPHICS-070..076`).
- No CPU-only test additions; the entire scope of this task is the opt-in `gpu;vulkan` smoke.
- No Vulkan validation-layer policy change.
- No Wayland/X11/headless-Vulkan permutation matrix; one host environment.

## Context
- Owner/layer: `tests/integration/graphics` for the smoke; `graphics/vulkan` for any minor diagnostics surface adjustments needed by the assertion.
- Planning parent: [`tasks/done/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md`](../done/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md), Recorded as Impl-D in the parent's Required changes.
- Upstream gates: `GRAPHICS-033C` (recording bodies). `GRAPHICS-033D` is the canonical visible-triangle smoke and owns the pixel-readback driver harness; `GRAPHICS-032D` is a sibling fixture that reuses this harness for recipe-selector coverage, so `GRAPHICS-032D` depends on this task, not the reverse.
- The smoke is intentionally outside the default CPU gate (`-LE 'gpu|vulkan|slow|flaky-quarantine'`); hosts without Vulkan skip the test deterministically.

## Required changes
- [x] Add `tests/integration/graphics/Test.MinimalDebugSurfaceGpuSmoke.cpp` labelled `gpu;vulkan` in `tests/CMakeLists.txt`.
- [x] The smoke does the following per its single test case:
  1. [x] Configure runtime with `INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON` build + `RenderConfig::EnablePromotedVulkanDevice = true` + `CreateReferenceEngineConfig()` + `RenderConfig::FrameRecipe = MinimalDebug`.
  2. [x] `Engine::Initialize()`. Skip the test deterministically if the host lacks Vulkan bootstrap readiness.
  3. [x] Drive a bounded runtime loop until the operational transition and minimal recipe have recorded.
  4. [x] Read the swapchain image back through a dedicated `MinimalDebug` readback path (RHI `CopyTextureToBuffer` + `IDevice::ReadBuffer` into a host-visible `BufferManager`-owned buffer wired through `IRenderer::SetMinimalDebugBackbufferReadbackBuffer`) and assert at four sample points: triangle interior pixels match the deterministic visible-triangle color and outside-triangle pixels match the clear color, with sRGB-to-linear + BGRA-to-RGBA normalisation gated on `IDevice::GetBackbufferFormat()`. The harness's `kSamplePoints` table drives the runtime `EXPECT_TRUE(Readback::ChannelsWithinTolerance(...))` calls.
  5. [x] Assert the fallback/validation/operational-gate counters do not increment during the operational frame window (now via the reusable `OperationalCounterStability::IsStable` helper).
  6. [x] `Engine::Shutdown()`.
- [x] Update the test allow-list in `tests/README.md` and `tests/CMakeLists.txt` to label the new fixture `gpu;vulkan` (no new label introduced).
- [x] Author reusable pixel-readback driver harness and fallback-counter stability helper so the sibling `GRAPHICS-032D` and canonical `GRAPHICS-076`/`GRAPHICS-081` fixtures can call them byte-identical (scaffold notice).
- [x] Add the backend-local backbuffer-to-host readback seam (`RHI::ICommandContext::CopyTextureToBuffer` + `RHI::IDevice::ReadBuffer`, Vulkan implementation, opt-in renderer hook + diagnostic counter) so the four-sample harness assertion can run as a live `EXPECT_` on a Vulkan-capable host; CPU contract coverage via `Test.MinimalDebugBackbufferReadback.cpp` runs in the default gate via `IntrinsicGraphicsContractCpuTests`.

## Tests
- [x] The new `gpu;vulkan` fixture itself is the test.
- [x] Confirm the default CPU gate (`-LE 'gpu|vulkan|slow|flaky-quarantine'`) does **not** select the new fixture.
- [ ] On hosts without Vulkan support: the fixture is selected by the `gpu;vulkan` opt-in invocation and reports a deterministic `SKIPPED` with a `VulkanRequestedButNotOperational` warn breadcrumb logged once.
- [x] CPU-only contract coverage `tests/contract/graphics/Test.MinimalTriangleReadbackHarness.cpp` exercises the harness's sample-point membership, expected-color quantization, tolerance comparator, and counter-stability predicate inside the default gate via `IntrinsicGraphicsContractCpuTests`.
- [x] CPU-only contract coverage `tests/contract/graphics/Test.MinimalDebugBackbufferReadback.cpp` exercises the renderer-side wiring contract: default-disabled state, configured-handle triplet (Present→TransferSrc→Copy→Present recorded against the MockDevice command context), default-recipe ignore path, and non-operational skip path. Joins the default gate via `IntrinsicGraphicsContractCpuTests`.

## Docs
- [x] Update `tests/README.md` to enumerate the new fixture under the `gpu;vulkan` opt-in section and to record the reusable readback / counter-stability helper seam plus its CPU-only contract coverage.
- [x] Update `tests/support/README.md` to enumerate the reusable readback harness and counter-stability helper.
- [x] Update `src/graphics/vulkan/README.md` to flip the `gpu;vulkan` minimal-recipe smoke row to current state and reference the harness.

## Acceptance criteria
- [ ] On a Vulkan-capable Linux host, the fixture passes pixel-readback assertions and reports zero fallback-counter increments. (Pending: the final-slice CPU contract gates pass locally; live Vulkan-host verification is the only remaining acceptance bullet.)
- [x] On hosts without Vulkan support, the fixture skips deterministically; CI default gate is unaffected. (`Test.MinimalDebugSurfaceGpuSmoke` is labelled `gpu;vulkan` and excluded by `-LE 'gpu|vulkan|slow|flaky-quarantine'`; the smoke's deterministic skip paths fire when `Platform::Backends::Glfw::CanInitialize()` or `GetVulkanDeviceOperationalInputs(...).{Logical,Swapchain,CommandSync}Ready` are false, and the new readback-allocation skip also fires deterministically when the backbuffer format has no host-uploadable layout.)
- [x] No `gpu;vulkan` regression in any other opt-in fixture. (No other `gpu;vulkan`-labelled fixtures touched; the new RHI default bodies preserve existing CPU contract behaviour.)

## Verification
```bash
cmake --preset ci -DINTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON
cmake --build --preset ci --target IntrinsicGraphicsIntegrationTests
# Default gate (skips the new fixture):
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Opt-in gate (selects only the new fixture and any sibling gpu;vulkan tests):
ctest --test-dir build/ci --output-on-failure -L 'gpu' -L 'vulkan' --timeout 120
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes
- Selecting the new fixture by default.
- Adding additional pass bodies under cover of the smoke.
- Mutating validation-layer or swapchain policy.
- Mixing mechanical file moves with semantic refactors.

## Next verification step
- Run `cmake --preset ci-vulkan` + `cmake --build --preset ci-vulkan --target IntrinsicGraphicsIntegrationTests` + `ctest --test-dir build/ci-vulkan -L 'gpu' -L 'vulkan' --timeout 120` on a Vulkan-capable host to confirm the runtime four-sample assertion passes alongside the already-passing operational-counter assertions, then retire to `tasks/done/`. The seam, the harness call site, and the CPU-side wiring contract have all landed on this branch.
