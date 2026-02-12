module;
#include "RHI.Vulkan.hpp"

export module RHI:CommandUtils;

import :Device;
import Core.Logging; // For VK_CHECK macro (uses Core::Log::Error)

export namespace RHI::CommandUtils
{
    // Internal: Holds the pool specific to the current thread
    struct ThreadRenderContext
    {
        VkCommandPool CommandPool = VK_NULL_HANDLE;
        VulkanDevice* OwnerDevicePtr = nullptr;
    };

    // Returns a reference to the thread-local context
    ThreadRenderContext& GetThreadContext()
    {
        thread_local ThreadRenderContext ctx;
        return ctx;
    }

    [[nodiscard]] VkCommandBuffer BeginSingleTimeCommands(VulkanDevice& device) {
        ThreadRenderContext& ctx = GetThreadContext();

        // Safety Check: Handle Engine Restart / Device Change
        // If the device instance changed (e.g. Engine restart), the old pool handle
        // in this thread_local storage is invalid (destroyed by the previous device).
        if (ctx.OwnerDevicePtr != &device) {
            ctx.CommandPool = VK_NULL_HANDLE;
            ctx.OwnerDevicePtr = &device;
        }

        if (ctx.CommandPool == VK_NULL_HANDLE) {
            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;

            poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            poolInfo.queueFamilyIndex = device.GetQueueIndices().GraphicsFamily.value();

            VK_CHECK(vkCreateCommandPool(device.GetLogicalDevice(), &poolInfo, nullptr, &ctx.CommandPool));

            ctx.OwnerDevicePtr = &device;
            device.RegisterThreadLocalPool(ctx.CommandPool);
        }

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = ctx.CommandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        VK_CHECK(vkAllocateCommandBuffers(device.GetLogicalDevice(), &allocInfo, &commandBuffer));

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
        return commandBuffer;
    }

    void EndSingleTimeCommands(VulkanDevice& device, VkCommandBuffer commandBuffer) {
        VK_CHECK(vkEndCommandBuffer(commandBuffer));

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        // Create a temporary fence
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence;
        VK_CHECK(vkCreateFence(device.GetLogicalDevice(), &fenceInfo, nullptr, &fence));

        // Submit
        VK_CHECK(device.SubmitToGraphicsQueue(submitInfo, fence));

        // Wait (Blocking!) - We keep this for compatibility with old code,
        // but new code should avoid calling this if possible.
        VK_CHECK(vkWaitForFences(device.GetLogicalDevice(), 1, &fence, VK_TRUE, UINT64_MAX));
        vkDestroyFence(device.GetLogicalDevice(), fence, nullptr);

        ThreadRenderContext& ctx = GetThreadContext();
        vkFreeCommandBuffers(device.GetLogicalDevice(), ctx.CommandPool, 1, &commandBuffer);
    }

    // Thread-Safe Immediate Execution using Thread-Local Pools
    [[deprecated("Use BeginSingleTimeCommands / EndSingleTimeCommands for more control")]]
    void ExecuteImmediate(VulkanDevice& device, auto&& function)
    {
        VkCommandBuffer cmd = BeginSingleTimeCommands(device);
        function(cmd);
        EndSingleTimeCommands(device, cmd);
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
