# Graphics/Backends/Null

Stub `IDevice` implementation. Exports `Extrinsic.Backends.Null` with the
`CreateNullDevice()` factory. `IDevice::IsOperational()` returns `false`
so upstream managers short-circuit at `Create()` rather than binding
leases to resources that point to nothing.

Null still keeps CPU-visible resource bookkeeping for contracts that are useful
without an operational GPU. It implements `GRAPHICS-118` placed-memory
bookkeeping by reporting deterministic buffer/texture memory requirements,
creating opaque memory-block handles, validating placed buffer/texture
alignment/range/memory-type compatibility, and recording the accepted
block+offset placement for introspection tests. It does not allocate real GPU
memory or enforce alias overlap hazards; render-graph planning and the Vulkan
backend own those later slices.

The file preserves `TODO:` markers that indicate where a real Vulkan /
VMA / swapchain wiring would live. When a real Vulkan backend is added
it should live in a sibling `../Vulkan/` directory as `Extrinsic.Backends.Vulkan`
rather than editing this file in place — keeping the null backend as a
stable fixture for tests and integration scaffolding.

## Contents

- `Backends.Null.cpp`
- `Backends.Null.cppm`
- `CMakeLists.txt`
