module;
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <optional>
#include "RHI.Vulkan.hpp"

export module Graphics:RenderSystem;

import RHI;
import :Camera;
import :Geometry;
import :RenderGraph;
import :RenderPipeline;
import :ShaderRegistry;
import :PipelineLibrary;
import :GPUScene;
import :Interaction; // New: Interaction Logic
import :Presentation; // New: Presentation Logic
import Core;
import ECS;

export namespace Graphics
{
    struct RenderSystemConfig
    {
        // Future: MSAA settings, Shadow resolution, etc.
    };

    class RenderSystem
    {
    public:
        RenderSystem(const RenderSystemConfig& config,
                     std::shared_ptr<RHI::VulkanDevice> device,
                     RHI::VulkanSwapchain& swapchain,
                     RHI::SimpleRenderer& renderer,
                     RHI::BindlessDescriptorSystem& bindlessSystem,
                     RHI::DescriptorAllocator& descriptorPool,
                     RHI::DescriptorLayout& descriptorLayout,
                     PipelineLibrary& pipelineLibrary,
                     const ShaderRegistry& shaderRegistry,
                     Core::Memory::LinearArena& frameArena,
                     Core::Memory::ScopeStack& frameScope,
                     GeometryPool& geometryStorage,
                     MaterialSystem& materialSystem);
        ~RenderSystem();

        // Hot-swap: schedules activation at the start of the next successfully-begun frame.
        void RequestPipelineSwap(std::unique_ptr<RenderPipeline> pipeline);

        void OnUpdate(ECS::Scene& scene, const CameraComponent& camera, Core::Assets::AssetManager& assetManager);

        void OnResize();

        [[nodiscard]] RHI::VulkanBuffer* GetGlobalUBO() const { return m_GlobalUBO.get(); }

        // Retained-mode scene is owned by Runtime::Engine. RenderSystem consumes it during rendering.
        void SetGpuScene(GPUScene* scene) { m_GpuScene = scene; }

        // Interaction System Access
        [[nodiscard]] InteractionSystem& GetInteraction() { return m_Interaction; }
        [[nodiscard]] const InteractionSystem& GetInteraction() const { return m_Interaction; }

        // Picking API Facade (Delegates to InteractionSystem)
        using PickResultGpu = InteractionSystem::PickResultGpu;
        void RequestPick(uint32_t x, uint32_t y);
        [[nodiscard]] PickResultGpu GetLastPickResult() const;
        [[nodiscard]] std::optional<PickResultGpu> TryConsumePickResult();

    private:
        RenderSystemConfig m_Config;
        size_t m_MinUboAlignment = 0;

        const ShaderRegistry* m_ShaderRegistry = nullptr; // non-owning
        PipelineLibrary* m_PipelineLibrary = nullptr; // non-owning

        // Ownership stays with the caller, but we avoid ref-count ops in hot code.
        std::shared_ptr<RHI::VulkanDevice> m_DeviceOwner;
        RHI::VulkanDevice* m_Device = nullptr;

        RHI::VulkanSwapchain& m_Swapchain;
        RHI::SimpleRenderer& m_Renderer;
        RHI::BindlessDescriptorSystem& m_BindlessSystem;
        VkDescriptorSet m_GlobalDescriptorSet = VK_NULL_HANDLE;

        RHI::DescriptorAllocator& m_DescriptorPool;
        RHI::DescriptorLayout& m_DescriptorLayout;

        std::unique_ptr<RHI::VulkanBuffer> m_GlobalUBO;

        // Transient GPU memory for RenderGraph (page allocator). Owned here so it is destroyed
        // before VulkanDevice teardown (Engine destroys RenderSystem first).
        std::unique_ptr<RHI::TransientAllocator> m_TransientAllocator;

        RenderGraph m_RenderGraph;
        GeometryPool& m_GeometryStorage;
        MaterialSystem& m_MaterialSystem;

        // Retained-mode GPU scene (persistent SSBOs + sparse updates). Non-owning.
        GPUScene* m_GpuScene = nullptr;

        // Functional Sub-Systems
        PresentationSystem m_Presentation; // New
        InteractionSystem m_Interaction;   // New

        // Cached frame lists for UI and debug resolve selection.
        std::vector<RenderGraphDebugPass> m_LastDebugPasses;
        std::vector<RenderGraphDebugImage> m_LastDebugImages;

        // Pipeline (hot-swappable)
        std::unique_ptr<RenderPipeline> m_ActivePipeline;
        std::unique_ptr<RenderPipeline> m_PendingPipeline;

        struct RetiredPipeline
        {
            std::unique_ptr<RenderPipeline> Pipeline;
            uint64_t RetireFrame = 0;
        };
        std::vector<RetiredPipeline> m_RetiredPipelines;

        void ApplyPendingPipelineSwap(uint32_t width, uint32_t height);
        void GarbageCollectRetiredPipelines();
    };
}
