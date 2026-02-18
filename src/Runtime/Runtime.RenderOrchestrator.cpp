module;
#include <memory>
#include "RHI.Vulkan.hpp"

module Runtime.RenderOrchestrator;

import Core.Logging;
import Core.Hash;
import Core.Memory;
import Core.FrameGraph;
import Core.Assets;
import Core.FeatureRegistry;
import RHI;
import Graphics;

using namespace Core::Hash;

namespace Runtime
{
    RenderOrchestrator::RenderOrchestrator(
        std::shared_ptr<RHI::VulkanDevice> device,
        RHI::VulkanSwapchain& swapchain,
        RHI::SimpleRenderer& renderer,
        RHI::BindlessDescriptorSystem& bindless,
        RHI::DescriptorAllocator& descriptorPool,
        RHI::DescriptorLayout& descriptorLayout,
        RHI::TextureSystem& textureSystem,
        Core::Assets::AssetManager& assetManager,
        uint32_t defaultTextureIndex, //TODO: Why is this not used? can we remove it or was this an error to not use it?
        Core::FeatureRegistry* featureRegistry,
        size_t frameArenaSize)
        : m_FrameArena(frameArenaSize)
        , m_FrameScope(frameArenaSize)
        , m_FrameGraph(m_FrameScope)
        , m_Device(std::move(device))
        , m_Swapchain(swapchain)
        , m_Bindless(bindless)
        , m_DescriptorLayout(descriptorLayout)
        , m_FeatureRegistry(featureRegistry)
    {
        Core::Log::Info("RenderOrchestrator: Initializing...");

        // 1. MaterialSystem (depends on TextureSystem + AssetManager)
        m_MaterialSystem = std::make_unique<Graphics::MaterialSystem>(textureSystem, assetManager);

        // 2. Initialize GeometryStorage with frames-in-flight for safe deferred deletion
        m_GeometryStorage.Initialize(m_Device->GetFramesInFlight());

        // 3. Pipelines & RenderSystem
        InitPipeline(swapchain, renderer, bindless, descriptorPool, descriptorLayout);

        // 4. Retained-mode GPUScene
        if (m_PipelineLibrary && m_Device)
        {
            if (auto* p = m_PipelineLibrary->GetSceneUpdatePipeline();
                p && m_PipelineLibrary->GetSceneUpdateSetLayout() != VK_NULL_HANDLE)
            {
                m_GpuScene = std::make_unique<Graphics::GPUScene>(
                    *m_Device, *p, m_PipelineLibrary->GetSceneUpdateSetLayout());

                if (m_RenderSystem)
                    m_RenderSystem->SetGpuScene(m_GpuScene.get());
            }
        }

        Core::Log::Info("RenderOrchestrator: Initialization complete.");
    }

    RenderOrchestrator::~RenderOrchestrator()
    {
        // Destroy GPU systems in reverse dependency order.
        m_GpuScene.reset();
        m_RenderSystem.reset();
        m_PipelineLibrary.reset();
        m_MaterialSystem.reset();

        // Clear geometry storage before device destruction.
        m_GeometryStorage.Clear();

        // Destroy per-frame transient RHI objects while VulkanDevice is still alive.
        m_FrameScope.Reset();

        Core::Log::Info("RenderOrchestrator: Shutdown complete.");
    }

    void RenderOrchestrator::InitPipeline(
        RHI::VulkanSwapchain& swapchain,
        RHI::SimpleRenderer& renderer,
        RHI::BindlessDescriptorSystem& bindless,
        RHI::DescriptorAllocator& descriptorPool,
        RHI::DescriptorLayout& descriptorLayout)
    {
        // Shader policy (data-driven)
        m_ShaderRegistry.Register("Forward.Vert"_id, "shaders/triangle.vert.spv");
        m_ShaderRegistry.Register("Forward.Frag"_id, "shaders/triangle.frag.spv");
        m_ShaderRegistry.Register("Picking.Vert"_id, "shaders/pick_id.vert.spv");
        m_ShaderRegistry.Register("Picking.Frag"_id, "shaders/pick_id.frag.spv");
        m_ShaderRegistry.Register("Debug.Vert"_id, "shaders/debug_view.vert.spv");
        m_ShaderRegistry.Register("Debug.Frag"_id, "shaders/debug_view.frag.spv");
        m_ShaderRegistry.Register("Debug.Comp"_id, "shaders/debug_view.comp.spv");
        m_ShaderRegistry.Register("Outline.Vert"_id, "shaders/debug_view.vert.spv"); // Reuse fullscreen triangle
        m_ShaderRegistry.Register("Outline.Frag"_id, "shaders/selection_outline.frag.spv");

        // Line rendering (debug draw)
        m_ShaderRegistry.Register("Line.Vert"_id, "shaders/line.vert.spv");
        m_ShaderRegistry.Register("Line.Frag"_id, "shaders/line.frag.spv");

        // Point cloud rendering (billboard/surfel/EWA splatting)
        m_ShaderRegistry.Register("PointCloud.Vert"_id, "shaders/point.vert.spv");
        m_ShaderRegistry.Register("PointCloud.Frag"_id, "shaders/point.frag.spv");

        // Stage 3 compute
        m_ShaderRegistry.Register("Cull.Comp"_id, "shaders/instance_cull_multigeo.comp.spv");

        // GPUScene scatter update
        m_ShaderRegistry.Register("SceneUpdate.Comp"_id, "shaders/scene_update.comp.spv");

        // Pipeline library (owns PSOs)
        m_PipelineLibrary = std::make_unique<Graphics::PipelineLibrary>(
            m_Device, bindless, descriptorLayout);
        m_PipelineLibrary->BuildDefaults(m_ShaderRegistry,
                                         swapchain.GetImageFormat(),
                                         RHI::VulkanImage::FindDepthFormat(*m_Device));

        // RenderSystem (borrows PSOs via PipelineLibrary)
        Core::Log::Info("RenderOrchestrator: Creating RenderSystem...");
        Graphics::RenderSystemConfig rsConfig{};
        m_RenderSystem = std::make_unique<Graphics::RenderSystem>(
            rsConfig,
            m_Device,
            swapchain,
            renderer,
            bindless,
            descriptorPool,
            descriptorLayout,
            *m_PipelineLibrary,
            m_ShaderRegistry,
            m_FrameArena,
            m_FrameScope,
            m_GeometryStorage,
            *m_MaterialSystem
        );
        Core::Log::Info("RenderOrchestrator: RenderSystem created successfully.");

        if (!m_RenderSystem)
        {
            Core::Log::Error("RenderOrchestrator: Failed to create RenderSystem!");
            std::exit(1);
        }

        // Wire DebugDraw accumulator to RenderSystem (consumed by LineRenderPass).
        m_RenderSystem->SetDebugDraw(&m_DebugDraw);

        // Default Render Pipeline (hot-swappable)
        auto defaultPipeline = std::make_unique<Graphics::DefaultPipeline>();
        defaultPipeline->SetFeatureRegistry(m_FeatureRegistry);
        m_RenderSystem->RequestPipelineSwap(std::move(defaultPipeline));
    }

    void RenderOrchestrator::OnResize()
    {
        if (m_RenderSystem)
            m_RenderSystem->OnResize();
    }

    void RenderOrchestrator::ResetFrameState()
    {
        m_FrameScope.Reset();
        m_FrameArena.Reset();
        m_DebugDraw.Reset();
    }
}
