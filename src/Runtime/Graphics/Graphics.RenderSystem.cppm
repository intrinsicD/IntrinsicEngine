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
import Runtime.RenderGraph;
import Runtime.Graphics.Camera;
import Runtime.Graphics.Geometry;
import Core.Memory;
import Core.Assets;

export namespace Runtime::Graphics
{
    class RenderSystem
    {
    public:
        RenderSystem(std::shared_ptr<RHI::VulkanDevice> device,
                     RHI::VulkanSwapchain& swapchain,
                     RHI::SimpleRenderer& renderer,
                     RHI::GraphicsPipeline& pipelin,
                     Core::Memory::LinearArena &frameArena);
        ~RenderSystem();

        void OnUpdate(ECS::Scene& scene, const CameraComponent& camera);

        [[nodiscard]] RHI::VulkanBuffer* GetGlobalUBO() const { return m_GlobalUBO.get(); }

    private:
        using MaterialPtr = std::shared_ptr<Graphics::Material>;
        using GeometryPtr = std::shared_ptr<Graphics::GeometryGpuData>;

        size_t m_MinUboAlignment = 0;

        std::shared_ptr<RHI::VulkanDevice> m_Device;
        RHI::VulkanSwapchain& m_Swapchain;
        RHI::SimpleRenderer& m_Renderer;
        RHI::GraphicsPipeline& m_Pipeline;

        // The Global Camera UBO
        std::unique_ptr<RHI::VulkanBuffer> m_GlobalUBO;
        
        // RenderGraph
        Graph::RenderGraph m_RenderGraph;
    };
}
