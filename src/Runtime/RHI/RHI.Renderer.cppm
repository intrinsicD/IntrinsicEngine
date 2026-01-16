module;

#include <vector>
#include <memory>

#include "RHI.Vulkan.hpp"

export module RHI:Renderer;

import :Device;
import :Swapchain;
import :Pipeline;
import :Image;
import :Profiler;

namespace RHI
{
    export class SimpleRenderer
    {
    public:
        SimpleRenderer(std::shared_ptr<VulkanDevice> device, VulkanSwapchain& swapchain);
        ~SimpleRenderer();

        void BeginFrame();
        void EndFrame();

        void BindPipeline(const GraphicsPipeline& pipeline);
        void Draw(uint32_t vertexCount);
        void SetViewport(uint32_t width, uint32_t height);

        void OnResize();

        [[nodiscard]] bool IsFrameInProgress() const { return m_IsFrameStarted; }
        [[nodiscard]] VkCommandBuffer GetCommandBuffer() const { return m_CommandBuffers[m_CurrentFrame]; }
        [[nodiscard]] uint32_t GetCurrentFrameIndex() const { return m_CurrentFrame; }
        [[nodiscard]] uint32_t GetFramesInFlight() const { return m_FramesInFlight; }

        // Expose Swapchain Image for RenderGraph Import
        [[nodiscard]] VkImage GetSwapchainImage(uint32_t index) const;
        [[nodiscard]] VkImageView GetSwapchainImageView(uint32_t index) const;
        [[nodiscard]] uint32_t GetImageIndex() const { return m_ImageIndex; }

        [[nodiscard]] GpuProfiler* GetGpuProfiler() const { return m_GpuProfiler.get(); }

        // GPU picking helper: schedule a copy of a single pixel (x,y) from an R32_UINT image into a host-visible buffer.
        // The caller must ensure the image is in VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL.
        void CopyPixel_R32_UINT_ToBuffer(VkImage srcImage,
                                        uint32_t srcWidth,
                                        uint32_t srcHeight,
                                        uint32_t x,
                                        uint32_t y,
                                        VkBuffer dstBuffer);

    private:
        std::shared_ptr<VulkanDevice> m_Device;
        VulkanSwapchain& m_Swapchain;
        VkCommandPool m_CommandPool = VK_NULL_HANDLE;

        // Arrays for Double Buffering
        std::vector<VkCommandBuffer> m_CommandBuffers;
        std::vector<VkSemaphore> m_ImageAvailableSemaphores;
        std::vector<VkSemaphore> m_RenderFinishedSemaphores;
        std::vector<VkFence> m_InFlightFences;

        uint32_t m_CurrentFrame = 0;
        uint32_t m_ImageIndex = 0;
        uint32_t m_FramesInFlight = 0;
        bool m_IsFrameStarted = false;

        void InitSyncStructures();
        std::unique_ptr<GpuProfiler> m_GpuProfiler;
    };
}
