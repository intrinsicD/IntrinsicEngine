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
