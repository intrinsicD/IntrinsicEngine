module;

#include <cstddef>
#include <memory>
#include <span>
#include <algorithm>
#include <unordered_set>
#include <glm/glm.hpp>
#include <entt/entt.hpp>

#include <RHI.Vulkan.hpp>

module Graphics:Pipelines.Impl;

import :RenderPipeline;
import :RenderGraph;
import :Components;
import :Geometry;
import :DebugDraw;
import :Passes.Picking;
import :Passes.Surface;
import :Passes.SelectionOutline;
import :Passes.Line;
import :Passes.Point;
import :Passes.DebugView;
import :Passes.ImGui;
import :Passes.PostProcess;
import :PipelineLibrary;
import :ShaderRegistry;
import :Pipelines;
import RHI;
import ECS;
import Core.Hash;
import Core.FeatureRegistry;
import Core.Logging;

using namespace Core::Hash;


namespace Graphics
{
    void DefaultPipeline::Shutdown()
    {
        if (m_PickingPass)          m_PickingPass->Shutdown();
        if (m_SurfacePass)          m_SurfacePass->Shutdown();
        if (m_SelectionOutlinePass) m_SelectionOutlinePass->Shutdown();
        if (m_LinePass)             m_LinePass->Shutdown();
        if (m_PointPass)            m_PointPass->Shutdown();
        if (m_PostProcessPass)      m_PostProcessPass->Shutdown();
        if (m_DebugViewPass)        m_DebugViewPass->Shutdown();
        if (m_ImGuiPass)            m_ImGuiPass->Shutdown();

        m_PickingPass.reset();
        m_SurfacePass.reset();
        m_SelectionOutlinePass.reset();
        m_LinePass.reset();
        m_PointPass.reset();
        m_PostProcessPass.reset();
        m_DebugViewPass.reset();
        m_ImGuiPass.reset();
    }

    void DefaultPipeline::Initialize(RHI::VulkanDevice& device,
                                    RHI::DescriptorAllocator& descriptorPool,
                                    RHI::DescriptorLayout& globalLayout,
                                    const ShaderRegistry& shaderRegistry,
                                    PipelineLibrary& pipelineLibrary)
    {
        m_PickingPass          = std::make_unique<Passes::PickingPass>();
        m_SurfacePass          = std::make_unique<Passes::SurfacePass>();
        m_SelectionOutlinePass = std::make_unique<Passes::SelectionOutlinePass>();
        m_LinePass             = std::make_unique<Passes::LinePass>();
        m_PointPass            = std::make_unique<Passes::PointPass>();
        m_DebugViewPass        = std::make_unique<Passes::DebugViewPass>();
        m_ImGuiPass            = std::make_unique<Passes::ImGuiPass>();
        m_PostProcessPass      = std::make_unique<Passes::PostProcessPass>();

        m_PickingPass->Initialize(device, descriptorPool, globalLayout);
        m_SurfacePass->Initialize(device, descriptorPool, globalLayout);
        m_SelectionOutlinePass->Initialize(device, descriptorPool, globalLayout);
        m_LinePass->Initialize(device, descriptorPool, globalLayout);
        m_PointPass->Initialize(device, descriptorPool, globalLayout);
        m_PostProcessPass->Initialize(device, descriptorPool, globalLayout);
        m_DebugViewPass->Initialize(device, descriptorPool, globalLayout);
        m_ImGuiPass->Initialize(device, descriptorPool, globalLayout);

        m_PickingPass->SetPipeline(&pipelineLibrary.GetOrDie(kPipeline_Picking));

        m_SurfacePass->SetPipeline(&pipelineLibrary.GetOrDie(kPipeline_Surface));
        m_SurfacePass->SetLinePipeline(&pipelineLibrary.GetOrDie(kPipeline_SurfaceLines));
        m_SurfacePass->SetPointPipeline(&pipelineLibrary.GetOrDie(kPipeline_SurfacePoints));
        m_SurfacePass->SetInstanceSetLayout(pipelineLibrary.GetStage1InstanceSetLayout());
        m_SurfacePass->SetCullPipeline(pipelineLibrary.GetCullPipeline());
        m_SurfacePass->SetCullSetLayout(pipelineLibrary.GetCullSetLayout());

        m_SelectionOutlinePass->SetShaderRegistry(shaderRegistry);
        m_LinePass->SetShaderRegistry(shaderRegistry);
        m_PointPass->SetShaderRegistry(shaderRegistry);
        m_DebugViewPass->SetShaderRegistry(shaderRegistry);
        m_PostProcessPass->SetShaderRegistry(shaderRegistry);

        m_PathDirty = true;
    }

    bool DefaultPipeline::IsFeatureEnabled(Core::Hash::StringID id) const
    {
        if (!m_Registry) return true; // No registry → all features enabled

        // Render-path resilience: when a new pass is added but not yet registered
        // in the FeatureRegistry catalog, keep the pass enabled by default rather
        // than silently dropping it from the pipeline.
        const Core::FeatureInfo* info = m_Registry->Find(id);
        if (!info)
            return true;

        return info->Enabled;
    }

    void DefaultPipeline::RebuildPath()
    {
        m_Path.Clear();

        // ==================================================================
        // 1. Picking (Readback) — entity/primitive ID for click queries.
        // ==================================================================
        if (m_PickingPass && IsFeatureEnabled("PickingPass"_id))
            m_Path.AddFeature("Picking", m_PickingPass.get());

        // ==================================================================
        // 2. Surface Pass — face rendering via SurfacePass (triangles / lines /
        //    point geometry).
        // ==================================================================
        if (m_SurfacePass && IsFeatureEnabled("SurfacePass"_id))
            m_Path.AddFeature("MeshPass.Surface", m_SurfacePass.get());

        // ==================================================================
        // 3. LinePass — unified BDA-based line rendering.
        //    Consolidates retained wireframe/graph edges and transient DebugDraw
        //    lines into a single pass. Always present when the pass exists so
        //    debug-line output is not silently dropped.
        // ==================================================================
        if (m_LinePass)
        {
            m_Path.AddStage("LinePass", [this](RenderPassContext& ctx)
            {
                m_LinePass->SetGeometryStorage(&ctx.GeometryStorage);
                m_LinePass->SetDebugDraw(ctx.DebugDrawPtr);
                m_LinePass->AddPasses(ctx);
            });
        }

        // ==================================================================
        // 4. PointPass — unified BDA-based point rendering.
        //    Iterates ECS::Point::Component for retained draws and
        //    DebugDraw::GetPoints() for transient markers.
        // ==================================================================
        if (m_PointPass && IsFeatureEnabled("PointPass"_id))
        {
            m_Path.AddStage("Points", [this](RenderPassContext& ctx)
            {
                m_PointPass->SetGeometryStorage(&ctx.GeometryStorage);
                m_PointPass->SetDebugDraw(ctx.DebugDrawPtr);
                m_PointPass->AddPasses(ctx);
            });
        }

        // ==================================================================
        // 5. Composition insertion point (future).
        //    When a deferred or hybrid lighting path is implemented, a
        //    composition stage would be added here — after geometry passes
        //    and before post-processing — to produce SceneColorHDR from
        //    G-buffer channels. For the current forward path, geometry
        //    passes write directly to SceneColorHDR; no composition needed.
        // ==================================================================

        // ==================================================================
        // 6. Post-Processing — HDR tone mapping + optional FXAA.
        //    Reads canonical SceneColorHDR and writes canonical SceneColorLDR.
        //    Final presentation to the imported swapchain image happens in the
        //    dedicated Present stage below.
        // ==================================================================
        if (m_PostProcessPass && IsFeatureEnabled("PostProcessPass"_id))
            m_Path.AddFeature("PostProcess", m_PostProcessPass.get());

        // ==================================================================
        // 6. Selection Outline — post-process overlay for selected entities.
        // ==================================================================
        if (m_SelectionOutlinePass && IsFeatureEnabled("SelectionOutlinePass"_id))
            m_Path.AddFeature("SelectionOutline", m_SelectionOutlinePass.get());

        // ==================================================================
        // 7. Debug View — conditional texture inspector overlay.
        // ==================================================================
        if (m_DebugViewPass && IsFeatureEnabled("DebugViewPass"_id))
        {
            m_Path.AddStage("DebugView", [this](RenderPassContext& ctx)
            {
                if (ctx.Debug.Enabled)
                    m_DebugViewPass->AddPasses(ctx);
            });
        }

        // ==================================================================
        // 8. ImGui — editor UI overlay.
        // ==================================================================
        if (m_ImGuiPass && IsFeatureEnabled("ImGuiPass"_id))
            m_Path.AddFeature("ImGui", m_ImGuiPass.get());

        m_Path.AddStage("Present", [](RenderPassContext& ctx)
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
        if (m_PathDirty || m_Registry)
        {
            RebuildPath();
            m_PathDirty = false;
        }

        m_Path.Execute(ctx);
    }

    void DefaultPipeline::OnResize(uint32_t width, uint32_t height)
    {
        if (m_PickingPass)          m_PickingPass->OnResize(width, height);
        if (m_SurfacePass)          m_SurfacePass->OnResize(width, height);
        if (m_SelectionOutlinePass) m_SelectionOutlinePass->OnResize(width, height);
        if (m_LinePass)             m_LinePass->OnResize(width, height);
        if (m_PointPass)            m_PointPass->OnResize(width, height);
        if (m_PostProcessPass)      m_PostProcessPass->OnResize(width, height);
        if (m_DebugViewPass)        m_DebugViewPass->OnResize(width, height);
        if (m_ImGuiPass)            m_ImGuiPass->OnResize(width, height);
    }

    void DefaultPipeline::PostCompile(uint32_t frameIndex,
                                     std::span<const RenderGraphDebugImage> debugImages,
                                     std::span<const RenderGraphDebugPass>)
    {
        if (m_PostProcessPass)
            m_PostProcessPass->PostCompile(frameIndex, debugImages);
        if (m_SelectionOutlinePass)
            m_SelectionOutlinePass->PostCompile(frameIndex, debugImages);
        if (m_DebugViewPass)
            m_DebugViewPass->PostCompile(frameIndex, debugImages);
    }

    [[nodiscard]] FrameRecipe BuildDefaultPipelineRecipe(const DefaultPipelineRecipeInputs& inputs)
    {
        FrameRecipe recipe{};

        const bool hasGeometry = inputs.SurfacePassEnabled || inputs.LinePassEnabled || inputs.PointPassEnabled;
        if (hasGeometry)
        {
            recipe.Depth = true;
            recipe.LightingPath = FrameLightingPath::Forward;
        }

        if (inputs.PickingPassEnabled)
        {
            recipe.Depth = true;
            recipe.EntityId = true;
        }

        if (inputs.PostProcessPassEnabled)
        {
            recipe.Post = true;
            recipe.SceneColorLDR = true;
        }

        if (inputs.SelectionOutlinePassEnabled && inputs.HasSelectionWork)
        {
            recipe.Selection = true;
            recipe.EntityId = true;
            recipe.SceneColorLDR = true;
        }

        if (inputs.DebugViewPassEnabled && inputs.DebugViewEnabled)
        {
            recipe.DebugVisualization = true;
            recipe.SceneColorLDR = true;
            if (const auto selected = TryGetRenderResourceByName(inputs.DebugResource))
            {
                switch (*selected)
                {
                case RenderResource::SceneDepth: recipe.Depth = true; break;
                case RenderResource::EntityId: recipe.EntityId = true; break;
                case RenderResource::PrimitiveId: recipe.PrimitiveId = true; break;
                case RenderResource::SceneNormal: recipe.Normals = true; break;
                case RenderResource::Albedo:
                case RenderResource::Material0: recipe.MaterialChannels = true; break;
                case RenderResource::SceneColorHDR:
                    if (recipe.LightingPath == FrameLightingPath::None)
                        recipe.LightingPath = FrameLightingPath::Forward;
                    break;
                case RenderResource::SceneColorLDR: recipe.SceneColorLDR = true; break;
                case RenderResource::SelectionMask:
                case RenderResource::SelectionOutline:
                    recipe.Selection = true;
                    break;
                }
            }
        }

        if (inputs.ImGuiPassEnabled)
            recipe.SceneColorLDR = recipe.SceneColorLDR || inputs.PostProcessPassEnabled;

        return recipe;
    }

    FrameRecipe DefaultPipeline::BuildFrameRecipe(const RenderPassContext& ctx) const
    {
        bool hasSelectionWork = false;
        if (m_SelectionOutlinePass && IsFeatureEnabled("SelectionOutlinePass"_id))
        {
            auto& registry = ctx.Scene.GetRegistry();
            hasSelectionWork = !registry.view<ECS::Components::Selection::SelectedTag>().empty() ||
                               !registry.view<ECS::Components::Selection::HoveredTag>().empty();
        }

        DefaultPipelineRecipeInputs inputs{};
        inputs.PickingPassEnabled = m_PickingPass && IsFeatureEnabled("PickingPass"_id);
        inputs.SurfacePassEnabled = m_SurfacePass && IsFeatureEnabled("SurfacePass"_id);
        inputs.LinePassEnabled = m_LinePass != nullptr;
        inputs.PointPassEnabled = m_PointPass && IsFeatureEnabled("PointPass"_id);
        inputs.PostProcessPassEnabled = m_PostProcessPass && IsFeatureEnabled("PostProcessPass"_id);
        inputs.SelectionOutlinePassEnabled = m_SelectionOutlinePass && IsFeatureEnabled("SelectionOutlinePass"_id);
        inputs.DebugViewPassEnabled = m_DebugViewPass && IsFeatureEnabled("DebugViewPass"_id);
        inputs.ImGuiPassEnabled = m_ImGuiPass && IsFeatureEnabled("ImGuiPass"_id);
        inputs.HasSelectionWork = hasSelectionWork;
        inputs.DebugViewEnabled = ctx.Debug.Enabled;
        inputs.DebugResource = ctx.Debug.SelectedResource;

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
        state.PathDirty = m_PathDirty;

        state.PickingPass = {m_PickingPass != nullptr, m_PickingPass && IsFeatureEnabled("PickingPass"_id)};
        state.SurfacePass = {m_SurfacePass != nullptr, m_SurfacePass && IsFeatureEnabled("SurfacePass"_id)};
        state.SelectionOutlinePass = {m_SelectionOutlinePass != nullptr, m_SelectionOutlinePass && IsFeatureEnabled("SelectionOutlinePass"_id)};
        state.LinePass = {m_LinePass != nullptr, m_LinePass != nullptr};
        state.PointPass = {m_PointPass != nullptr, m_PointPass && IsFeatureEnabled("PointPass"_id)};
        state.PostProcessPass = {m_PostProcessPass != nullptr, m_PostProcessPass && IsFeatureEnabled("PostProcessPass"_id)};
        state.DebugViewPass = {m_DebugViewPass != nullptr, m_DebugViewPass && IsFeatureEnabled("DebugViewPass"_id)};
        state.ImGuiPass = {m_ImGuiPass != nullptr, m_ImGuiPass && IsFeatureEnabled("ImGuiPass"_id)};
        return state;
    }
}
