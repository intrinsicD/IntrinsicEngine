# Graphics/Backends/Null

Stub `IDevice` implementation. Exports `Extrinsic.Backends.Null` with the
`CreateNullDevice()` factory. `IDevice::IsOperational()` returns `false`
so upstream managers short-circuit at `Create()` rather than binding
leases to resources that point to nothing.

The file preserves `TODO:` markers that indicate where a real Vulkan /
VMA / swapchain wiring would live. When a real Vulkan backend is added
it should live in a sibling `../Vulkan/` directory as `Extrinsic.Backends.Vulkan`
rather than editing this file in place — keeping the null backend as a
stable fixture for tests and integration scaffolding.

## Contents

- `Backends.Null.cpp`
- `Backends.Null.cppm`
- `CMakeLists.txt`
