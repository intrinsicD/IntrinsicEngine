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
import :Passes.Picking;
import :Passes.Forward;
import :Passes.DebugView;
import :Passes.ImGui;
import :ShaderLibrary;
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
                     RHI::DescriptorAllocator& descriptorPool,
                     RHI::DescriptorLayout& descriptorLayout,
                     RHI::GraphicsPipeline& pipeline,
                     RHI::GraphicsPipeline& pickPipeline,
                     Core::Memory::LinearArena& frameArena,
                     Core::Memory::ScopeStack& frameScope,
                     GeometryStorage& geometryStorage);
        ~RenderSystem();

        void OnUpdate(ECS::Scene& scene, const CameraComponent& camera, Core::Assets::AssetManager& assetManager);

        void OnResize();

        void RequestPick(uint32_t x, uint32_t y);

        struct PickResultGpu
        {
            bool HasHit = false;
            uint32_t EntityID = 0;
        };

        [[nodiscard]] PickResultGpu GetLastPickResult() const;
        [[nodiscard]] std::optional<PickResultGpu> TryConsumePickResult();

        [[nodiscard]] RHI::VulkanBuffer* GetGlobalUBO() const { return m_GlobalUBO.get(); }

        struct DebugViewState
        {
            bool Enabled = false;
            Core::Hash::StringID SelectedResource = Core::Hash::StringID("PickID");
            ResourceID SelectedResourceId = kInvalidResource;
            bool ShowInViewport = false;
            float DepthNear = 0.1f;
            float DepthFar = 1000.0f;
        };

        [[nodiscard]] const DebugViewState& GetDebugViewState() const { return m_DebugView; }
        void SetDebugViewSelectedResource(Core::Hash::StringID name) { m_DebugView.SelectedResource = name; }
        void SetDebugViewShowInViewport(bool show) { m_DebugView.ShowInViewport = show; }

        //TODO: finish implementing the ShaderLibrary hot reload
    private:
        size_t m_MinUboAlignment = 0;

        // Ownership stays with the caller, but we avoid ref-count ops in hot code.
        std::shared_ptr<RHI::VulkanDevice> m_DeviceOwner;
        RHI::VulkanDevice* m_Device = nullptr;

        RHI::VulkanSwapchain& m_Swapchain;
        RHI::SimpleRenderer& m_Renderer;
        RHI::BindlessDescriptorSystem& m_BindlessSystem;
        RHI::GraphicsPipeline& m_Pipeline;
        RHI::GraphicsPipeline& m_PickPipeline;
        VkDescriptorSet m_GlobalDescriptorSet = VK_NULL_HANDLE;

        std::unique_ptr<RHI::VulkanBuffer> m_GlobalUBO;

        RenderGraph m_RenderGraph;
        GeometryStorage& m_GeometryStorage;

        std::vector<std::unique_ptr<RHI::VulkanImage>> m_DepthImages;

        struct PendingPick
        {
            bool Pending = false;
            uint32_t X = 0;
            uint32_t Y = 0;
            uint32_t RequestFrame = 0;
        };

        PendingPick m_PendingPick;
        std::vector<std::unique_ptr<RHI::VulkanBuffer>> m_PickReadbackBuffers;
        std::vector<bool> m_FrameHasPendingReadback;
        int32_t m_PickResultReadyFrame = -1;
        PickResultGpu m_LastPickResult{};
        bool m_HasPendingConsumedResult = false;
        PickResultGpu m_PendingConsumedResult{};

        DebugViewState m_DebugView{};

        // Cached frame lists for UI and debug resolve selection.
        std::vector<RenderGraphDebugPass> m_LastDebugPasses;
        std::vector<RenderGraphDebugImage> m_LastDebugImages;

        // Features (owned)
        std::unique_ptr<Passes::PickingPass> m_PickingPass;
        std::unique_ptr<Passes::ForwardPass> m_ForwardPass;
        std::unique_ptr<Passes::DebugViewPass> m_DebugViewPass;
        std::unique_ptr<Passes::ImGuiPass> m_ImGuiPass;

        // NOTE: Descriptor allocator reference is needed for DebugView per-frame sets
        RHI::DescriptorAllocator& m_DescriptorPool;
        std::unique_ptr<ShaderLibrary> m_ShaderLibrary;
    };
}
