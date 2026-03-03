module;
#include <array>
#include <memory>
#include <span>
#include <string_view>
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

        // 2. Pipelines & RenderSystem
        InitPipeline(swapchain, renderer, bindless, descriptorPool, descriptorLayout);

        // 3. Retained-mode GPUScene
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
        struct ShaderRegistration
        {
            Core::Hash::StringID Id;
            std::string_view Path;
        };

        constexpr std::array kShaderRegistrations = {
            ShaderRegistration{"Surface.Vert"_id, "shaders/surface.vert.spv"},
            ShaderRegistration{"Surface.Frag"_id, "shaders/surface.frag.spv"},
            ShaderRegistration{"Picking.Vert"_id, "shaders/pick_id.vert.spv"},
            ShaderRegistration{"Picking.Frag"_id, "shaders/pick_id.frag.spv"},
            ShaderRegistration{"Debug.Vert"_id, "shaders/debug_view.vert.spv"},
            ShaderRegistration{"Debug.Frag"_id, "shaders/debug_view.frag.spv"},
            ShaderRegistration{"Debug.Comp"_id, "shaders/debug_view.comp.spv"},
            ShaderRegistration{"Outline.Vert"_id, "shaders/debug_view.vert.spv"}, // Reuse fullscreen triangle
            ShaderRegistration{"Outline.Frag"_id, "shaders/selection_outline.frag.spv"},
            ShaderRegistration{"Line.Vert"_id, "shaders/line.vert.spv"},
            ShaderRegistration{"Line.Frag"_id, "shaders/line.frag.spv"},
            ShaderRegistration{"PointCloud.Vert"_id, "shaders/point.vert.spv"},
            ShaderRegistration{"PointCloud.Frag"_id, "shaders/point.frag.spv"},
            ShaderRegistration{"Cull.Comp"_id, "shaders/instance_cull_multigeo.comp.spv"},
            ShaderRegistration{"SceneUpdate.Comp"_id, "shaders/scene_update.comp.spv"},
            ShaderRegistration{"RetainedPoint.Vert"_id, "shaders/point_retained.vert.spv"},
            ShaderRegistration{"RetainedPoint.Frag"_id, "shaders/point_retained.frag.spv"},
            ShaderRegistration{"Point.FlatDisc.Vert"_id, "shaders/point_flatdisc.vert.spv"},
            ShaderRegistration{"Point.FlatDisc.Frag"_id, "shaders/point_flatdisc.frag.spv"},
            ShaderRegistration{"Point.Surfel.Vert"_id, "shaders/point_surfel.vert.spv"},
            ShaderRegistration{"Point.Surfel.Frag"_id, "shaders/point_surfel.frag.spv"},
        };

        for (const auto& registration : kShaderRegistrations)
            m_ShaderRegistry.Register(registration.Id, registration.Path.data());

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

        // Wire DebugDraw accumulator to RenderSystem (consumed by LinePass).
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

    std::pair<Geometry::GeometryHandle, RHI::TransferToken>
    RenderOrchestrator::CreateGeometryView(RHI::TransferManager& transferManager,
                                           Geometry::GeometryHandle reuseVertexBuffersFrom,
                                           std::span<const uint32_t> indices,
                                           Graphics::PrimitiveTopology topology,
                                           Graphics::GeometryUploadMode uploadMode)
    {
        if (!reuseVertexBuffersFrom.IsValid())
        {
            Core::Log::Error("RenderOrchestrator::CreateGeometryView: invalid reuse handle.");
            return { {}, {} };
        }

        Graphics::GeometryUploadRequest req{};
        req.ReuseVertexBuffersFrom = reuseVertexBuffersFrom;
        req.Indices = indices;
        req.Topology = topology;
        req.UploadMode = uploadMode;

        auto [gpuData, token] = Graphics::GeometryGpuData::CreateAsync(m_Device, transferManager, req, &m_GeometryStorage);
        if (!gpuData)
        {
            Core::Log::Error("RenderOrchestrator::CreateGeometryView: GPU data creation failed.");
            return { {}, {} };
        }

        const auto h = m_GeometryStorage.Add(std::move(gpuData));
        return { h, token };
    }
}
