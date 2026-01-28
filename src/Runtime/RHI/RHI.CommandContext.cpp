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

            std::vector<VkCommandBuffer> Buffers;
            uint32_t UsedCount = 0;
        };

        static thread_local ThreadData s_Thread;

        [[nodiscard]] VkCommandPool GetOrCreatePool(VulkanDevice& device)
        {
            // Handle device swap/restart.
            if (s_Thread.OwnerDevicePtr != &device)
            {
                s_Thread.Pool = VK_NULL_HANDLE;
                s_Thread.OwnerDevicePtr = &device;
                s_Thread.Buffers.clear();
                s_Thread.UsedCount = 0;
            }

            if (s_Thread.Pool == VK_NULL_HANDLE)
            {
                VkCommandPoolCreateInfo poolInfo{};
                poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                poolInfo.queueFamilyIndex = device.GetQueueIndices().GraphicsFamily.value();
                poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

                VK_CHECK(vkCreateCommandPool(device.GetLogicalDevice(), &poolInfo, nullptr, &s_Thread.Pool));
                s_Thread.OwnerDevicePtr = &device;
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

    VkCommandBuffer CommandContext::BeginSecondary(VulkanDevice& device, const SecondaryInheritanceInfo& inherit)
    {
        VkCommandPool pool = GetOrCreatePool(device);

        if (s_Thread.UsedCount >= s_Thread.Buffers.size())
        {
            s_Thread.Buffers.push_back(AllocateSecondary(device, pool));
        }

        VkCommandBuffer cmd = s_Thread.Buffers[s_Thread.UsedCount++];

        // Reset is allowed because pool has RESET_COMMAND_BUFFER_BIT.
        VK_CHECK(vkResetCommandBuffer(cmd, 0));

        // If this secondary is executed inside vkCmdBeginRendering, Vulkan requires dynamic rendering
        // inheritance info AND the RENDER_PASS_CONTINUE flag.
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

        // For SECONDARY command buffers, pInheritanceInfo must always be valid.
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

        s_Thread.UsedCount = 0;
        VK_CHECK(vkResetCommandPool(device.GetLogicalDevice(), s_Thread.Pool, 0));
    }
}
