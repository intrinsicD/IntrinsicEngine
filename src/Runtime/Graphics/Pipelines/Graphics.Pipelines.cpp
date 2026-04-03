module;

#include <memory>
#include <span>
#include <glm/glm.hpp>
#include <entt/entt.hpp>

#include <RHI.Vulkan.hpp>

module Graphics.Pipelines;

import Graphics.RenderPipeline;
import Graphics.RenderGraph;
import Graphics.Components;
import Graphics.Geometry;

import Graphics.Passes.Picking;
import Graphics.Passes.Surface;
import Graphics.Passes.Shadow;
import Graphics.Passes.SelectionOutline;
import Graphics.Passes.Line;
import Graphics.Passes.Point;
import Graphics.Passes.HtexPatchPreview;
import Graphics.Passes.DebugView;
import Graphics.Passes.ImGui;
import Graphics.Passes.PostProcess;
import Graphics.Passes.Composition;
import Graphics.PipelineLibrary;
import Graphics.ShaderRegistry;
import RHI.Descriptors;
import RHI.Device;
import Core.Hash;
import Core.FeatureRegistry;
import Core.Logging;

using namespace Core::Hash;


namespace Graphics
{
    struct DefaultPipeline::Impl
    {
        std::unique_ptr<Passes::PickingPass> PickingPass;
        std::unique_ptr<Passes::SurfacePass> SurfacePass;
        std::unique_ptr<Passes::ShadowPass> ShadowPass;
        std::unique_ptr<Passes::SelectionOutlinePass> SelectionOutlinePass;
        std::unique_ptr<Passes::LinePass> LinePass;
        std::unique_ptr<Passes::PointPass> PointPass;
        std::unique_ptr<Passes::HtexPatchPreviewPass> HtexPatchPreviewPass;
        std::unique_ptr<Passes::DebugViewPass> DebugViewPass;
        std::unique_ptr<Passes::ImGuiPass> ImGuiPass;
        std::unique_ptr<Passes::PostProcessPass> PostProcessPass;
        std::unique_ptr<Passes::CompositionPass> CompositionPass;
        RenderPath Path;
        bool PathDirty = true;
    };

    DefaultPipeline::DefaultPipeline() : m_Impl(std::make_unique<Impl>()) {}
    DefaultPipeline::~DefaultPipeline() = default;

    [[nodiscard]] FrameRecipe BuildDefaultPipelineRecipe(const DefaultPipelineRecipeInputs& inputs)
    {
        const bool hasGeometry = inputs.SurfacePassEnabled || inputs.LinePassEnabled || inputs.PointPassEnabled;
        const bool debugActive = inputs.DebugViewPassEnabled && inputs.DebugViewEnabled;
        const Core::Hash::StringID debugResource = inputs.DebugResource;

        const bool debugRequestsDepth = debugActive && debugResource == GetRenderResourceName(RenderResource::SceneDepth);
        const bool debugRequestsEntityId = debugActive &&
                                           (debugResource == GetRenderResourceName(RenderResource::EntityId) ||
                                            debugResource == GetRenderResourceName(RenderResource::SelectionMask) ||
                                            debugResource == GetRenderResourceName(RenderResource::SelectionOutline));
        const bool debugRequestsPrimitiveId = debugActive &&
                                             (debugResource == GetRenderResourceName(RenderResource::EntityId) ||
                                              debugResource == GetRenderResourceName(RenderResource::PrimitiveId) ||
                                              debugResource == GetRenderResourceName(RenderResource::SelectionMask) ||
                                              debugResource == GetRenderResourceName(RenderResource::SelectionOutline));
        const bool debugRequestsNormals = debugActive && debugResource == GetRenderResourceName(RenderResource::SceneNormal);
        const bool debugRequestsMaterial = debugActive &&
                                           (debugResource == GetRenderResourceName(RenderResource::Albedo) ||
                                            debugResource == GetRenderResourceName(RenderResource::Material0));
        const bool debugRequestsSceneColor = debugActive && debugResource == GetRenderResourceName(RenderResource::SceneColorHDR);
        const bool debugRequestsSelection = debugActive &&
                                            (debugResource == GetRenderResourceName(RenderResource::SelectionMask) ||
                                             debugResource == GetRenderResourceName(RenderResource::SelectionOutline));

        const bool selectionActive = (inputs.SelectionOutlinePassEnabled && inputs.HasSelectionWork) || debugRequestsSelection;
        const bool needsPickingSideband = inputs.PickingPassEnabled || debugRequestsEntityId || debugRequestsPrimitiveId;

        FrameRecipe recipe{};
        recipe.Selection = selectionActive;
        recipe.DebugVisualization = debugActive;
        recipe.Post = inputs.PostProcessPassEnabled;

        recipe.Depth = hasGeometry || needsPickingSideband || debugRequestsDepth || selectionActive;
        recipe.EntityId = needsPickingSideband || selectionActive;
        recipe.PrimitiveId = inputs.PickingPassEnabled || debugRequestsPrimitiveId;

        const bool hybridRequested = inputs.RequestedLightingPath == FrameLightingPath::Hybrid;
        const bool deferredRequested = hybridRequested ||
                                       inputs.RequestedLightingPath == FrameLightingPath::Deferred;
        const bool deferredUsable = deferredRequested && inputs.SurfacePassEnabled && inputs.CompositionPassEnabled && hasGeometry;
        recipe.LightingPath = hasGeometry
            ? (deferredUsable ? (hybridRequested ? FrameLightingPath::Hybrid : FrameLightingPath::Deferred)
                              : FrameLightingPath::Forward)
            : (debugRequestsSceneColor ? FrameLightingPath::Forward : FrameLightingPath::None);

        recipe.DepthPrepass = inputs.DepthPrepassEnabled && inputs.SurfacePassEnabled && hasGeometry;
        recipe.Normals = UsesDeferredComposition(recipe.LightingPath) || debugRequestsNormals;
        recipe.MaterialChannels = UsesDeferredComposition(recipe.LightingPath) || debugRequestsMaterial;
        recipe.SceneColorLDR = recipe.Post || selectionActive || debugActive || inputs.ImGuiPassEnabled;
        recipe.Shadows = inputs.ShadowsEnabled && hasGeometry && recipe.LightingPath != FrameLightingPath::None;

        return recipe;
    }


    void DefaultPipeline::Shutdown()
    {
        if (m_Impl->PickingPass)          m_Impl->PickingPass->Shutdown();
        if (m_Impl->SurfacePass)          m_Impl->SurfacePass->Shutdown();
        if (m_Impl->ShadowPass)           m_Impl->ShadowPass->Shutdown();
        if (m_Impl->SelectionOutlinePass) m_Impl->SelectionOutlinePass->Shutdown();
        if (m_Impl->LinePass)             m_Impl->LinePass->Shutdown();
        if (m_Impl->PointPass)            m_Impl->PointPass->Shutdown();
        if (m_Impl->HtexPatchPreviewPass) m_Impl->HtexPatchPreviewPass->Shutdown();
        if (m_Impl->PostProcessPass)      m_Impl->PostProcessPass->Shutdown();
        if (m_Impl->CompositionPass)      m_Impl->CompositionPass->Shutdown();
        if (m_Impl->DebugViewPass)        m_Impl->DebugViewPass->Shutdown();
        if (m_Impl->ImGuiPass)            m_Impl->ImGuiPass->Shutdown();

        m_Impl->PickingPass.reset();
        m_Impl->SurfacePass.reset();
        m_Impl->ShadowPass.reset();
        m_Impl->SelectionOutlinePass.reset();
        m_Impl->LinePass.reset();
        m_Impl->PointPass.reset();
        m_Impl->HtexPatchPreviewPass.reset();
        m_Impl->PostProcessPass.reset();
        m_Impl->CompositionPass.reset();
        m_Impl->DebugViewPass.reset();
        m_Impl->ImGuiPass.reset();
    }

    void DefaultPipeline::Initialize(RHI::VulkanDevice& device,
                                    RHI::DescriptorAllocator& descriptorPool,
                                    RHI::DescriptorLayout& globalLayout,
                                    const ShaderRegistry& shaderRegistry,
                                    PipelineLibrary& pipelineLibrary)
    {
        m_Impl->PickingPass          = std::make_unique<Passes::PickingPass>();
        m_Impl->SurfacePass          = std::make_unique<Passes::SurfacePass>();
        m_Impl->ShadowPass           = std::make_unique<Passes::ShadowPass>();
        m_Impl->SelectionOutlinePass = std::make_unique<Passes::SelectionOutlinePass>();
        m_Impl->LinePass             = std::make_unique<Passes::LinePass>();
        m_Impl->PointPass            = std::make_unique<Passes::PointPass>();
        m_Impl->HtexPatchPreviewPass = std::make_unique<Passes::HtexPatchPreviewPass>();
        m_Impl->DebugViewPass        = std::make_unique<Passes::DebugViewPass>();
        m_Impl->ImGuiPass            = std::make_unique<Passes::ImGuiPass>();
        m_Impl->PostProcessPass      = std::make_unique<Passes::PostProcessPass>();
        m_Impl->CompositionPass      = std::make_unique<Passes::CompositionPass>();

        m_Impl->PickingPass->Initialize(device, descriptorPool, globalLayout);
        m_Impl->SurfacePass->Initialize(device, descriptorPool, globalLayout);
        m_Impl->ShadowPass->Initialize(device, descriptorPool, globalLayout);
        m_Impl->SelectionOutlinePass->Initialize(device, descriptorPool, globalLayout);
        m_Impl->LinePass->Initialize(device, descriptorPool, globalLayout);
        m_Impl->PointPass->Initialize(device, descriptorPool, globalLayout);
        m_Impl->HtexPatchPreviewPass->Initialize(device, descriptorPool, globalLayout);
        m_Impl->PostProcessPass->Initialize(device, descriptorPool, globalLayout);
        m_Impl->CompositionPass->Initialize(device, descriptorPool, globalLayout);
        m_Impl->DebugViewPass->Initialize(device, descriptorPool, globalLayout);
        m_Impl->ImGuiPass->Initialize(device, descriptorPool, globalLayout);

        m_Impl->PickingPass->SetPipeline(&pipelineLibrary.GetOrDie(kPipeline_Picking));
        m_Impl->PickingPass->SetMeshPickPipeline(&pipelineLibrary.GetOrDie(kPipeline_PickMesh));
        m_Impl->PickingPass->SetLinePickPipeline(&pipelineLibrary.GetOrDie(kPipeline_PickLine));
        m_Impl->PickingPass->SetPointPickPipeline(&pipelineLibrary.GetOrDie(kPipeline_PickPoint));

        m_Impl->SurfacePass->SetPipeline(&pipelineLibrary.GetOrDie(kPipeline_Surface));
        m_Impl->SurfacePass->SetLinePipeline(&pipelineLibrary.GetOrDie(kPipeline_SurfaceLines));
        m_Impl->SurfacePass->SetPointPipeline(&pipelineLibrary.GetOrDie(kPipeline_SurfacePoints));
        m_Impl->SurfacePass->SetInstanceSetLayout(pipelineLibrary.GetStage1InstanceSetLayout());
        m_Impl->SurfacePass->SetCullPipeline(pipelineLibrary.GetCullPipeline());
        m_Impl->SurfacePass->SetCullSetLayout(pipelineLibrary.GetCullSetLayout());

        m_Impl->SelectionOutlinePass->SetShaderRegistry(shaderRegistry);
        m_Impl->LinePass->SetShaderRegistry(shaderRegistry);
        m_Impl->PointPass->SetShaderRegistry(shaderRegistry);
        m_Impl->DebugViewPass->SetShaderRegistry(shaderRegistry);
        m_Impl->PostProcessPass->SetShaderRegistry(shaderRegistry);
        m_Impl->CompositionPass->SetShaderRegistry(shaderRegistry);
        m_Impl->CompositionPass->SetGlobalSetLayout(globalLayout.GetHandle());

        // Wire G-buffer pipeline to SurfacePass for deferred path.
        if (pipelineLibrary.Contains(kPipeline_SurfaceGBuffer))
            m_Impl->SurfacePass->SetGBufferPipeline(&pipelineLibrary.GetOrDie(kPipeline_SurfaceGBuffer));

        // Wire debug surface pipeline for transient triangle visualization.
        if (pipelineLibrary.Contains(kPipeline_DebugSurface))
            m_Impl->SurfacePass->SetDebugSurfacePipeline(&pipelineLibrary.GetOrDie(kPipeline_DebugSurface));

        // Wire depth prepass pipeline for early-Z fill.
        if (pipelineLibrary.Contains(kPipeline_DepthPrepass))
            m_Impl->SurfacePass->SetDepthPrepassPipeline(&pipelineLibrary.GetOrDie(kPipeline_DepthPrepass));
        if (pipelineLibrary.Contains(kPipeline_ShadowDepth))
            m_Impl->ShadowPass->SetPipeline(&pipelineLibrary.GetOrDie(kPipeline_ShadowDepth));

        m_Impl->PathDirty = true;
    }

    bool DefaultPipeline::IsFeatureEnabled(const Core::FeatureDescriptor& descriptor) const
    {
        if (!m_Registry) return true; // No registry → all features enabled

        // Render-path resilience: when a new pass is added but not yet registered
        // in the FeatureRegistry catalog, keep the pass enabled by default rather
        // than silently dropping it from the pipeline.
        const Core::FeatureInfo* info = m_Registry->Find(descriptor);
        if (!info)
            return true;

        return info->Enabled;
    }

    void DefaultPipeline::RebuildPath()
    {
        m_Impl->Path.Clear();

        // ==================================================================
        // 1. Picking (Readback) — entity/primitive ID for click queries.
        // ==================================================================
        if (m_Impl->PickingPass && IsFeatureEnabled(FeatureCatalog::PickingPass))
            m_Impl->Path.AddFeature("Picking", m_Impl->PickingPass.get());

        // ==================================================================
        // 2. Surface Pass — face rendering via SurfacePass (triangles / lines /
        //    point geometry).
        // ==================================================================
        if (m_Impl->SurfacePass && IsFeatureEnabled(FeatureCatalog::SurfacePass))
            m_Impl->Path.AddFeature("MeshPass.Surface", m_Impl->SurfacePass.get());

        // ==================================================================
        // 3. ShadowPass — shadow-atlas depth lane (CSM phase-1 scaffold).
        // ==================================================================
        if (m_Impl->ShadowPass && IsFeatureEnabled(FeatureCatalog::ShadowPass))
            m_Impl->Path.AddFeature("ShadowPass", m_Impl->ShadowPass.get());

        // ==================================================================
        // 4. Composition — deferred lighting.
        //    Reads G-buffer (SceneNormal, Albedo, Material0) + SceneDepth
        //    and writes SceneColorHDR via a fullscreen deferred lighting pass.
        //    No-op when the frame recipe selects the forward lighting path
        //    (SurfacePass writes SceneColorHDR directly in that case).
        //
        //    IMPORTANT: this must execute before LinePass / PointPass. Those
        //    passes are the forward-overlay lane for primitives/materials that
        //    stay out of the deferred G-buffer (wireframe, debug, point-cloud,
        //    future transparent/special materials). Running composition first
        //    preserves their SceneColorHDR LOAD/accumulate contract.
        // ==================================================================
        if (m_Impl->CompositionPass)
            m_Impl->Path.AddFeature("Composition", m_Impl->CompositionPass.get());

        // ==================================================================
        // 5. LinePass — unified BDA-based line rendering.
        //    Consolidates retained wireframe/graph edges and transient DebugDraw
        //    lines into a single forward-overlay lane on SceneColorHDR.
        // ==================================================================
        if (m_Impl->LinePass && IsFeatureEnabled(FeatureCatalog::LinePass))
        {
            m_Impl->Path.AddStage("LinePass", [this](RenderPassContext& ctx)
            {
                m_Impl->LinePass->SetGeometryStorage(&ctx.GeometryStorage);
                m_Impl->LinePass->AddPasses(ctx);
            });
        }

        // ==================================================================
        // 6. PointPass — unified BDA-based point rendering.
        //    Iterates ECS::Point::Component for retained draws and
        //    DebugDraw::GetPoints() for transient markers. Like LinePass, this
        //    is part of the forward-overlay lane that composes after deferred
        //    lighting when the frame recipe selects that path.
        // ==================================================================
        if (m_Impl->PointPass && IsFeatureEnabled(FeatureCatalog::PointPass))
        {
            m_Impl->Path.AddStage("Points", [this](RenderPassContext& ctx)
            {
                m_Impl->PointPass->SetGeometryStorage(&ctx.GeometryStorage);
                m_Impl->PointPass->AddPasses(ctx);
            });
        }

        // ==================================================================
        // 7. HtexPatchPreview — hierarchical texture patch preview.
        //    Renders a low-res proxy of the selected texture region for debugging
        //    texture streaming and LOD transitions.
        // ==================================================================
        if (m_Impl->HtexPatchPreviewPass && IsFeatureEnabled(FeatureCatalog::HtexPatchPreviewPass))
        {
            m_Impl->Path.AddStage("HtexPatchPreview", [this](RenderPassContext& ctx)
            {
                m_Impl->HtexPatchPreviewPass->AddPasses(ctx);
            });
        }

        // ==================================================================
        // 8. Post-Processing — HDR tone mapping + optional FXAA.
        //    Reads canonical SceneColorHDR and writes canonical SceneColorLDR.
        //    Final presentation to the imported swapchain image happens in the
        //    dedicated Present stage below.
        // ==================================================================
        if (m_Impl->PostProcessPass && IsFeatureEnabled(FeatureCatalog::PostProcessPass))
            m_Impl->Path.AddFeature("PostProcess", m_Impl->PostProcessPass.get());

        // ==================================================================
        // 9. Selection Outline — post-process overlay for selected entities.
        // ==================================================================
        if (m_Impl->SelectionOutlinePass && IsFeatureEnabled(FeatureCatalog::SelectionOutlinePass))
            m_Impl->Path.AddFeature("SelectionOutline", m_Impl->SelectionOutlinePass.get());

        // ==================================================================
        // 10. Debug View — conditional texture inspector overlay.
        // ==================================================================
        if (m_Impl->DebugViewPass && IsFeatureEnabled(FeatureCatalog::DebugViewPass))
        {
            m_Impl->Path.AddStage("DebugView", [this](RenderPassContext& ctx)
            {
                if (ctx.Debug.Enabled)
                    m_Impl->DebugViewPass->AddPasses(ctx);
            });
        }

        // ==================================================================
        // 11. ImGui — editor UI overlay.
        // ==================================================================
        if (m_Impl->ImGuiPass && IsFeatureEnabled(FeatureCatalog::ImGuiPass))
            m_Impl->Path.AddFeature("ImGui", m_Impl->ImGuiPass.get());

        m_Impl->Path.AddStage("Present", [](RenderPassContext& ctx)
        {
            const RGResourceHandle src = ctx.Blackboard.Get(RenderResource::SceneColorLDR);
            const RGResourceHandle dst = ctx.Blackboard.Get("Backbuffer"_id);
            if (!src.IsValid() || !dst.IsValid())
                return;

            struct PresentData
            {
                RGResourceHandle Src;
                RGResourceHandle Dst;
            };

            ctx.Graph.AddPass<PresentData>("Present.LDR",
                                           [&](PresentData& data, RGBuilder& builder)
                                           {
                                               data.Src = builder.Read(src,
                                                                       VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                                                       VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
                                               data.Dst = builder.Write(dst,
                                                                        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                                                        VK_ACCESS_2_TRANSFER_WRITE_BIT);
                                           },
                                           [resolution = ctx.Resolution](const PresentData& data,
                                                                         const RGRegistry& reg,
                                                                         VkCommandBuffer cmd)
                                           {
                                               if (!data.Src.IsValid() || !data.Dst.IsValid())
                                                   return;

                                               VkImageBlit blit{};
                                               blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                                               blit.srcSubresource.layerCount = 1;
                                               blit.srcOffsets[1] = {
                                                   static_cast<int32_t>(resolution.width),
                                                   static_cast<int32_t>(resolution.height),
                                                   1
                                               };
                                               blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                                               blit.dstSubresource.layerCount = 1;
                                               blit.dstOffsets[1] = {
                                                   static_cast<int32_t>(resolution.width),
                                                   static_cast<int32_t>(resolution.height),
                                                   1
                                               };

                                               vkCmdBlitImage(cmd,
                                                              reg.GetImage(data.Src), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                              reg.GetImage(data.Dst), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                              1, &blit, VK_FILTER_NEAREST);

                                               VkImageMemoryBarrier2 restoreBarrier{};
                                               restoreBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                                               restoreBarrier.image = reg.GetImage(data.Dst);
                                               restoreBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                                               restoreBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                                               restoreBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                                               restoreBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                                               restoreBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                                               restoreBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                                               restoreBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                                               restoreBarrier.subresourceRange.baseMipLevel = 0;
                                               restoreBarrier.subresourceRange.levelCount = 1;
                                               restoreBarrier.subresourceRange.baseArrayLayer = 0;
                                               restoreBarrier.subresourceRange.layerCount = 1;

                                               VkDependencyInfo restoreInfo{};
                                               restoreInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                                               restoreInfo.imageMemoryBarrierCount = 1;
                                               restoreInfo.pImageMemoryBarriers = &restoreBarrier;
                                               vkCmdPipelineBarrier2(cmd, &restoreInfo);
                                           });
        });
    }

    void DefaultPipeline::SetupFrame(RenderPassContext& ctx)
    {
        // When a FeatureRegistry is connected, rebuild every frame so that
        // runtime enable/disable changes take effect immediately.
        if (m_Impl->PathDirty || m_Registry)
        {
            RebuildPath();
            m_Impl->PathDirty = false;
        }

        m_Impl->Path.Execute(ctx);
    }

    void DefaultPipeline::OnResize(uint32_t width, uint32_t height)
    {
        if (m_Impl->PickingPass)          m_Impl->PickingPass->OnResize(width, height);
        if (m_Impl->SurfacePass)          m_Impl->SurfacePass->OnResize(width, height);
        if (m_Impl->ShadowPass)           m_Impl->ShadowPass->OnResize(width, height);
        if (m_Impl->SelectionOutlinePass) m_Impl->SelectionOutlinePass->OnResize(width, height);
        if (m_Impl->LinePass)             m_Impl->LinePass->OnResize(width, height);
        if (m_Impl->PointPass)            m_Impl->PointPass->OnResize(width, height);
        if (m_Impl->HtexPatchPreviewPass) m_Impl->HtexPatchPreviewPass->OnResize(width, height);
        if (m_Impl->PostProcessPass)      m_Impl->PostProcessPass->OnResize(width, height);
        if (m_Impl->CompositionPass)      m_Impl->CompositionPass->OnResize(width, height);
        if (m_Impl->DebugViewPass)        m_Impl->DebugViewPass->OnResize(width, height);
        if (m_Impl->ImGuiPass)            m_Impl->ImGuiPass->OnResize(width, height);
    }

    void DefaultPipeline::PostCompile(uint32_t frameIndex,
                                     std::span<const RenderGraphDebugImage> debugImages,
                                     std::span<const RenderGraphDebugPass>)
    {
        if (m_Impl->PostProcessPass)
            m_Impl->PostProcessPass->PostCompile(frameIndex, debugImages);
        if (m_Impl->CompositionPass)
            m_Impl->CompositionPass->PostCompile(frameIndex, debugImages);
        if (m_Impl->SelectionOutlinePass)
            m_Impl->SelectionOutlinePass->PostCompile(frameIndex, debugImages);
        if (m_Impl->DebugViewPass)
            m_Impl->DebugViewPass->PostCompile(frameIndex, debugImages);
    }

    FrameRecipe DefaultPipeline::BuildFrameRecipe(const RenderPassContext& ctx) const
    {
        DefaultPipelineRecipeInputs inputs{};
        inputs.PickingPassEnabled = m_Impl->PickingPass && IsFeatureEnabled(FeatureCatalog::PickingPass);
        inputs.SurfacePassEnabled = m_Impl->SurfacePass && IsFeatureEnabled(FeatureCatalog::SurfacePass);
        inputs.LinePassEnabled = m_Impl->LinePass && IsFeatureEnabled(FeatureCatalog::LinePass);
        inputs.PointPassEnabled = m_Impl->PointPass && IsFeatureEnabled(FeatureCatalog::PointPass);
        inputs.PostProcessPassEnabled = m_Impl->PostProcessPass && IsFeatureEnabled(FeatureCatalog::PostProcessPass);
        inputs.SelectionOutlinePassEnabled = m_Impl->SelectionOutlinePass && IsFeatureEnabled(FeatureCatalog::SelectionOutlinePass);
        inputs.DebugViewPassEnabled = m_Impl->DebugViewPass && IsFeatureEnabled(FeatureCatalog::DebugViewPass);
        inputs.ImGuiPassEnabled = m_Impl->ImGuiPass && IsFeatureEnabled(FeatureCatalog::ImGuiPass);
        inputs.CompositionPassEnabled = m_Impl->CompositionPass != nullptr;
        inputs.ShadowsEnabled = ctx.Lighting.Shadows.Enabled;
        inputs.HasSelectionWork = ctx.HasSelectionWork;
        inputs.DebugViewEnabled = ctx.Debug.Enabled;
        inputs.DebugResource = ctx.Debug.SelectedResource;

        inputs.DepthPrepassEnabled = IsFeatureEnabled(FeatureCatalog::DepthPrepass);
        inputs.RequestedLightingPath = (inputs.CompositionPassEnabled && IsFeatureEnabled(FeatureCatalog::DeferredLighting))
            ? FrameLightingPath::Deferred
            : FrameLightingPath::Forward;

        FrameRecipe recipe = BuildDefaultPipelineRecipe(inputs);

        if ((recipe.Selection || recipe.DebugVisualization) && !inputs.PickingPassEnabled && recipe.EntityId)
        {
            Core::Log::Warn("DefaultPipeline: EntityId requested by selection/debug recipe but PickingPass is disabled; resource will exist without a producer.");
        }

        return recipe;
    }

    [[nodiscard]] RenderPipelineDebugState DefaultPipeline::GetDebugState() const
    {
        RenderPipelineDebugState state{};
        state.HasFeatureRegistry = (m_Registry != nullptr);
        state.PathDirty = m_Impl->PathDirty;

        state.PickingPass = {m_Impl->PickingPass != nullptr, m_Impl->PickingPass && IsFeatureEnabled(FeatureCatalog::PickingPass)};
        state.DepthPrepass = {m_Impl->SurfacePass != nullptr, m_Impl->SurfacePass && IsFeatureEnabled(FeatureCatalog::DepthPrepass)};
        state.SurfacePass = {m_Impl->SurfacePass != nullptr, m_Impl->SurfacePass && IsFeatureEnabled(FeatureCatalog::SurfacePass)};
        state.SelectionOutlinePass = {m_Impl->SelectionOutlinePass != nullptr, m_Impl->SelectionOutlinePass && IsFeatureEnabled(FeatureCatalog::SelectionOutlinePass)};
        state.LinePass = {m_Impl->LinePass != nullptr, m_Impl->LinePass && IsFeatureEnabled(FeatureCatalog::LinePass)};
        state.PointPass = {m_Impl->PointPass != nullptr, m_Impl->PointPass && IsFeatureEnabled(FeatureCatalog::PointPass)};
        state.PostProcessPass = {m_Impl->PostProcessPass != nullptr, m_Impl->PostProcessPass && IsFeatureEnabled(FeatureCatalog::PostProcessPass)};
        state.DebugViewPass = {m_Impl->DebugViewPass != nullptr, m_Impl->DebugViewPass && IsFeatureEnabled(FeatureCatalog::DebugViewPass)};
        state.ImGuiPass = {m_Impl->ImGuiPass != nullptr, m_Impl->ImGuiPass && IsFeatureEnabled(FeatureCatalog::ImGuiPass)};
        return state;
    }

    Passes::SelectionOutlineSettings* DefaultPipeline::GetSelectionOutlineSettings()
    {
        return m_Impl->SelectionOutlinePass ? &m_Impl->SelectionOutlinePass->GetSettings() : nullptr;
    }

    Passes::PostProcessSettings* DefaultPipeline::GetPostProcessSettings()
    {
        return m_Impl->PostProcessPass ? &m_Impl->PostProcessPass->GetSettings() : nullptr;
    }

    const Passes::HistogramReadback* DefaultPipeline::GetHistogramReadback() const
    {
        return m_Impl->PostProcessPass ? &m_Impl->PostProcessPass->GetHistogram() : nullptr;
    }

    [[nodiscard]] const Passes::SelectionOutlineDebugState* DefaultPipeline::GetSelectionOutlineDebugState() const
    {
        return m_Impl->SelectionOutlinePass ? &m_Impl->SelectionOutlinePass->GetDebugState() : nullptr;
    }

    [[nodiscard]] const Passes::PostProcessDebugState* DefaultPipeline::GetPostProcessDebugState() const
    {
        return m_Impl->PostProcessPass ? &m_Impl->PostProcessPass->GetDebugState() : nullptr;
    }
}
