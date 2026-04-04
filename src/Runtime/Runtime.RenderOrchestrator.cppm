module;
#include <memory>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

export module Runtime.RenderOrchestrator;

import Core.Assets;
import Core.FeatureRegistry;
import Core.FrameGraph;
import Core.Memory;
import RHI.Bindless;
import RHI.Descriptors;
import RHI.Device;
import RHI.Renderer;
import RHI.Swapchain;
import RHI.TextureManager;
import RHI.Transfer;
import Graphics.DebugDraw;
import Graphics.Geometry;
import Graphics.GPUScene;
import Graphics.Material;
import Graphics.PipelineLibrary;
import Graphics.RenderDriver;
import Graphics.RenderPipeline;
import Graphics.ShaderHotReload;
import Graphics.ShaderRegistry;
import Geometry.Handle;
import Runtime.RenderExtraction;

export namespace Runtime
{
    // Owns the render subsystem: ShaderRegistry, PipelineLibrary, GPUScene,
    // RenderDriver, MaterialRegistry, per-frame arena/scope/FrameGraph, and
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
                           RHI::TextureManager& textureManager,
                           Core::Assets::AssetManager& assetManager,
                           Core::FeatureRegistry* featureRegistry = nullptr,
                           size_t frameArenaSize = 1024 * 1024,
                           uint32_t frameContextCount = DefaultFrameContexts);
        ~RenderOrchestrator();

        // Non-copyable, non-movable (owns GPU resources and frame state).
        RenderOrchestrator(const RenderOrchestrator&) = delete;
        RenderOrchestrator& operator=(const RenderOrchestrator&) = delete;
        RenderOrchestrator(RenderOrchestrator&&) = delete;
        RenderOrchestrator& operator=(RenderOrchestrator&&) = delete;

        // --- Accessors ---
        [[nodiscard]] Graphics::RenderDriver& GetRenderDriver();
        [[nodiscard]] const Graphics::RenderDriver& GetRenderDriver() const;

        [[nodiscard]] Graphics::GPUScene& GetGPUScene();
        [[nodiscard]] const Graphics::GPUScene& GetGPUScene() const;
        [[nodiscard]] Graphics::GPUScene* GetGPUScenePtr() const;

        [[nodiscard]] Graphics::PipelineLibrary& GetPipelineLibrary();
        [[nodiscard]] const Graphics::PipelineLibrary& GetPipelineLibrary() const;

        [[nodiscard]] Graphics::MaterialRegistry& GetMaterialRegistry();
        [[nodiscard]] const Graphics::MaterialRegistry& GetMaterialRegistry() const;

        [[nodiscard]] Graphics::ShaderRegistry& GetShaderRegistry();
        [[nodiscard]] const Graphics::ShaderRegistry& GetShaderRegistry() const;

        [[nodiscard]] Core::FrameGraph& GetFrameGraph();
        [[nodiscard]] const Core::FrameGraph& GetFrameGraph() const;

        [[nodiscard]] Graphics::GeometryPool& GetGeometryStorage();
        [[nodiscard]] const Graphics::GeometryPool& GetGeometryStorage() const;

        [[nodiscard]] Core::Memory::LinearArena& GetFrameArena();
        [[nodiscard]] Core::Memory::ScopeStack& GetFrameScope();

        [[nodiscard]] Graphics::DebugDraw& GetDebugDraw();
        [[nodiscard]] const Graphics::DebugDraw& GetDebugDraw() const;
        [[nodiscard]] uint32_t GetFrameContextCount() const;

        // Shader hot-reload service (may be null if not initialized).
        [[nodiscard]] Graphics::ShaderHotReloadService* GetShaderHotReload();
        [[nodiscard]] const Graphics::ShaderHotReloadService* GetShaderHotReload() const;

        // Create and start the shader hot-reload service.
        // Call after construction, when ShaderRegistry is fully populated.
        void InitShaderHotReload();

        // --- Per-frame maintenance ---
        void OnResize();

        // Reset per-frame allocators (call at the start of each frame).
        void ResetFrameState();

        // --- Staged frame-pipeline seam ---

        // Runs GUI::BeginFrame() + GUI::DrawGUI() so ImGui draw data is
        // finalized before extraction.  Returns an immutable overlay packet.
        [[nodiscard]] Graphics::EditorOverlayPacket PrepareEditorOverlay() const;

        [[nodiscard]] FrameContext& BeginFrame() const;
        [[nodiscard]] RenderWorld ExtractRenderWorld(const RenderFrameInput& input) const;
        void PrepareFrame(FrameContext& frame, RenderWorld renderWorld);
        void ExecuteFrame(FrameContext& frame);
        void EndFrame(FrameContext& frame);

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
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
