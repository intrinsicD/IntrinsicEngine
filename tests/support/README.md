# Test Support

Shared helpers, fixtures, builders, and test-only utilities used by multiple test categories belong here.

## Visible-Triangle Readback Harness

`MinimalTriangleReadback.hpp` is the reusable readback harness for the
default-recipe visible-triangle smoke. It is header-only and engine-free:
triangle/clear constants mirror the explicit debug-triangle packet in
`tests/integration/graphics/Test.DefaultRecipeSurfaceGpuSmoke.cpp`, the four
deterministic sample points (one interior + three exterior corners) are chosen
so viewport Y-flip cannot change membership, and `ExpectedAt(point)` plus
`ChannelsWithinTolerance(expected, actual)` are constexpr so the same table is
locked in at compile time across CPU helper tests and the opt-in default-recipe
gpu;vulkan fixture.

CPU-only contract coverage of the harness lives in
`tests/contract/graphics/Test.MinimalTriangleReadbackHarness.cpp`
(`MinimalTriangleReadbackHarness` suite) and runs in the default gate.

The renderer-side wiring contract that drives the live readback through
`RHI::ICommandContext::CopyTextureToBuffer` + `RHI::IDevice::ReadBuffer` and
the opt-in `IRenderer::SetDefaultRecipeBackbufferReadbackBuffer(handle)` hook
is covered by `tests/contract/graphics/Test.DefaultRecipeBackbufferReadback.cpp`
(`DefaultRecipeBackbufferReadbackContract` suite), also in the default gate. The
`gpu;vulkan` smoke is the only call site that turns the harness's compile-time
expectations into a runtime `EXPECT_TRUE(Readback::ChannelsWithinTolerance(...))`
assertion on a Vulkan-capable host; format conversion (BGRA-to-RGBA, sRGB-to-
linear) is applied based on `IDevice::GetBackbufferFormat()` so the comparison
runs against the harness's canonical linear RGBA expectations.

## Operational fallback-counter stability helper

`OperationalCounterStability.hpp` is the reusable fallback-counter snapshot
diff helper for any opt-in `gpu;vulkan` fixture that needs to assert "fallback
counters do not increment across an operational frame". It is header-only and
engine-free. CPU-only contract coverage of the helper lives in the
`OperationalCounterStability` suite alongside the readback-harness tests.
