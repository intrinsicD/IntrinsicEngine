# Test Support

Shared helpers, fixtures, builders, and test-only utilities used by multiple test categories belong here.

`EditorFeatureTestContext.hpp` supplies the editor context and shared canonical
vertex/UV/topology builders used by the Models, Visualization, MeshMethods and
ClusteringMethods contract partitions. Selectable entities and point-cloud
sources share the same builders. Fixed three-vertex graph and icosahedron fixtures
also compile in that owner; graph/mesh construction imports stay in its implementation.
Context construction and presentation recipe/state
fixtures also share compiled helpers. Its context conversions and geometry builders
compile once in `EditorFeatureTestContext.cpp`; `EditorFeatureTestSupportObjs` links them into
the runtime contract, editor integration, runtime graphics and Sandbox GPU-smoke
test executables. The context imports `Graphics.RenderDiagnostics` for copied
frame statistics; renderer execution consumers import `Graphics.Renderer`
themselves. Asset import uses command callbacks; the context does not borrow the
live asset service. Tests that use that service import its module directly.

`GraphicsTestSupport.hpp` declares command-pass inspection and exact readback
format conversion. The bodies compile once in `GraphicsTestSupport.cpp` through
`GraphicsTestSupportObjs`, linked into the graphics CPU contract and Vulkan
smoke executables. The `GraphicsTestSupport` CPU suite checks first-match lookup,
channel order, sRGB conversion and alpha preservation. `MockRHI.hpp` owns the
mock backbuffer-barrier query; expected pixels and packet setup stay in callers.
`MockDevice` construction and destruction compile in `MockRHI.cpp` alongside
the recorded-command methods, linked through `MockRhiTestSupportObjs`.

`SandboxEditorJobHarness.hpp` declares the shared editor-job fixture. Its
snapshot, command callbacks, drain loop and scheduler lifecycle compile once in
`SandboxEditorJobHarness.cpp`, linked by the runtime contract test target.

## Compiled geometry fixtures

`geometry/Test_MeshBuilders.h` declares shared mesh fixtures; their bodies compile
in `geometry/MeshBuilders.cpp`. `GeometryMeshBuilderTestSupportObjs` links into
`IntrinsicGeometryTests`, `IntrinsicGeometryProcessStateTests` and
`IntrinsicGeometrySlowTests`. Consumers import `Geometry.HalfedgeMesh` or the
`Geometry` umbrella before the header. Default arguments stay in the header;
the support object adds no test cases or registration labels.

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
