module;
#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>
#include "RHI.Vulkan.hpp"

module Runtime.RenderOrchestrator;

import Core.Logging;
import Core.Hash;
import Core.Memory;
import Core.FrameGraph;
import Core.Assets;
import Core.FeatureRegistry;
import Core.Telemetry;
import RHI.Bindless;
import RHI.Descriptors;
import RHI.Device;
import RHI.Image;
import RHI.Profiler;
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
import Graphics.RenderDriver;
import Graphics.RenderPipeline;
import Graphics.ShaderRegistry;
import Interface;
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

    struct RenderOrchestrator::Impl
    {
        Impl(std::shared_ptr<RHI::VulkanDevice> inDevice,
             RHI::VulkanSwapchain& inSwapchain,
             RHI::SimpleRenderer& renderer,
             RHI::BindlessDescriptorSystem& bindless,
             RHI::DescriptorAllocator& descriptorPool,
             RHI::DescriptorLayout& descriptorLayout,
             RHI::TextureManager& textureManager,
             Core::Assets::AssetManager& inAssetManager,
             Core::FeatureRegistry* inFeatureRegistry,
             size_t frameArenaSize,
             uint32_t frameContextCount)
            : FrameArena(frameArenaSize)
            , FrameScope(frameArenaSize)
            , FrameGraph(FrameScope)
            , Device(std::move(inDevice))
            , Swapchain(inSwapchain)
            , Bindless(bindless)
            , DescriptorLayoutRef(descriptorLayout)
            , AssetManagerRef(inAssetManager)
            , FeatureRegistryRef(inFeatureRegistry)
            , FrameContextRing(frameContextCount, frameArenaSize)
        {
            MaterialRegistry = std::make_unique<Graphics::MaterialRegistry>(textureManager, inAssetManager);
            InitPipeline(renderer, descriptorPool);

            if (PipelineLibrary && Device)
            {
                if (auto* p = PipelineLibrary->GetSceneUpdatePipeline();
                    p && PipelineLibrary->GetSceneUpdateSetLayout() != VK_NULL_HANDLE)
                {
                    GpuScene = std::make_unique<Graphics::GPUScene>(
                        *Device, *p, PipelineLibrary->GetSceneUpdateSetLayout());

                    if (RenderDriver)
                        RenderDriver->SetGpuScene(GpuScene.get());
                }
            }
        }

        void InitPipeline(RHI::SimpleRenderer& renderer, RHI::DescriptorAllocator& descriptorPool)
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
                ShaderRegistration{"Outline.Vert"_id, "shaders/debug_view.vert.spv"},
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
                ShaderRegistration{"DebugSurface.Vert"_id, "shaders/debug_surface.vert.spv"},
                ShaderRegistration{"DebugSurface.Frag"_id, "shaders/debug_surface.frag.spv"},
                ShaderRegistration{"Shadow.Depth.Vert"_id, "shaders/shadow_depth.vert.spv"},
            };

            for (const auto& registration : kShaderRegistrations)
                ShaderRegistry.Register(registration.Id, registration.Path.data());

            PipelineLibrary = std::make_unique<Graphics::PipelineLibrary>(
                Device, Bindless, DescriptorLayoutRef);
            PipelineLibrary->BuildDefaults(ShaderRegistry,
                                           Swapchain.GetImageFormat(),
                                           RHI::VulkanImage::FindDepthFormat(*Device));

            Core::Log::Info("RenderOrchestrator: Creating RenderDriver...");
            Graphics::RenderDriverConfig rsConfig{};
            RenderDriver = std::make_unique<Graphics::RenderDriver>(
                rsConfig,
                Device,
                Swapchain,
                renderer,
                Bindless,
                descriptorPool,
                DescriptorLayoutRef,
                *PipelineLibrary,
                ShaderRegistry,
                FrameArena,
                FrameScope,
                GeometryStorage,
                *MaterialRegistry);
            Core::Log::Info("RenderOrchestrator: RenderDriver created successfully.");

            auto defaultPipeline = std::make_unique<Graphics::DefaultPipeline>();
            defaultPipeline->SetFeatureRegistry(FeatureRegistryRef);
            RenderDriver->RequestPipelineSwap(std::move(defaultPipeline));
        }

        Core::Memory::LinearArena FrameArena;
        Core::Memory::ScopeStack FrameScope;
        Core::FrameGraph FrameGraph;
        Graphics::GeometryPool GeometryStorage;
        Graphics::ShaderRegistry ShaderRegistry;
        Graphics::DebugDraw DebugDraw;
        std::unique_ptr<Graphics::PipelineLibrary> PipelineLibrary;
        std::unique_ptr<Graphics::MaterialRegistry> MaterialRegistry;
        std::unique_ptr<Graphics::GPUScene> GpuScene;
        std::unique_ptr<Graphics::RenderDriver> RenderDriver;
        std::shared_ptr<RHI::VulkanDevice> Device;
        RHI::VulkanSwapchain& Swapchain;
        RHI::BindlessDescriptorSystem& Bindless;
        RHI::DescriptorLayout& DescriptorLayoutRef;
        Core::Assets::AssetManager& AssetManagerRef;
        Core::FeatureRegistry* FeatureRegistryRef = nullptr;
        mutable FrameContextRing FrameContextRing;
    };

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
    {
        Core::Log::Info("RenderOrchestrator: Initializing...");
        m_Impl = std::make_unique<Impl>(std::move(device),
                                        swapchain,
                                        renderer,
                                        bindless,
                                        descriptorPool,
                                        descriptorLayout,
                                        textureManager,
                                        assetManager,
                                        featureRegistry,
                                        frameArenaSize,
                                        frameContextCount);
        Core::Log::Info("RenderOrchestrator: frame-context ring configured for {} slots.",
                        m_Impl->FrameContextRing.GetFramesInFlight());

        Core::Log::Info("RenderOrchestrator: Initialization complete.");
    }

    RenderOrchestrator::~RenderOrchestrator()
    {
        Core::Log::Info("RenderOrchestrator: Shutdown complete.");
    }

    Graphics::RenderDriver& RenderOrchestrator::GetRenderDriver() { return *m_Impl->RenderDriver; }
    const Graphics::RenderDriver& RenderOrchestrator::GetRenderDriver() const { return *m_Impl->RenderDriver; }
    Graphics::GPUScene& RenderOrchestrator::GetGPUScene() { return *m_Impl->GpuScene; }
    const Graphics::GPUScene& RenderOrchestrator::GetGPUScene() const { return *m_Impl->GpuScene; }
    Graphics::GPUScene* RenderOrchestrator::GetGPUScenePtr() const { return m_Impl->GpuScene.get(); }
    Graphics::PipelineLibrary& RenderOrchestrator::GetPipelineLibrary() { return *m_Impl->PipelineLibrary; }
    const Graphics::PipelineLibrary& RenderOrchestrator::GetPipelineLibrary() const { return *m_Impl->PipelineLibrary; }
    Graphics::MaterialRegistry& RenderOrchestrator::GetMaterialRegistry() { return *m_Impl->MaterialRegistry; }
    const Graphics::MaterialRegistry& RenderOrchestrator::GetMaterialRegistry() const { return *m_Impl->MaterialRegistry; }
    Graphics::ShaderRegistry& RenderOrchestrator::GetShaderRegistry() { return m_Impl->ShaderRegistry; }
    const Graphics::ShaderRegistry& RenderOrchestrator::GetShaderRegistry() const { return m_Impl->ShaderRegistry; }
    Core::FrameGraph& RenderOrchestrator::GetFrameGraph() { return m_Impl->FrameGraph; }
    const Core::FrameGraph& RenderOrchestrator::GetFrameGraph() const { return m_Impl->FrameGraph; }
    Graphics::GeometryPool& RenderOrchestrator::GetGeometryStorage() { return m_Impl->GeometryStorage; }
    const Graphics::GeometryPool& RenderOrchestrator::GetGeometryStorage() const { return m_Impl->GeometryStorage; }
    Core::Memory::LinearArena& RenderOrchestrator::GetFrameArena() { return m_Impl->FrameArena; }
    Core::Memory::ScopeStack& RenderOrchestrator::GetFrameScope() { return m_Impl->FrameScope; }
    Graphics::DebugDraw& RenderOrchestrator::GetDebugDraw() { return m_Impl->DebugDraw; }
    const Graphics::DebugDraw& RenderOrchestrator::GetDebugDraw() const { return m_Impl->DebugDraw; }
    uint32_t RenderOrchestrator::GetFrameContextCount() const { return m_Impl->FrameContextRing.GetFramesInFlight(); }

    void RenderOrchestrator::OnResize()
    {
        if (m_Impl->RenderDriver)
            m_Impl->RenderDriver->OnResize();

        // After the GPU has been drained and the swapchain recreated, reset all
        // frame-context slots so the next BeginFrame does not attempt to wait on
        // stale timeline values from before the resize.
        m_Impl->FrameContextRing.InvalidateAfterResize();
    }

    void RenderOrchestrator::ResetFrameState()
    {
        m_Impl->FrameScope.Reset();
        m_Impl->FrameArena.Reset();
        m_Impl->DebugDraw.Reset();
    }

    Graphics::EditorOverlayPacket RenderOrchestrator::PrepareEditorOverlay() const
    {
        Interface::GUI::BeginFrame();
        Interface::GUI::DrawGUI();
        return Graphics::EditorOverlayPacket{.HasDrawData = true};
    }

    FrameContext& RenderOrchestrator::BeginFrame() const
    {
        const VkExtent2D extent = m_Impl->Swapchain.GetExtent();
        FrameContext& frame = m_Impl->FrameContextRing.BeginFrame(
            m_Impl->Device ? m_Impl->Device->GetGlobalFrameNumber() : 0u,
            RenderViewport{
                .Width = extent.width,
                .Height = extent.height,
            });
        WaitForFrameContextReuseIfNeeded(m_Impl->Device, frame);

        // Flush per-slot deferred deletions now that the GPU has confirmed
        // completion of this slot's previous work.  Must happen BEFORE
        // render allocators are reset for the new frame.
        //
        // Only flush when reusing a previously-submitted slot (the GPU wait
        // has confirmed completion) or when there is no device (headless/test
        // — nothing is in flight).  Skip on first-cycle slots that were never
        // submitted to avoid flushing deletions before any GPU work ran.
        if (frame.ReusedSubmittedSlot || !m_Impl->Device)
            frame.FlushDeferredDeletions();

        // Push the stored GPU profiling sample from this slot's previous
        // frame to telemetry (B4.9).  The sample was cached in EndFrame()
        // and lives on the FrameContext so profiling data follows frame-
        // context ownership rather than going straight to a global sink.
        if (frame.ResolvedGpuProfile)
        {
            auto& telemetry = Core::Telemetry::TelemetrySystem::Get();
            telemetry.SetGpuFrameTimeNs(frame.ResolvedGpuProfile->GpuFrameTimeNs);

            std::vector<Core::Telemetry::PassTimingEntry> passTimings;
            passTimings.reserve(frame.ResolvedGpuProfile->ScopeCount);
            for (uint32_t s = 0; s < frame.ResolvedGpuProfile->ScopeCount; ++s)
            {
                passTimings.push_back(Core::Telemetry::PassTimingEntry{
                    frame.ResolvedGpuProfile->ScopeNames[s],
                    frame.ResolvedGpuProfile->ScopeDurationsNs[s],
                    0 // CPU time merged separately
                });
            }
            telemetry.SetPassGpuTimings(std::move(passTimings));
            frame.ResolvedGpuProfile.reset();
        }

        return frame;
    }

    RenderWorld RenderOrchestrator::ExtractRenderWorld(const RenderFrameInput& input) const
    {
        RenderWorld world = Runtime::ExtractRenderWorld(input);

        // Override the default lighting with the driver-owned settings
        // so UI-driven changes propagate into the rendered frame.
        world.Lighting = m_Impl->RenderDriver->GetLightEnvironment();

        // Compute cascade light-view-projection matrices from camera state.
        if (world.Lighting.Shadows.Enabled && world.Lighting.Shadows.CascadeCount > 0)
        {
            const auto& cam = world.View.Camera;
            const auto splits = Graphics::ComputeCascadeSplitDistances(
                cam.Near, cam.Far, world.Lighting.Shadows.CascadeCount,
                world.Lighting.Shadows.SplitLambda);
            world.Lighting.Shadows.CascadeSplits = splits;

            constexpr uint32_t kCascadeResolution = 2048u;
            const auto cascadeMatrices = Graphics::ComputeCascadeViewProjections(
                cam.ViewMatrix, cam.ProjectionMatrix,
                world.Lighting.LightDirection,
                splits,
                world.Lighting.Shadows.CascadeCount,
                kCascadeResolution,
                cam.Near, cam.Far);

            for (uint32_t i = 0; i < Graphics::ShadowParams::MaxCascades; ++i)
                world.Lighting.ShadowCascades.LightViewProjection[i] = cascadeMatrices[i];
            world.Lighting.ShadowCascades.SplitDistances = splits;
            world.Lighting.ShadowCascades.CascadeCount = world.Lighting.Shadows.CascadeCount;
        }

        // Snapshot transient debug draw data into immutable vectors so render
        // passes consume frozen state instead of the live DebugDraw accumulator.
        auto lines = m_Impl->DebugDraw.GetLines();
        auto overlayLines = m_Impl->DebugDraw.GetOverlayLines();
        auto points = m_Impl->DebugDraw.GetPoints();
        auto triangles = m_Impl->DebugDraw.GetTriangles();
        if (!lines.empty())
            world.DebugDrawLines.assign(lines.begin(), lines.end());
        if (!overlayLines.empty())
            world.DebugDrawOverlayLines.assign(overlayLines.begin(), overlayLines.end());
        if (!points.empty())
            world.DebugDrawPoints.assign(points.begin(), points.end());
        if (!triangles.empty())
            world.DebugDrawTriangles.assign(triangles.begin(), triangles.end());

        // Resolve interaction state during extraction so BuildGraph consumes
        // immutable extracted state rather than querying live InteractionSystem.
        const auto& interaction = m_Impl->RenderDriver->GetInteraction();

        const auto& pendingPick = interaction.GetPendingPick();
        world.PickRequest = Graphics::PickRequestSnapshot{
            .Pending = pendingPick.Pending,
            .X = pendingPick.X,
            .Y = pendingPick.Y,
        };

        const auto& debugView = interaction.GetDebugViewState();
        world.DebugView = Graphics::DebugViewSnapshot{
            .Enabled = debugView.Enabled,
            .ShowInViewport = debugView.ShowInViewport,
            .DisableCulling = debugView.DisableCulling,
            .SelectedResource = debugView.SelectedResource,
            .DepthNear = debugView.DepthNear,
            .DepthFar = debugView.DepthFar,
        };

        // Snapshot retained GPUScene state so frame-recipe decisions use
        // extraction-time state rather than late live queries.
        world.GpuScene = Graphics::GpuSceneSnapshot{
            .Available = m_Impl->GpuScene != nullptr,
            .ActiveCountApprox = m_Impl->GpuScene ? m_Impl->GpuScene->GetActiveCountApprox() : 0u,
        };

        return world;
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

        const uint64_t currentFrame = m_Impl->Device ? m_Impl->Device->GetGlobalFrameNumber() : frame.FrameNumber;
        m_Impl->RenderDriver->BeginFrame(currentFrame);
        if (!m_Impl->RenderDriver->AcquireFrame())
            return;

        // Rebind render-graph allocators to this FrameContext's per-slot memory.
        // RenderGraph::Reset() (called inside BuildGraph) will reset these allocators.
        if (!frame.HasAllocators())
        {
            Core::Log::Error("RenderOrchestrator::PrepareFrame: FrameContext slot {} has no render allocators — "
                             "skipping frame. FrameContextRing::Configure() must allocate per-slot arenas. "
                             "Ending acquired renderer frame to keep acquire/present rhythm consistent.",
                             frame.SlotIndex);
            m_Impl->RenderDriver->EndFrame();
            return;
        }
        m_Impl->RenderDriver->RebindFrameAllocators(frame.GetRenderArena(), frame.GetRenderScope());

        m_Impl->RenderDriver->UpdateGlobals(preparedRenderWorld->View.Camera, preparedRenderWorld->Lighting);

        const Graphics::GlobalRenderModeOverride renderModeOverride =
            m_Impl->RenderDriver->GetGlobalRenderModeOverride();

        const bool showSurfaceDraws = renderModeOverride == Graphics::GlobalRenderModeOverride::None
            || renderModeOverride == Graphics::GlobalRenderModeOverride::Shaded
            || renderModeOverride == Graphics::GlobalRenderModeOverride::WireframeShaded
            || renderModeOverride == Graphics::GlobalRenderModeOverride::Flat;
        const bool showLineDraws = renderModeOverride == Graphics::GlobalRenderModeOverride::None
            || renderModeOverride == Graphics::GlobalRenderModeOverride::Wireframe
            || renderModeOverride == Graphics::GlobalRenderModeOverride::WireframeShaded;
        const bool showPointDraws = renderModeOverride == Graphics::GlobalRenderModeOverride::None
            || renderModeOverride == Graphics::GlobalRenderModeOverride::Points;

        const std::span<const Graphics::SurfaceDrawPacket> surfaceDraws =
            showSurfaceDraws ? std::span<const Graphics::SurfaceDrawPacket>(preparedRenderWorld->SurfaceDraws) : std::span<const Graphics::SurfaceDrawPacket>{};
        const std::span<const Graphics::LineDrawPacket> lineDraws =
            showLineDraws ? std::span<const Graphics::LineDrawPacket>(preparedRenderWorld->LineDraws) : std::span<const Graphics::LineDrawPacket>{};
        const std::span<const Graphics::PointDrawPacket> pointDraws =
            showPointDraws ? std::span<const Graphics::PointDrawPacket>(preparedRenderWorld->PointDraws) : std::span<const Graphics::PointDrawPacket>{};

        // B1: Resolve bounding spheres from GeometryPool into draw packets so
        // they are self-contained for CPU frustum culling.
        {
            RenderWorld* mutableWorld = frame.GetPreparedRenderWorld();
            Graphics::ResolveDrawPacketBounds(mutableWorld->LineDraws,
                                               mutableWorld->PointDraws,
                                               m_Impl->GeometryStorage);
        }

        // B1: Centralized CPU frustum cull for Line/Point packets.
        // Surface packets are excluded — SurfacePass uses GPU-driven culling.
        const bool cullingEnabled = !preparedRenderWorld->DebugView.DisableCulling;
        Graphics::CulledDrawList culledDraws = Graphics::CullDrawPackets(
            lineDraws,
            pointDraws,
            preparedRenderWorld->View.ProjectionMatrix,
            preparedRenderWorld->View.ViewMatrix,
            cullingEnabled);

        // Construct structured render preparation input from the extracted RenderWorld.
        // BuildGraphInput holds non-owning views into the RenderWorld, which remains
        // alive on the FrameContext until EndFrame.
        const Graphics::BuildGraphInput graphInput{
            .Camera = preparedRenderWorld->View.Camera,
            .Lighting = preparedRenderWorld->Lighting,
            .HasSelectionWork = preparedRenderWorld->HasSelectionWork,
            .SelectionOutline = preparedRenderWorld->SelectionOutline,
            .PickRequest = preparedRenderWorld->PickRequest,
            .DebugView = preparedRenderWorld->DebugView,
            .SurfacePicking = preparedRenderWorld->SurfacePicking,
            .LinePicking = preparedRenderWorld->LinePicking,
            .PointPicking = preparedRenderWorld->PointPicking,
            .SurfaceDraws = surfaceDraws,
            .LineDraws = lineDraws,
            .PointDraws = pointDraws,
            .CulledDraws = std::move(culledDraws),
            .HtexPatchPreview = preparedRenderWorld->HtexPatchPreview ? &*preparedRenderWorld->HtexPatchPreview : nullptr,
            .DebugDrawLines = preparedRenderWorld->DebugDrawLines,
            .DebugDrawOverlayLines = preparedRenderWorld->DebugDrawOverlayLines,
            .DebugDrawPoints = preparedRenderWorld->DebugDrawPoints,
            .DebugDrawTriangles = preparedRenderWorld->DebugDrawTriangles,
            .EditorOverlay = preparedRenderWorld->EditorOverlay,
        };
        m_Impl->RenderDriver->BuildGraph(m_Impl->AssetManagerRef, graphInput);
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

        m_Impl->RenderDriver->ExecuteGraph();
        m_Impl->RenderDriver->EndFrame();
        frame.Submitted = true;
    }

    void RenderOrchestrator::EndFrame(FrameContext& frame)
    {
        frame.LastSubmittedTimelineValue = (frame.Submitted && m_Impl->Device) ? m_Impl->Device->GetGraphicsTimelineValue() : 0u;

        // Consume the resolved GPU profiling sample from the renderer and
        // store it under this FrameContext's ownership (B4.9).  The sample
        // was resolved non-blockingly in SimpleRenderer::EndFrame() for the
        // oldest in-flight slot.  Telemetry is fed from BeginFrame() when
        // the slot is next reused — keeping profiling data on the frame-
        // context ring rather than going directly to a global sink.
        // Only consume when the frame was actually submitted to avoid
        // misattributing stale profiling data to unsubmitted frames.
        if (frame.Submitted && m_Impl->RenderDriver)
            frame.ResolvedGpuProfile = m_Impl->RenderDriver->ConsumeResolvedGpuProfile();

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

        auto [gpuData, token] = Graphics::GeometryGpuData::CreateAsync(m_Impl->Device, transferManager, req, &m_Impl->GeometryStorage);
        if (!gpuData)
        {
            Core::Log::Error("RenderOrchestrator::CreateGeometryView: GPU data creation failed.");
            return { {}, {} };
        }

        const auto h = m_Impl->GeometryStorage.Add(std::move(gpuData));
        return { h, token };
    }
}
