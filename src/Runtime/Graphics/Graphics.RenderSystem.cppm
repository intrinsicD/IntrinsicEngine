module;
#include <glm/glm.hpp>
#include <memory>
#include "RHI/RHI.Vulkan.hpp"

export module Runtime.Graphics.RenderSystem;

import Runtime.RHI.Bindless;
import Runtime.RHI.Device;
import Runtime.RHI.Swapchain;
import Runtime.RHI.Renderer;
import Runtime.RHI.Pipeline;
import Runtime.RHI.Descriptors;
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
                     RHI::BindlessDescriptorSystem& bindlessSystem,
                     RHI::DescriptorPool& descriptorPool,
                     RHI::DescriptorLayout& descriptorLayout,
                     RHI::GraphicsPipeline& pipeline,
                     Core::Memory::LinearArena& frameArena);
        ~RenderSystem();

        void OnUpdate(ECS::Scene& scene, const CameraComponent& camera);

        [[nodiscard]] RHI::VulkanBuffer* GetGlobalUBO() const { return m_GlobalUBO.get(); }

    private:
        size_t m_MinUboAlignment = 0;

        std::shared_ptr<RHI::VulkanDevice> m_Device;
        RHI::VulkanSwapchain& m_Swapchain;
        RHI::BindlessDescriptorSystem& m_BindlessSystem;
        RHI::SimpleRenderer& m_Renderer;
        RHI::GraphicsPipeline& m_Pipeline;
        VkDescriptorSet m_GlobalDescriptorSet = VK_NULL_HANDLE;

        // The Global Camera UBO
        std::unique_ptr<RHI::VulkanBuffer> m_GlobalUBO;

        // RenderGraph
        Graph::RenderGraph m_RenderGraph;
    };
}
