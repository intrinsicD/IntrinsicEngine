// Broad Vulkan internal partition retired.
//
// Focused non-reexported partitions now declare backend internals:
// - Extrinsic.Backends.Vulkan:Device
// - Extrinsic.Backends.Vulkan:Queues
// - Extrinsic.Backends.Vulkan:Memory
// - Extrinsic.Backends.Vulkan:CommandPools
// - Extrinsic.Backends.Vulkan:Descriptors
// - Extrinsic.Backends.Vulkan:Swapchain
// - Extrinsic.Backends.Vulkan:Pipelines
// - Extrinsic.Backends.Vulkan:Transfer
// - Extrinsic.Backends.Vulkan:Sync
// - Extrinsic.Backends.Vulkan:Surface
// - Extrinsic.Backends.Vulkan:Diagnostics
//
// This file intentionally contains no `export module` declaration so generated
// module inventory no longer reports a broad `:Internal` partition.
