module;
#include <cstdint>
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
import RHI.Profiler;
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
import Asset.Manager;
import ECS;

export namespace Graphics
{
    enum class GlobalRenderModeOverride : uint8_t
    {
        None = 0,          // Per-entity visibility only (default behavior)
        Shaded,            // Surface pass only
        Wireframe,         // Line pass only
        WireframeShaded,   // Surface + line passes
        Points,            // Point pass only
        Flat,              // Flat shading intent (currently maps to shaded path)
    };

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
                        const BuildGraphInput& input);
        void ExecuteGraph();
        void EndFrame();

        void OnResize();

        // Rebind per-frame allocators to a FrameContext-owned backing store.
        void RebindFrameAllocators(Core::Memory::LinearArena& arena,
                                   Core::Memory::ScopeStack& scope);

        [[nodiscard]] RHI::VulkanBuffer* GetGlobalUBO() const;

        // Retained-mode scene is owned by Runtime::Engine. RenderDriver consumes it during rendering.
        void SetGpuScene(GPUScene* scene);


        // Accessors
        [[nodiscard]] InteractionSystem& GetInteraction();
        [[nodiscard]] const InteractionSystem& GetInteraction() const;
        [[nodiscard]] GlobalResources& GetGlobalResources();

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

        // Mutable scene lighting state.  UI panels modify this directly;
        // RenderOrchestrator::ExtractRenderWorld() copies the current value
        // into RenderWorld::Lighting each frame.
        [[nodiscard]] LightEnvironmentPacket& GetLightEnvironment();
        [[nodiscard]] const LightEnvironmentPacket& GetLightEnvironment() const;
        void SetGlobalRenderModeOverride(GlobalRenderModeOverride mode);
        [[nodiscard]] GlobalRenderModeOverride GetGlobalRenderModeOverride() const;

        // Consume the last resolved GPU profiling result (moves ownership to the caller).
        // Returns std::nullopt if no resolved result is available yet.
        [[nodiscard]] std::optional<RHI::GpuTimestampFrame> ConsumeResolvedGpuProfile();

        // Dump the last compiled render graph to a human-readable string.
        // Returns pass execution order, resource lifetimes, and dependency info.
        [[nodiscard]] std::string DumpRenderGraphToString() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    [[nodiscard]] RenderGraphValidationResult ValidateCompiledGraph(
        const FrameRecipe& recipe,
        std::span<const RenderGraphDebugPass> passes,
        std::span<const RenderGraphDebugImage> images,
        std::span<const ImportedResourceWritePolicy> writePolicies = {});
}
