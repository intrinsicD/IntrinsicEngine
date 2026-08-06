# Graphics/Backends/Null

Stub `IDevice` implementation. Exports `Extrinsic.Backends.Null` with the
`CreateNullDevice()` factory. `IDevice::IsOperational()` returns `false`
so upstream managers short-circuit at `Create()` rather than binding
leases to resources that point to nothing.

Null still keeps CPU-visible resource bookkeeping for contracts that are useful
without an operational GPU. It implements `GRAPHICS-118` placed-memory
bookkeeping by reporting deterministic buffer/texture memory requirements,
creating opaque memory-block handles with the selected block-base alignment,
validating placed buffer/texture alignment/range/memory-type compatibility, and
recording the accepted
block+offset placement for introspection tests. It does not allocate real GPU
memory or enforce alias overlap hazards; render-graph planning owns lifetime
safety, renderer allocation owns the opt-in placed allocation and fallback
lanes, and the Vulkan backend owns real GPU placed binding.

The Null implementation stays intentionally GPU-free and deterministic. The
promoted `Extrinsic.Backends.Vulkan` implementation lives separately under
`src/graphics/vulkan/`; native device, VMA, queue, and swapchain behavior does
not belong in this fixture.

## Contents

- `Backends.Null.cpp`
- `Backends.Null.cppm`
- `CMakeLists.txt`
