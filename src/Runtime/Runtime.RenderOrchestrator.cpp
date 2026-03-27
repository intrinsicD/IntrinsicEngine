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
import RHI.Bindless;
import RHI.Descriptors;
import RHI.Device;
import RHI.Image;
import RHI.Renderer;
import RHI.Swapchain;
import RHI.Texture;
import RHI.TextureManager;
import RHI.Transfer;
import Graphics.DebugDraw;
import Graphics.Geometry;
import Graphics.GPUScene;
import Graphics.MaterialRegistry;
import Graphics.PipelineLibrary;
import Graphics.Pipelines;
import Graphics.RenderSystem;
import Graphics.ShaderRegistry;
import Runtime.RenderExtraction;

using namespace Core::Hash;

namespace Runtime
{
    namespace
    {
        void WaitForFrameContextReuseIfNeeded(const std::shared_ptr<RHI::VulkanDevice>& device, const FrameContext& frame)
        {
            if (!device || !frame.ReusedSubmittedSlot || frame.LastSubmittedTimelineValue == 0u)
                return;

            const uint64_t completed = device->GetGraphicsTimelineCompletedValue();
            if (completed >= frame.LastSubmittedTimelineValue)
                return;

            VkSemaphoreWaitInfo waitInfo{};
            waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
            waitInfo.semaphoreCount = 1;
            const VkSemaphore timelineSemaphore = device->GetGraphicsTimelineSemaphore();
            waitInfo.pSemaphores = &timelineSemaphore;
            const uint64_t waitValue = frame.LastSubmittedTimelineValue;
            waitInfo.pValues = &waitValue;

            const VkResult result = vkWaitSemaphores(device->GetLogicalDevice(), &waitInfo, UINT64_MAX);
            if (result != VK_SUCCESS)
            {
                Core::Log::Warn(
                    "RenderOrchestrator::BeginFrame: failed waiting for frame-context slot {} timeline {} reuse (VkResult={}).",
                    frame.SlotIndex,
                    frame.LastSubmittedTimelineValue,
                    static_cast<int32_t>(result));
                return;
            }

            Core::Log::Debug(
                "RenderOrchestrator::BeginFrame: waited for timeline {} before reusing submitted frame-context slot {}.",
                frame.LastSubmittedTimelineValue,
                frame.SlotIndex);
        }
    }

    RenderOrchestrator::RenderOrchestrator(
        std::shared_ptr<RHI::VulkanDevice> device,
        RHI::VulkanSwapchain& swapchain,
        RHI::SimpleRenderer& renderer,
        RHI::BindlessDescriptorSystem& bindless,
        RHI::DescriptorAllocator& descriptorPool,
        RHI::DescriptorLayout& descriptorLayout,
        RHI::TextureManager& textureManager,
        Core::Assets::AssetManager& assetManager,
        Core::FeatureRegistry* featureRegistry,
        size_t frameArenaSize,
        uint32_t frameContextCount)
        : m_FrameArena(frameArenaSize)
        , m_FrameScope(frameArenaSize)
        , m_FrameGraph(m_FrameScope)
        , m_Device(std::move(device))
        , m_Swapchain(swapchain)
        , m_Bindless(bindless)
        , m_DescriptorLayout(descriptorLayout)
        , m_AssetManager(assetManager)
        , m_FeatureRegistry(featureRegistry)
        , m_FrameContextRing(frameContextCount)
    {
        Core::Log::Info("RenderOrchestrator: Initializing...");
        Core::Log::Info("RenderOrchestrator: frame-context ring configured for {} slots.",
                        m_FrameContextRing.GetFramesInFlight());

        // 1. MaterialRegistry (depends on TextureManager + AssetManager)
        m_MaterialRegistry = std::make_unique<Graphics::MaterialRegistry>(textureManager, assetManager);

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
        m_MaterialRegistry.reset();

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
            ShaderRegistration{"Surface.GBuffer.Frag"_id, "shaders/surface_gbuffer.frag.spv"},
            ShaderRegistration{"Deferred.Lighting.Frag"_id, "shaders/deferred_lighting.frag.spv"},
            ShaderRegistration{"Picking.Vert"_id, "shaders/pick_id.vert.spv"},
            ShaderRegistration{"Picking.Frag"_id, "shaders/pick_id.frag.spv"},
            ShaderRegistration{"PickMesh.Vert"_id, "shaders/pick_mesh.vert.spv"},
            ShaderRegistration{"PickMesh.Frag"_id, "shaders/pick_mesh.frag.spv"},
            ShaderRegistration{"PickLine.Vert"_id, "shaders/pick_line.vert.spv"},
            ShaderRegistration{"PickLine.Frag"_id, "shaders/pick_line.frag.spv"},
            ShaderRegistration{"PickPoint.Vert"_id, "shaders/pick_point.vert.spv"},
            ShaderRegistration{"PickPoint.Frag"_id, "shaders/pick_point.frag.spv"},
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
            ShaderRegistration{"Point.Sphere.Vert"_id, "shaders/point_sphere.vert.spv"},
            ShaderRegistration{"Point.Sphere.Frag"_id, "shaders/point_sphere.frag.spv"},
            ShaderRegistration{"Post.Fullscreen.Vert"_id, "shaders/post_fullscreen.vert.spv"},
            ShaderRegistration{"Post.ToneMap.Frag"_id, "shaders/post_tonemap.frag.spv"},
            ShaderRegistration{"Post.FXAA.Frag"_id, "shaders/post_fxaa.frag.spv"},
            ShaderRegistration{"Post.SMAA.Edge.Frag"_id, "shaders/post_smaa_edge.frag.spv"},
            ShaderRegistration{"Post.SMAA.Blend.Frag"_id, "shaders/post_smaa_blend.frag.spv"},
            ShaderRegistration{"Post.SMAA.Resolve.Frag"_id, "shaders/post_smaa_resolve.frag.spv"},
            ShaderRegistration{"Post.BloomDown.Frag"_id, "shaders/post_bloom_downsample.frag.spv"},
            ShaderRegistration{"Post.BloomUp.Frag"_id, "shaders/post_bloom_upsample.frag.spv"},
            ShaderRegistration{"Post.Histogram.Comp"_id, "shaders/post_histogram.comp.spv"},
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
            *m_MaterialRegistry
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

    FrameContext& RenderOrchestrator::BeginFrame() const
    {
        const VkExtent2D extent = m_Swapchain.GetExtent();
        FrameContext& frame = m_FrameContextRing.BeginFrame(
            m_Device ? m_Device->GetGlobalFrameNumber() : 0u,
            RenderViewport{
                .Width = extent.width,
                .Height = extent.height,
            });
        WaitForFrameContextReuseIfNeeded(m_Device, frame);
        return frame;
    }

    RenderWorld RenderOrchestrator::ExtractRenderWorld(const RenderFrameInput& input) const
    {
        return Runtime::ExtractRenderWorld(input);
    }

    void RenderOrchestrator::PrepareFrame(FrameContext& frame, RenderWorld renderWorld)
    {
        frame.PreparedRenderWorld = std::move(renderWorld);
        frame.Prepared = false;
        frame.Submitted = false;
        frame.Viewport = frame.PreparedRenderWorld ? frame.PreparedRenderWorld->View.Viewport : RenderViewport{};

        const RenderWorld* preparedRenderWorld = frame.GetPreparedRenderWorld();
        if (!preparedRenderWorld || !preparedRenderWorld->IsValid())
        {
            Core::Log::Warn("RenderOrchestrator::PrepareFrame skipped: invalid RenderWorld supplied for frame preparation.");
            return;
        }

        if (preparedRenderWorld->World.HasCommitDrift())
        {
            Core::Log::Warn(
                "RenderOrchestrator::PrepareFrame skipped: extracted world snapshot drifted from authoritative state (snapshot tick {}, current tick {}).",
                preparedRenderWorld->World.CommittedTick,
                preparedRenderWorld->World.GetCurrentCommittedTick());
            return;
        }

        const uint64_t currentFrame = m_Device ? m_Device->GetGlobalFrameNumber() : frame.FrameNumber;
        m_RenderSystem->BeginFrame(currentFrame);
        if (!m_RenderSystem->AcquireFrame())
            return;

        m_RenderSystem->UpdateGlobals(preparedRenderWorld->View.Camera);
        m_RenderSystem->BuildGraph(m_AssetManager,
                                   preparedRenderWorld->View.Camera,
                                   preparedRenderWorld->HasSelectionWork,
                                   preparedRenderWorld->SelectionOutline,
                                   preparedRenderWorld->SurfacePicking,
                                   preparedRenderWorld->LinePicking,
                                   preparedRenderWorld->PointPicking,
                                   preparedRenderWorld->SurfaceDraws,
                                   preparedRenderWorld->LineDraws,
                                   preparedRenderWorld->PointDraws,
                                   preparedRenderWorld->HtexPatchPreview ? &*preparedRenderWorld->HtexPatchPreview : nullptr);
        frame.Prepared = true;
    }

    void RenderOrchestrator::ExecuteFrame(FrameContext& frame)
    {
        const RenderWorld* preparedRenderWorld = frame.GetPreparedRenderWorld();
        if (!preparedRenderWorld || !preparedRenderWorld->IsValid())
        {
            Core::Log::Warn("RenderOrchestrator::ExecuteFrame skipped: no prepared RenderWorld bound to the active FrameContext.");
            return;
        }

        if (!frame.Prepared)
        {
            Core::Log::Warn("RenderOrchestrator::ExecuteFrame skipped: frame preparation did not complete successfully.");
            return;
        }

        m_RenderSystem->ExecuteGraph();
        m_RenderSystem->EndFrame();
        frame.Submitted = true;
    }

    void RenderOrchestrator::EndFrame(FrameContext& frame)
    {
        frame.LastSubmittedTimelineValue = (frame.Submitted && m_Device) ? m_Device->GetGraphicsTimelineValue() : 0u;
        frame.ResetPreparedState();
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
