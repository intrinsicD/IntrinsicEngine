module;
#include <glm/glm.hpp>
#include <memory>
#include "RHI.Vulkan.hpp"

export module Graphics:RenderSystem;

import RHI;
import :Camera;
import :Geometry;
import :RenderGraph;
import Core;
import ECS;

export namespace Graphics
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
                     Core::Memory::LinearArena& frameArena,
                     GeometryStorage& geometryStorage);
        ~RenderSystem();

        void OnUpdate(ECS::Scene& scene, const CameraComponent& camera, Core::Assets::AssetManager &assetManager);

        [[nodiscard]] RHI::VulkanBuffer* GetGlobalUBO() const { return m_GlobalUBO.get(); }

    private:
        size_t m_MinUboAlignment = 0;

        std::shared_ptr<RHI::VulkanDevice> m_Device;
        RHI::VulkanSwapchain& m_Swapchain;
        RHI::SimpleRenderer& m_Renderer;
        RHI::BindlessDescriptorSystem& m_BindlessSystem;
        RHI::GraphicsPipeline& m_Pipeline;
        VkDescriptorSet m_GlobalDescriptorSet = VK_NULL_HANDLE;

        // The Global Camera UBO
        std::unique_ptr<RHI::VulkanBuffer> m_GlobalUBO;

        // RenderGraph
        RenderGraph m_RenderGraph;
        GeometryStorage& m_GeometryStorage;
    };
}
