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
| `Extrinsic.Backends.Vulkan:{Device,Queues,Memory,CommandPools,Descriptors,Swapchain,Pipelines,Transfer,Sync,Surface,Diagnostics}` | *(internal partitions — not re-exported)* |

## File inventory

| File | Responsibility |
|---|---|
| `Backends.Vulkan.cppm` | Umbrella interface — exports `CreateVulkanDevice()` |
| `Backends.Vulkan.Device.cppm` | Non-re-exported `:Device` partition — `VulkanDevice` declaration and aggregate backend ownership. |
| `Backends.Vulkan.Queues.cppm` | Non-re-exported `:Queues` partition — queue-family and raw queue state contracts. |
| `Backends.Vulkan.Memory.cppm` | Non-re-exported `:Memory` partition — backend buffer/image/sampler records. |
| `Backends.Vulkan.CommandPools.cppm` | Non-re-exported `:CommandPools` partition — command-context declaration. |
| `Backends.Vulkan.Descriptors.cppm` | Non-re-exported `:Descriptors` partition — bindless descriptor heap declaration. |
| `Backends.Vulkan.Swapchain.cppm` | Non-re-exported `:Swapchain` partition — swapchain state contract. |
| `Backends.Vulkan.Pipelines.cppm` | Non-re-exported `:Pipelines` partition — pipeline record and RHI-to-Vulkan mapping declarations. |
| `Backends.Vulkan.Transfer.cppm` | Non-re-exported `:Transfer` partition — staging belt and transfer queue declarations. |
| `Backends.Vulkan.Sync.cppm` | Non-re-exported `:Sync` partition — frames-in-flight, per-frame sync, deferred deletion. |
| `Backends.Vulkan.Surface.cppm` | Non-re-exported `:Surface` partition — surface ownership contract. |
| `Backends.Vulkan.Diagnostics.cppm` | Non-re-exported `:Diagnostics` partition — profiler/timestamp declaration. |
| `Backends.Vulkan.Internal.cppm` | Retired migration stub; contains no module declaration. |
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

Internal Vulkan types are declared in focused non-re-exported module partitions.
Implementation `.cpp` files import the partition that owns the declarations they
define or consume. For example:

```cpp
module;
#include "Vulkan.hpp"   // provides VkDevice, VmaAllocator, etc.
module Extrinsic.Backends.Vulkan;
import :Transfer;       // gets StagingBelt/VulkanTransferQueue declarations
```

The umbrella `Backends.Vulkan.cppm` does **not** `export import` any internal
partition, so external consumers of `Extrinsic.Backends.Vulkan` see only
`CreateVulkanDevice()` — concrete Vulkan types remain hidden.

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

