module;
#include "RHI/RHI.Vulkan.hpp"

export module Runtime.RHI.CommandUtils;

import Runtime.RHI.Device;
import Core.Logging;

export namespace Runtime::RHI::CommandUtils
{
    // Internal: Holds the pool specific to the current thread
    struct ThreadRenderContext
    {
        VkCommandPool CommandPool = VK_NULL_HANDLE;
        VulkanDevice* Owner = nullptr;
    };

    // Returns a reference to the thread-local context
    ThreadRenderContext& GetThreadContext()
    {
        thread_local ThreadRenderContext ctx;
        return ctx;
    }

    // Thread-Safe Immediate Execution using Thread-Local Pools
    void ExecuteImmediate(VulkanDevice& device, auto&& function)
    {
        ThreadRenderContext& ctx = GetThreadContext();

        // Check if the cached pool belongs to a dead device (e.g. Engine restart)
        if (ctx.CommandPool != VK_NULL_HANDLE && ctx.Owner != &device) {
            // We can't destroy the old pool because the old device is gone.
            // Just leak the handle ID (it's invalid anyway) and reset.
            ctx.CommandPool = VK_NULL_HANDLE;
            ctx.Owner = nullptr;
        }

        // 1. Lazy Initialization of Thread-Local Pool
        if (ctx.CommandPool == VK_NULL_HANDLE)
        {
            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            // TRANSIENT: Hints that buffers are short-lived
            poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            poolInfo.queueFamilyIndex = device.GetQueueIndices().GraphicsFamily.value();

            if (vkCreateCommandPool(device.GetLogicalDevice(), &poolInfo, nullptr, &ctx.CommandPool) != VK_SUCCESS)
            {
                Core::Log::Error("Failed to create thread-local command pool!");
                return;
            }

            // REGISTER WITH DEVICE for auto-cleanup
            ctx.Owner = &device;
            device.RegisterThreadLocalPool(ctx.CommandPool);
        }

        // 2. Allocate Command Buffer (No lock needed: Pool is local to this thread)
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = ctx.CommandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(device.GetLogicalDevice(), &allocInfo, &commandBuffer);

        // 3. Record
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        function(commandBuffer);
        vkEndCommandBuffer(commandBuffer);

        // 4. Submit (QUEUE IS SHARED -> NEEDS LOCK)
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence;
        vkCreateFence(device.GetLogicalDevice(), &fenceInfo, nullptr, &fence);

        VK_CHECK(device.SubmitToGraphicsQueue(submitInfo, fence));

        // 5. Wait & Cleanup
        vkWaitForFences(device.GetLogicalDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(device.GetLogicalDevice(), fence, nullptr);

        vkFreeCommandBuffers(device.GetLogicalDevice(), ctx.CommandPool, 1, &commandBuffer);
    }

    // Helper for barriers (unchanged logic)
    void TransitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout)
    {
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        }
        else
        {
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        }

        // Basic Pipeline Barrier logic
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED)
        {
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            barrier.srcAccessMask = 0;
        }

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;

        vkCmdPipelineBarrier2(cmd, &depInfo);
    }
}
