module;

#include <vector>

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
        static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

        SimpleRenderer(VulkanDevice& device, VulkanSwapchain& swapchain);
        ~SimpleRenderer();

        void BeginFrame();
        void EndFrame();

        void BindPipeline(const GraphicsPipeline& pipeline);
        void Draw(uint32_t vertexCount);
        void SetViewport(uint32_t width, uint32_t height);

        [[nodiscard]] bool IsFrameInProgress() const { return m_IsFrameStarted; }
        [[nodiscard]] VkCommandBuffer GetCommandBuffer() const { return m_CommandBuffers[m_CurrentFrame]; }
        [[nodiscard]] uint32_t GetCurrentFrameIndex() const { return m_CurrentFrame; }

    private:
        VulkanDevice& m_Device;
        VulkanSwapchain& m_Swapchain;
        VulkanImage* m_DepthImage = nullptr;

        // Arrays for Double Buffering
        std::vector<VkCommandBuffer> m_CommandBuffers;
        std::vector<VkSemaphore> m_ImageAvailableSemaphores;
        std::vector<VkSemaphore> m_RenderFinishedSemaphores;
        std::vector<VkFence> m_InFlightFences;

        uint32_t m_CurrentFrame = 0;
        uint32_t m_ImageIndex = 0;
        bool m_IsFrameStarted = false;

        void InitSyncStructures();
        void CreateDepthBuffer();
    };
}
