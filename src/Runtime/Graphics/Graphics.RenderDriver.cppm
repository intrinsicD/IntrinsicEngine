module;
#include <glm/glm.hpp>
#include <memory>
#include <span>
#include <vector>
#include <optional>

#include "RHI.Vulkan.hpp"

export module Graphics.RenderDriver;

import RHI.Bindless;
import RHI.Buffer;
import RHI.Descriptors;
import RHI.Device;
import RHI.Renderer;
import RHI.Swapchain;
import Graphics.Camera;
import Graphics.Geometry;
import Graphics.RenderGraph;
import Graphics.RenderPipeline;
import Graphics.ShaderRegistry;
import Graphics.PipelineLibrary;
import Graphics.GPUScene;
import Graphics.MaterialRegistry;
import Graphics.DebugDraw;
import Graphics.Interaction; // New: Interaction Logic
import Graphics.Presentation; // New: Presentation Logic
import Graphics.GlobalResources; // New: Global State
import Graphics.Passes.SelectionOutlineSettings;
import Graphics.Passes.PostProcessSettings;
import Core.Memory;
import Core.Assets;
import ECS;

export namespace Graphics
{
    struct RenderDriverConfig
    {
        bool EnableRenderAuditLogging = false;
        // Future: MSAA settings, Shadow resolution, etc.
    };

    class RenderDriver
    {
    public:
        RenderDriver(const RenderDriverConfig& config,
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
                     MaterialRegistry& materialRegistry);
        ~RenderDriver();

        // Hot-swap: schedules activation at the start of the next successfully-begun frame.
        void RequestPipelineSwap(std::unique_ptr<RenderPipeline> pipeline);

        void BeginFrame(uint64_t currentFrame);
        [[nodiscard]] bool AcquireFrame();
        void ProcessCompletedGpuWork(ECS::Scene& scene, uint64_t currentFrame);
        void UpdateGlobals(const CameraComponent& camera, const LightEnvironmentPacket& lighting);
        void BuildGraph(Core::Assets::AssetManager& assetManager,
                        const CameraComponent& camera,
                        const LightEnvironmentPacket& lighting = {},
                        bool hasSelectionWork = false,
                        const SelectionOutlinePacket& selectionOutline = {},
                        std::span<const PickingSurfacePacket> pickingSurfacePackets = {},
                        std::span<const PickingLinePacket> pickingLinePackets = {},
                        std::span<const PickingPointPacket> pickingPointPackets = {},
                        std::span<const SurfaceDrawPacket> surfaceDrawPackets = {},
                        std::span<const LineDrawPacket> lineDrawPackets = {},
                        std::span<const PointDrawPacket> pointDrawPackets = {},
                        const HtexPatchPreviewPacket* htexPatchPreview = nullptr,
                        std::span<const DebugDraw::LineSegment> debugDrawLines = {},
                        std::span<const DebugDraw::LineSegment> debugDrawOverlayLines = {},
                        std::span<const DebugDraw::PointMarker> debugDrawPoints = {},
                        std::span<const DebugDraw::TriangleVertex> debugDrawTriangles = {},
                        const EditorOverlayPacket& editorOverlay = {});
        void ExecuteGraph();
        void EndFrame();

        void OnResize();

        [[nodiscard]] RHI::VulkanBuffer* GetGlobalUBO() const { return m_GlobalResources.GetCameraUBO(); }

        // Retained-mode scene is owned by Runtime::Engine. RenderDriver consumes it during rendering.
        void SetGpuScene(GPUScene* scene) { m_GpuScene = scene; }


        // Accessors
        [[nodiscard]] InteractionSystem& GetInteraction() { return m_Interaction; }
        [[nodiscard]] const InteractionSystem& GetInteraction() const { return m_Interaction; }
        [[nodiscard]] GlobalResources& GetGlobalResources() { return m_GlobalResources; }

        // Picking API Facade (Delegates to InteractionSystem)
        using PickResultGpu = InteractionSystem::PickResultGpu;
        void RequestPick(uint32_t x, uint32_t y);
        [[nodiscard]] PickResultGpu GetLastPickResult() const;

        // Access selection outline settings (returns nullptr if not available)
        [[nodiscard]] Passes::SelectionOutlineSettings* GetSelectionOutlineSettings();

        // Access post-process settings (returns nullptr if not available)
        [[nodiscard]] Passes::PostProcessSettings* GetPostProcessSettings();

        // Access histogram readback data (returns nullptr if not available)
        [[nodiscard]] const Passes::HistogramReadback* GetHistogramReadback() const;

        // Dump the last compiled render graph to a human-readable string.
        // Returns pass execution order, resource lifetimes, and dependency info.
        [[nodiscard]] std::string DumpRenderGraphToString() const;

    private:
        RenderDriverConfig m_Config;

        // Ownership stays with the caller, but we avoid ref-count ops in hot code.
        std::shared_ptr<RHI::VulkanDevice> m_DeviceOwner;
        RHI::VulkanDevice* m_Device = nullptr;

        RHI::VulkanSwapchain& m_Swapchain;
        RHI::SimpleRenderer& m_Renderer;

        // Per-frame scratch allocators provided by Runtime::Engine.
        // - LinearArena: POD pass data
        // - ScopeStack : destructor-safe closures + frame-stable context snapshots
        Core::Memory::ScopeStack& m_FrameScope;

        // Sub-Systems
        GlobalResources m_GlobalResources; // Holds UBOs, Descriptors, Allocators
        PresentationSystem m_Presentation;
        InteractionSystem m_Interaction;

        RenderGraph m_RenderGraph;
        GeometryPool& m_GeometryStorage;
        MaterialRegistry& m_MaterialRegistry;

        // Retained-mode GPU scene (persistent SSBOs + sparse updates). Non-owning.
        GPUScene* m_GpuScene = nullptr;

        // Cached frame lists for UI and debug resolve selection.
        std::vector<RenderGraphDebugPass> m_LastDebugPasses;
        std::vector<RenderGraphDebugImage> m_LastDebugImages;
        FrameRecipe m_LastFrameRecipe{};
        uint32_t m_ResizeCount = 0;
        VkExtent2D m_LastResizeExtent{};
        uint64_t m_LastResizeGlobalFrame = 0;
        VkExtent2D m_LastBuiltGraphExtent{};
        uint32_t m_LastBuiltFrameIndex = 0;
        uint32_t m_LastBuiltImageIndex = 0;

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

    [[nodiscard]] RenderGraphValidationResult ValidateCompiledGraph(
        const FrameRecipe& recipe,
        std::span<const RenderGraphDebugPass> passes,
        std::span<const RenderGraphDebugImage> images,
        std::span<const ImportedResourceWritePolicy> writePolicies = {});
}
