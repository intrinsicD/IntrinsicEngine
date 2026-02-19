module;
#include <memory>
#include <cstddef>
#include <span>

#include "RHI.Vulkan.hpp"

export module Runtime.RenderOrchestrator;

import Core.FrameGraph;
import Core.Memory;
import Core.Assets;
import Core.Hash;
import Core.FeatureRegistry;
import RHI;
import Graphics;
import Geometry;
import ECS;

export namespace Runtime
{
    // Owns the render subsystem: ShaderRegistry, PipelineLibrary, GPUScene,
    // RenderSystem, MaterialSystem, per-frame arena/scope/FrameGraph, and
    // GeometryPool.  Extracted from Engine following the GraphicsBackend /
    // AssetPipeline / SceneManager pattern.
    class RenderOrchestrator
    {
    public:
        // Construct the full render subsystem.  Requires borrowed references
        // to GraphicsBackend-owned infrastructure and the AssetManager.
        RenderOrchestrator(std::shared_ptr<RHI::VulkanDevice> device,
                           RHI::VulkanSwapchain& swapchain,
                           RHI::SimpleRenderer& renderer,
                           RHI::BindlessDescriptorSystem& bindless,
                           RHI::DescriptorAllocator& descriptorPool,
                           RHI::DescriptorLayout& descriptorLayout,
                           RHI::TextureSystem& textureSystem,
                           Core::Assets::AssetManager& assetManager,
                           uint32_t defaultTextureIndex,
                           Core::FeatureRegistry* featureRegistry = nullptr,
                           size_t frameArenaSize = 1024 * 1024);
        ~RenderOrchestrator();

        // Non-copyable, non-movable (owns GPU resources and frame state).
        RenderOrchestrator(const RenderOrchestrator&) = delete;
        RenderOrchestrator& operator=(const RenderOrchestrator&) = delete;
        RenderOrchestrator(RenderOrchestrator&&) = delete;
        RenderOrchestrator& operator=(RenderOrchestrator&&) = delete;

        // --- Accessors ---
        [[nodiscard]] Graphics::RenderSystem& GetRenderSystem() { return *m_RenderSystem; }
        [[nodiscard]] const Graphics::RenderSystem& GetRenderSystem() const { return *m_RenderSystem; }

        [[nodiscard]] Graphics::GPUScene& GetGPUScene() { return *m_GpuScene; }
        [[nodiscard]] const Graphics::GPUScene& GetGPUScene() const { return *m_GpuScene; }
        [[nodiscard]] Graphics::GPUScene* GetGPUScenePtr() const { return m_GpuScene.get(); }

        [[nodiscard]] Graphics::PipelineLibrary& GetPipelineLibrary() { return *m_PipelineLibrary; }
        [[nodiscard]] const Graphics::PipelineLibrary& GetPipelineLibrary() const { return *m_PipelineLibrary; }

        [[nodiscard]] Graphics::MaterialSystem& GetMaterialSystem() { return *m_MaterialSystem; }
        [[nodiscard]] const Graphics::MaterialSystem& GetMaterialSystem() const { return *m_MaterialSystem; }

        [[nodiscard]] Graphics::ShaderRegistry& GetShaderRegistry() { return m_ShaderRegistry; }
        [[nodiscard]] const Graphics::ShaderRegistry& GetShaderRegistry() const { return m_ShaderRegistry; }

        [[nodiscard]] Core::FrameGraph& GetFrameGraph() { return m_FrameGraph; }
        [[nodiscard]] const Core::FrameGraph& GetFrameGraph() const { return m_FrameGraph; }

        [[nodiscard]] Graphics::GeometryPool& GetGeometryStorage() { return m_GeometryStorage; }
        [[nodiscard]] const Graphics::GeometryPool& GetGeometryStorage() const { return m_GeometryStorage; }

        [[nodiscard]] Core::Memory::LinearArena& GetFrameArena() { return m_FrameArena; }
        [[nodiscard]] Core::Memory::ScopeStack& GetFrameScope() { return m_FrameScope; }

        [[nodiscard]] Graphics::DebugDraw& GetDebugDraw() { return m_DebugDraw; }
        [[nodiscard]] const Graphics::DebugDraw& GetDebugDraw() const { return m_DebugDraw; }

        // --- Per-frame maintenance ---
        void OnResize();

        // Reset per-frame allocators (call at the start of each frame).
        void ResetFrameState();

        // -----------------------------------------------------------------
        // Geometry Views (shared-vertex GPU data)
        // -----------------------------------------------------------------
        // Create a new GeometryGpuData instance that reuses the vertex buffer
        // from an existing geometry handle, but uploads a new index buffer and
        // sets a new topology (Lines/Points/etc).
        // Returns {newHandle, transferToken}.
        [[nodiscard]] std::pair<Geometry::GeometryHandle, RHI::TransferToken>
        CreateGeometryView(RHI::TransferManager& transferManager,
                           Geometry::GeometryHandle reuseVertexBuffersFrom,
                           std::span<const uint32_t> indices,
                           Graphics::PrimitiveTopology topology,
                           Graphics::GeometryUploadMode uploadMode = Graphics::GeometryUploadMode::Staged);

    private:
        // Per-frame scratch memory.
        Core::Memory::LinearArena m_FrameArena;
        Core::Memory::ScopeStack m_FrameScope;
        Core::FrameGraph m_FrameGraph;

        // Geometry pool (mesh data storage).
        Graphics::GeometryPool m_GeometryStorage;

        // Shader path registry (populated during init, read-only afterwards).
        Graphics::ShaderRegistry m_ShaderRegistry;

        // Immediate-mode debug drawing accumulator (reset each frame).
        Graphics::DebugDraw m_DebugDraw;

        // Pipeline state objects.
        std::unique_ptr<Graphics::PipelineLibrary> m_PipelineLibrary;

        // Material system (texture binding, hot-reload).
        std::unique_ptr<Graphics::MaterialSystem> m_MaterialSystem;

        // Retained-mode GPU scene (persistent SSBOs, slot allocator).
        std::unique_ptr<Graphics::GPUScene> m_GpuScene;

        // Full render system (owns RenderGraph, presentation, interaction).
        std::unique_ptr<Graphics::RenderSystem> m_RenderSystem;

        // Borrowed references to GraphicsBackend infrastructure.
        std::shared_ptr<RHI::VulkanDevice> m_Device;
        RHI::VulkanSwapchain& m_Swapchain;
        RHI::BindlessDescriptorSystem& m_Bindless;
        RHI::DescriptorLayout& m_DescriptorLayout;

        // Borrowed reference to the engine-wide feature registry (nullable).
        Core::FeatureRegistry* m_FeatureRegistry = nullptr;

        void InitPipeline(RHI::VulkanSwapchain& swapchain,
                          RHI::SimpleRenderer& renderer,
                          RHI::BindlessDescriptorSystem& bindless,
                          RHI::DescriptorAllocator& descriptorPool,
                          RHI::DescriptorLayout& descriptorLayout);
    };
}
