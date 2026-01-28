module;
#include "RHI.Vulkan.hpp"
#include <vector>

module RHI:CommandContext.Impl;

import :CommandContext;
import Core;

namespace RHI
{
    namespace
    {
        struct ThreadData
        {
            VkCommandPool Pool = VK_NULL_HANDLE;
            VulkanDevice* OwnerDevicePtr = nullptr;

            // One secondary buffer ring per in-flight frame.
            // Engine default is 3 frames; keep a small fixed upper bound to avoid allocations in hot paths.
            static constexpr uint32_t kMaxFramesInFlight = 3;
            std::vector<VkCommandBuffer> BuffersPerFrame[kMaxFramesInFlight];
            uint32_t UsedCountPerFrame[kMaxFramesInFlight] = {0u, 0u, 0u};

            uint32_t FramesInFlight = kMaxFramesInFlight;

            // Tracks the last *epoch* (monotonic frame counter) observed for each slot.
            uint64_t LastEpochPerFrame[kMaxFramesInFlight] = {~0ull, ~0ull, ~0ull};
        };

        thread_local ThreadData s_Thread;

        [[nodiscard]] VkCommandPool GetOrCreatePool(VulkanDevice& device)
        {
            // Handle device swap/restart.
            if (s_Thread.OwnerDevicePtr != &device)
            {
                s_Thread.Pool = VK_NULL_HANDLE;
                s_Thread.OwnerDevicePtr = &device;

                for (auto& v : s_Thread.BuffersPerFrame) v.clear();
                s_Thread.UsedCountPerFrame[0] = 0;
                s_Thread.UsedCountPerFrame[1] = 0;
                s_Thread.UsedCountPerFrame[2] = 0;
                s_Thread.LastEpochPerFrame[0] = ~0ull;
                s_Thread.LastEpochPerFrame[1] = ~0ull;
                s_Thread.LastEpochPerFrame[2] = ~0ull;

                s_Thread.FramesInFlight = device.GetFramesInFlight();
                if (s_Thread.FramesInFlight == 0 || s_Thread.FramesInFlight > ThreadData::kMaxFramesInFlight)
                    s_Thread.FramesInFlight = ThreadData::kMaxFramesInFlight;
            }

            if (s_Thread.Pool == VK_NULL_HANDLE)
            {
                VkCommandPoolCreateInfo poolInfo{};
                poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                poolInfo.queueFamilyIndex = device.GetQueueIndices().GraphicsFamily.value();
                poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

                VK_CHECK(vkCreateCommandPool(device.GetLogicalDevice(), &poolInfo, nullptr, &s_Thread.Pool));
                s_Thread.OwnerDevicePtr = &device;
                s_Thread.FramesInFlight = device.GetFramesInFlight();
                if (s_Thread.FramesInFlight == 0 || s_Thread.FramesInFlight > ThreadData::kMaxFramesInFlight)
                    s_Thread.FramesInFlight = ThreadData::kMaxFramesInFlight;
                device.RegisterThreadLocalPool(s_Thread.Pool);
            }

            return s_Thread.Pool;
        }

        [[nodiscard]] VkCommandBuffer AllocateSecondary(VulkanDevice& device, VkCommandPool pool)
        {
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool = pool;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
            allocInfo.commandBufferCount = 1;

            VkCommandBuffer cmd = VK_NULL_HANDLE;
            VK_CHECK(vkAllocateCommandBuffers(device.GetLogicalDevice(), &allocInfo, &cmd));
            return cmd;
        }
    }

    VkCommandBuffer CommandContext::BeginSecondary(VulkanDevice& device,
                                                  uint64_t frameEpoch,
                                                  const SecondaryInheritanceInfo& inherit)
    {
        VkCommandPool pool = GetOrCreatePool(device);

        const uint32_t frames = (s_Thread.FramesInFlight > 0) ? s_Thread.FramesInFlight : 1u;
        const uint32_t slot = static_cast<uint32_t>(frameEpoch % static_cast<uint64_t>(frames));

        // Reset per-slot cursor once per new epoch.
        if (s_Thread.LastEpochPerFrame[slot] != frameEpoch)
        {
            s_Thread.LastEpochPerFrame[slot] = frameEpoch;
            s_Thread.UsedCountPerFrame[slot] = 0;
        }

        auto& buffers = s_Thread.BuffersPerFrame[slot];
        uint32_t& used = s_Thread.UsedCountPerFrame[slot];

        if (used >= buffers.size())
        {
            buffers.push_back(AllocateSecondary(device, pool));
        }

        VkCommandBuffer cmd = buffers[used++];

        // Reset is allowed because pool has RESET_COMMAND_BUFFER_BIT.
        // Safety relies on the renderer waiting on the in-flight fence for this slot.
        VK_CHECK(vkResetCommandBuffer(cmd, 0));

        const bool isRasterSecondary = inherit.IsRaster();

        VkCommandBufferInheritanceInfo inheritInfo{};
        inheritInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;

        VkCommandBufferInheritanceRenderingInfo inheritRendering{};
        inheritRendering.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO;

        if (isRasterSecondary)
        {
            inheritRendering.viewMask = inherit.ViewMask;
            inheritRendering.rasterizationSamples = inherit.RasterizationSamples;
            inheritRendering.colorAttachmentCount = static_cast<uint32_t>(inherit.ColorAttachmentFormats.size());
            inheritRendering.pColorAttachmentFormats = inherit.ColorAttachmentFormats.empty()
                                                           ? nullptr
                                                           : inherit.ColorAttachmentFormats.data();
            inheritRendering.depthAttachmentFormat = inherit.DepthAttachmentFormat;
            inheritRendering.stencilAttachmentFormat = inherit.StencilAttachmentFormat;

            inheritInfo.pNext = &inheritRendering;
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pInheritanceInfo = &inheritInfo;

        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (isRasterSecondary)
        {
            beginInfo.flags |= VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
        }

        VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
        return cmd;
    }

    void CommandContext::End(VkCommandBuffer cmd)
    {
        if (cmd == VK_NULL_HANDLE) return;
        VK_CHECK(vkEndCommandBuffer(cmd));
    }

    void CommandContext::Reset(VulkanDevice& device)
    {
        if (s_Thread.OwnerDevicePtr != &device) return;
        if (s_Thread.Pool == VK_NULL_HANDLE) return;

        s_Thread.UsedCountPerFrame[0] = 0;
        s_Thread.UsedCountPerFrame[1] = 0;
        s_Thread.UsedCountPerFrame[2] = 0;
        s_Thread.LastEpochPerFrame[0] = ~0ull;
        s_Thread.LastEpochPerFrame[1] = ~0ull;
        s_Thread.LastEpochPerFrame[2] = ~0ull;
        VK_CHECK(vkResetCommandPool(device.GetLogicalDevice(), s_Thread.Pool, 0));
    }
}
