module;
#include "RHI.Vulkan.hpp"
#include <span>

export module RHI:CommandContext;

import :Device;

export namespace RHI
{
    struct SecondaryInheritanceInfo
    {
        // Dynamic rendering inheritance (VK_KHR_dynamic_rendering).
        // Only needed when the secondary will be executed inside a vkCmdBeginRendering scope.
        std::span<const VkFormat> ColorAttachmentFormats{};
        VkFormat DepthAttachmentFormat = VK_FORMAT_UNDEFINED;
        VkFormat StencilAttachmentFormat = VK_FORMAT_UNDEFINED;
        VkSampleCountFlagBits RasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        uint32_t ViewMask = 0;

        [[nodiscard]] constexpr bool IsRaster() const noexcept
        {
            return !ColorAttachmentFormats.empty() ||
                   DepthAttachmentFormat != VK_FORMAT_UNDEFINED ||
                   StencilAttachmentFormat != VK_FORMAT_UNDEFINED;
        }
    };

    class CommandContext
    {
    public:
        // Returns a Secondary command buffer ready for recording.
        // Uses a thread-local VkCommandPool internally.
        [[nodiscard]] static VkCommandBuffer BeginSecondary(VulkanDevice& device,
                                                           const SecondaryInheritanceInfo& inherit = {});

        // Finalize the secondary buffer.
        static void End(VkCommandBuffer cmd);

        // Optional optimization hook: resets the per-thread command pool.
        // Safe to call at end-of-frame when you know the GPU has finished executing the secondary buffers.
        static void Reset(VulkanDevice& device);
    };
}
