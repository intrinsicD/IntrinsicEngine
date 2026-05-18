# Test Support

Shared helpers, fixtures, builders, and test-only utilities used by multiple test categories belong here.

## Runtime RHI shared environment

`RuntimeRhiTestEnvironment.hpp` provides a lazy per-process headless
`RHI::VulkanContext` + `RHI::VulkanDevice` for legacy runtime/RHI,
runtime/graphics, full headless engine, asset-pipeline, and GPU-maintenance
integration tests. It preserves opt-in Vulkan skip semantics by exposing
`CheckAvailable()`, which test bodies or fixtures must call before borrowing the
shared device.

Reset contract:

- tests may borrow the shared device but must keep per-test mutable state
  (bindless systems, texture managers, transfer managers, descriptor allocators,
  buffer managers, scene managers, asset pipelines, framegraphs, geometry pools,
  GPU scenes, and asset registrations) owned by the individual fixture/test;
- tests that submit GPU work must wait for completion or garbage-collect their
  own per-test resources before returning;
- the environment waits for the shared logical device to idle during process
  teardown before releasing the device/context.

## MinimalDebug visible-triangle readback harness

`MinimalTriangleReadback.hpp` is the reusable readback harness for the
`GRAPHICS-033D` MinimalDebug visible-triangle smoke. It is header-only and
engine-free: triangle/clear constants mirror
`assets/shaders/minimal_debug_visible_triangle.{vert,frag}`, the four
deterministic sample points (one interior + three exterior corners) are chosen
so viewport Y-flip cannot change membership, and `ExpectedAt(point)` plus
`ChannelsWithinTolerance(expected, actual)` are constexpr so the same table is
locked in at compile time across:

- the GRAPHICS-033D MinimalDebug fixture
  (`MinimalDebugSurfaceGpuSmoke.ReferenceTriangleRecordsOnOperationalPromotedVulkan`
  in `tests/integration/graphics/Test.MinimalDebugSurfaceGpuSmoke.cpp`);
- the sibling GRAPHICS-032D recipe-selector fixture
  (`MinimalDebugSurfaceGpuSmoke.RecipeSelectorReachesOperationalVulkanCommandStream`
  in the same file; reuses the harness only for the framebuffer extent and
  fallback-counter contract — pixel readback stays exclusive to the 033D sibling);
- the canonical GRAPHICS-076 / GRAPHICS-081 default-recipe smoke once the
  scaffold recipe is retired.

CPU-only contract coverage of the harness lives in
`tests/contract/graphics/Test.MinimalTriangleReadbackHarness.cpp`
(`MinimalTriangleReadbackHarness` suite) and runs in the default gate.

The renderer-side wiring contract that drives the live readback through
`RHI::ICommandContext::CopyTextureToBuffer` + `RHI::IDevice::ReadBuffer` and
the opt-in `IRenderer::SetMinimalDebugBackbufferReadbackBuffer(handle)` hook
is covered by `tests/contract/graphics/Test.MinimalDebugBackbufferReadback.cpp`
(`MinimalDebugBackbufferReadbackContract` suite), also in the default gate. The
`gpu;vulkan` smoke is the only call site that turns the harness's compile-time
expectations into a runtime `EXPECT_TRUE(Readback::ChannelsWithinTolerance(...))`
assertion on a Vulkan-capable host; format conversion (BGRA-to-RGBA, sRGB-to-
linear) is applied based on `IDevice::GetBackbufferFormat()` so the comparison
runs against the harness's canonical linear RGBA expectations.

## Operational fallback-counter stability helper

`OperationalCounterStability.hpp` is the reusable fallback-counter snapshot
diff helper for any opt-in `gpu;vulkan` fixture that needs to assert "fallback
counters do not increment across an operational frame". It is header-only,
engine-free, and consumed by `Test.MinimalDebugSurfaceGpuSmoke.cpp` plus the
sibling fixtures named above. CPU-only contract coverage of the helper lives
in the `OperationalCounterStability` suite alongside the readback-harness
tests.
