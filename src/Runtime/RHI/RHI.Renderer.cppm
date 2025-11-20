module;

#include "RHI/RHI.Vulkan.hpp"

export module Runtime.RHI.Renderer;

import Runtime.RHI.Device;
import Runtime.RHI.Swapchain;
import Runtime.RHI.Pipeline;
import Runtime.RHI.Image;

namespace Runtime::RHI
{
    export class SimpleRenderer
    {
    public:
        SimpleRenderer(VulkanDevice& device, VulkanSwapchain& swapchain);
        ~SimpleRenderer();

        void BeginFrame();
        void EndFrame();

        void BindPipeline(const GraphicsPipeline& pipeline);
        void Draw(uint32_t vertexCount);
        void SetViewport(uint32_t width, uint32_t height);
        [[nodiscard]] VkCommandBuffer GetCommandBuffer() const { return m_CommandBuffer; }

        [[nodiscard]] bool IsFrameInProgress() const { return m_IsFrameStarted; }

    private:
        VulkanDevice& m_Device;
        VulkanSwapchain& m_Swapchain;

        VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;

        // Sync Objects
        VkSemaphore m_ImageAvailableSemaphore = VK_NULL_HANDLE;
        VkSemaphore m_RenderFinishedSemaphore = VK_NULL_HANDLE;
        VkFence m_InFlightFence = VK_NULL_HANDLE;

        VulkanImage* m_DepthImage = nullptr;

        uint32_t m_ImageIndex = 0;
        bool m_IsFrameStarted = false;

        void InitSyncStructures();
        void TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
        void CreateDepthBuffer();
    };
}
