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
        VkFence         AsyncComputeFence = VK_NULL_HANDLE;
        VkFence         TransferFence = VK_NULL_HANDLE;
        VkSemaphore     ImageAcquired = VK_NULL_HANDLE;
        VkSemaphore     RenderDone    = VK_NULL_HANDLE;
        VkSemaphore     QueueTimelines[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
        std::uint64_t   QueueTimelineBase[3] = {0, 0, 0};
        VkCommandPool   AsyncComputeCmdPool = VK_NULL_HANDLE;
        VkCommandPool   TransferCmdPool = VK_NULL_HANDLE;
        std::uint32_t   AcquiredImageIndex = 0;
        bool            ImageAcquiredForFrame = false;
        bool            SubmittedForPresent = false;
        std::vector<VkCommandBuffer> QueueSubmitCmdBuffers;
        std::vector<VulkanDeferredDelete> DeletionQueue;
    };
}
