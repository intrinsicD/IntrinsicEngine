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

        void OnUpdate(ECS::Scene& scene, const CameraComponent& camera, Core::Assets::AssetManager &assetManager);

        // Called after swapchain recreation.
        // Clears transient caches and recreates per-frame resources as needed.
        void OnResize();

        // -----------------------------------------------------------------
        // GPU picking (pixel-perfect ID buffer)
        // -----------------------------------------------------------------
        // Schedule a pick readback for the given framebuffer pixel.
        // Result becomes available a few frames later (FramesInFlight).
        void RequestPick(uint32_t x, uint32_t y);

        struct PickResultGpu
        {
            bool HasHit = false;
            uint32_t EntityID = 0;
        };

        // Non-blocking: returns the most recent resolved pick result.
        [[nodiscard]] PickResultGpu GetLastPickResult() const;

        // Pop the next completed pick result (one-shot). Returns std::nullopt if not ready yet.
        [[nodiscard]] std::optional<PickResultGpu> TryConsumePickResult();

        [[nodiscard]] RHI::VulkanBuffer* GetGlobalUBO() const { return m_GlobalUBO.get(); }

        // -----------------------------------------------------------------
        // Render-target viewer (debug)
        // -----------------------------------------------------------------
        struct DebugViewState
        {
            bool Enabled = false; // Disabled by default; enable via UI
            Core::Hash::StringID SelectedResource = Core::Hash::StringID("PickID");
            ResourceID SelectedResourceId = kInvalidResource;        // resolved from per-pass attachment selection
            bool ShowInViewport = false; // if true, replace the main view with the debug view

            // Camera params for depth visualization (heuristic defaults)
            float DepthNear = 0.1f;
            float DepthFar = 1000.0f;
        };

        [[nodiscard]] const DebugViewState& GetDebugViewState() const { return m_DebugView; }
        void SetDebugViewSelectedResource(Core::Hash::StringID name) { m_DebugView.SelectedResource = name; }
        void SetDebugViewShowInViewport(bool show) { m_DebugView.ShowInViewport = show; }

    private:
        size_t m_MinUboAlignment = 0;

        std::shared_ptr<RHI::VulkanDevice> m_Device;
        RHI::VulkanSwapchain& m_Swapchain;
        RHI::SimpleRenderer& m_Renderer;
        RHI::BindlessDescriptorSystem& m_BindlessSystem;
        RHI::GraphicsPipeline& m_Pipeline;
        RHI::GraphicsPipeline& m_PickPipeline;
        VkDescriptorSet m_GlobalDescriptorSet = VK_NULL_HANDLE;

        // The Global Camera UBO
        std::unique_ptr<RHI::VulkanBuffer> m_GlobalUBO;

        // RenderGraph
        RenderGraph m_RenderGraph;
        GeometryStorage& m_GeometryStorage;

        std::vector<std::unique_ptr<RHI::VulkanImage>> m_DepthImages;

        // --- GPU picking state ---
        struct PendingPick
        {
            bool Pending = false;
            uint32_t X = 0;
            uint32_t Y = 0;
            uint32_t RequestFrame = 0; // Frame index when pick was requested
        };

        PendingPick m_PendingPick;

        // One readback buffer per frame-in-flight.
        std::vector<std::unique_ptr<RHI::VulkanBuffer>> m_PickReadbackBuffers;

        // Track which frame's pick result is ready to be consumed.
        // -1 means no pending result.
        int32_t m_PickResultReadyFrame = -1;

        // Last resolved result (from an older frame, no GPU stall).
        PickResultGpu m_LastPickResult{};

        // Next result to be consumed by the app.
        bool m_HasPendingConsumedResult = false;
        PickResultGpu m_PendingConsumedResult{};

        DebugViewState m_DebugView{};

        // Debug-view resources (per-frame)
        std::vector<std::unique_ptr<RHI::VulkanImage>> m_DebugViewImages;
        VkSampler m_DebugViewSampler = VK_NULL_HANDLE;
        void* m_DebugViewImGuiTexId = nullptr; // ImGui texture for current debug view image

        // Debug-view pipeline resources
        VkDescriptorSetLayout m_DebugViewSetLayout = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_DebugViewSets; // One per frame-in-flight
        std::unique_ptr<RHI::GraphicsPipeline> m_DebugViewPipeline;

        // Dummy 1x1 textures to initialize all descriptor bindings (avoid validation errors)
        std::unique_ptr<RHI::VulkanImage> m_DebugViewDummyFloat;  // R8G8B8A8_UNORM
        std::unique_ptr<RHI::VulkanImage> m_DebugViewDummyUint;   // R32_UINT
        std::unique_ptr<RHI::VulkanImage> m_DebugViewDummyDepth;  // D32_SFLOAT

        // Cached frame lists for UI
        std::vector<RenderGraphDebugPass> m_LastDebugPasses;
        std::vector<RenderGraphDebugImage> m_LastDebugImages;
    };
}
