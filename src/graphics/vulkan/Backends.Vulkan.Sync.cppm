module;

#include <cstdint>
#include <functional>
#include <vector>

#include "Vulkan.hpp"

export module Extrinsic.Backends.Vulkan:Sync;

namespace Extrinsic::Backends::Vulkan
{
#if __cpp_lib_move_only_function >= 202110L
    export using VulkanDeferredDelete = std::move_only_function<void()>;
#else
    export using VulkanDeferredDelete = std::function<void()>;
#endif

    export constexpr uint32_t kMaxFramesInFlight = 3;

    export struct PerFrame
    {
        VkCommandPool   CmdPool       = VK_NULL_HANDLE;
        VkCommandBuffer CmdBuffer     = VK_NULL_HANDLE;
        VkFence         Fence         = VK_NULL_HANDLE;
        VkSemaphore     ImageAcquired = VK_NULL_HANDLE;
        VkSemaphore     RenderDone    = VK_NULL_HANDLE;
        std::vector<VulkanDeferredDelete> DeletionQueue;
    };
}

