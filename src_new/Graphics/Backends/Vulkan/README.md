# Graphics/Backends/Vulkan

Full Vulkan 1.3 `IDevice` implementation. Exports `Extrinsic.Backends.Vulkan`
with the `CreateVulkanDevice()` factory. Requires a surface-capable physical
device with `VK_KHR_timeline_semaphore`, `VK_EXT_descriptor_indexing`
(PARTIALLY_BOUND + UPDATE_AFTER_BIND), and dynamic rendering
(`VK_KHR_dynamic_rendering` / Vulkan 1.3 core).

## Module surface

| Module | Exported API |
|---|---|
| `Extrinsic.Backends.Vulkan` | `CreateVulkanDevice()` |
| `Extrinsic.Backends.Vulkan:Internal` | *(internal — not re-exported)* |

## File inventory

| File | Responsibility |
|---|---|
| `Backends.Vulkan.cppm` | Umbrella interface — exports `CreateVulkanDevice()` |
| `Backends.Vulkan.Internal.cppm` | Non-exported `:Internal` partition — all concrete type declarations (structs, class headers, constants, mapping function forward-decls). Never re-exported from the umbrella. |
| `Backends.Vulkan.Mappings.cpp` | §2 RHI enum → Vulkan enum conversion tables (`ToVkFormat`, `ToVkImageLayout`, `AspectFromFormat`, `ToVkBufferUsage`, etc.) |
| `Backends.Vulkan.Staging.cpp` | §5 `StagingBelt` — host-visible ring-buffer for async uploads |
| `Backends.Vulkan.Profiler.cpp` | §6 `VulkanProfiler` — `IProfiler` backed by `VkQueryPool` timestamps |
| `Backends.Vulkan.Bindless.cpp` | §7 `VulkanBindlessHeap` — `IBindlessHeap` with `PARTIALLY_BOUND` descriptor array |
| `Backends.Vulkan.Transfer.cpp` | §8 `VulkanTransferQueue` — `ITransferQueue` via timeline semaphore + `StagingBelt` |
| `Backends.Vulkan.CommandContext.cpp` | §9 `VulkanCommandContext` — `ICommandContext` (one per frame-in-flight slot) |
| `Backends.Vulkan.Device.cpp` | §11 `VulkanDevice` implementations + §12 `CreateVulkanDevice()` factory |
| `Backends.Vulkan.cpp` | *(empty placeholder — kept to avoid CMake source-list churn)* |
| `Vma.cpp` | VMA implementation TU (`VMA_IMPLEMENTATION` guard) — compiles without modules |
| `VmaConfig.hpp` | VMA configuration header |
| `Vulkan.hpp` | Vulkan/volk/VMA include aggregator |

## Partition design

All internal Vulkan types (`VulkanBuffer`, `VulkanImage`, `StagingBelt`,
`VulkanProfiler`, `VulkanBindlessHeap`, `VulkanTransferQueue`,
`VulkanCommandContext`, `PerFrame`, `VulkanDevice`) are declared in
`Backends.Vulkan.Internal.cppm` — a non-exported partition of the module.
Each implementation `.cpp` file opens with:

```cpp
module;
#include "Vulkan.hpp"   // provides VkDevice, VmaAllocator, etc.
module Extrinsic.Backends.Vulkan;
import :Internal;       // gets all concrete types + re-exported Extrinsic imports
```

The umbrella `Backends.Vulkan.cppm` does **NOT** `export import :Internal`,
so external consumers of `Extrinsic.Backends.Vulkan` see only
`CreateVulkanDevice()` — the concrete Vulkan types are fully hidden.

## Dependencies

```
Extrinsic.Backends.Vulkan
  → ExtrinsicRHI     (public)
  → ExtrinsicCore    (private)
  → Vulkan::Vulkan   (private)
  → volk             (private)
  → VulkanMemoryAllocator (private)
  → glfw             (private)
```

