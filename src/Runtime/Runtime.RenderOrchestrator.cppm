module;
#include <memory>
#include <cstddef>

#include "RHI.Vulkan.hpp"

export module Runtime.RenderOrchestrator;

import Core;
import RHI;
import Graphics;
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

        // --- Per-frame maintenance ---
        void OnResize();

        // Reset per-frame allocators (call at the start of each frame).
        void ResetFrameState();

    private:
        // Per-frame scratch memory.
        Core::Memory::LinearArena m_FrameArena;
        Core::Memory::ScopeStack m_FrameScope;
        Core::FrameGraph m_FrameGraph;

        // Geometry pool (mesh data storage).
        Graphics::GeometryPool m_GeometryStorage;

        // Shader path registry (populated during init, read-only afterwards).
        Graphics::ShaderRegistry m_ShaderRegistry;

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

        void InitPipeline(RHI::VulkanSwapchain& swapchain,
                          RHI::SimpleRenderer& renderer,
                          RHI::BindlessDescriptorSystem& bindless,
                          RHI::DescriptorAllocator& descriptorPool,
                          RHI::DescriptorLayout& descriptorLayout);
    };
}
