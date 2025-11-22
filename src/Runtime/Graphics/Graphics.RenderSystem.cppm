module;
#include <glm/glm.hpp>
#include <memory>

export module Runtime.Graphics.RenderSystem;

import Runtime.RHI.Device;
import Runtime.RHI.Swapchain;
import Runtime.RHI.Renderer;
import Runtime.RHI.Pipeline;
import Runtime.RHI.Buffer;
import Runtime.ECS.Scene;

export namespace Runtime::Graphics
{
    struct CameraData
    {
        glm::mat4 View;
        glm::mat4 Proj;
    };

    class RenderSystem
    {
    public:
        RenderSystem(RHI::VulkanDevice& device,
                     RHI::VulkanSwapchain& swapchain,
                     RHI::SimpleRenderer& renderer,
                     RHI::GraphicsPipeline& pipeline);
        ~RenderSystem();

        void OnUpdate(ECS::Scene& scene, const CameraData& camera);

        [[nodiscard]] RHI::VulkanBuffer* GetGlobalUBO() const { return m_GlobalUBO.get(); }

    private:
        size_t m_MinUboAlignment = 0;

        RHI::VulkanDevice& m_Device;
        RHI::VulkanSwapchain& m_Swapchain;
        RHI::SimpleRenderer& m_Renderer;
        RHI::GraphicsPipeline& m_Pipeline;

        // The Global Camera UBO
        std::unique_ptr<RHI::VulkanBuffer> m_GlobalUBO;
        char* m_MappedCameraPtr = nullptr;
        size_t m_CameraStride = 0;
    };
}
