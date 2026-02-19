module;

#include <cstddef>
#include <memory>
#include <span>
#include <algorithm>
#include <unordered_set>
#include <glm/glm.hpp>
#include <entt/entt.hpp>

module Graphics:Pipelines.Impl;

import :RenderPipeline;
import :RenderGraph;
import :Components;
import :Geometry;
import :DebugDraw;
import :Passes.Picking;
import :Passes.Forward;
import :Passes.Mesh;
import :Passes.Graph;
import :Passes.SelectionOutline;
import :Passes.Line;
import :Passes.PointCloud;
import :Passes.DebugView;
import :Passes.ImGui;
import :PipelineLibrary;
import :ShaderRegistry;
import :Pipelines;
import RHI;
import ECS;
import Core.Hash;
import Core.FeatureRegistry;

using namespace Core::Hash;

namespace Graphics
{
    void DefaultPipeline::Shutdown()
    {
        if (m_PickingPass)          m_PickingPass->Shutdown();
        if (m_ForwardPass)          m_ForwardPass->Shutdown();
        if (m_MeshPass)             m_MeshPass->Shutdown();
        if (m_GraphPass)            m_GraphPass->Shutdown();
        if (m_SelectionOutlinePass) m_SelectionOutlinePass->Shutdown();
        if (m_LineRenderPass)       m_LineRenderPass->Shutdown();
        if (m_PointCloudPass)       m_PointCloudPass->Shutdown();
        if (m_DebugViewPass)        m_DebugViewPass->Shutdown();
        if (m_ImGuiPass)            m_ImGuiPass->Shutdown();

        m_PickingPass.reset();
        m_ForwardPass.reset();
        m_MeshPass.reset();
        m_GraphPass.reset();
        m_SelectionOutlinePass.reset();
        m_LineRenderPass.reset();
        m_PointCloudPass.reset();
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
        m_ForwardPass          = std::make_unique<Passes::ForwardPass>();
        m_MeshPass             = std::make_unique<Passes::MeshRenderPass>();
        m_GraphPass            = std::make_unique<Passes::GraphRenderPass>();
        m_SelectionOutlinePass = std::make_unique<Passes::SelectionOutlinePass>();
        m_LineRenderPass       = std::make_unique<Passes::LineRenderPass>();
        m_PointCloudPass       = std::make_unique<Passes::PointCloudRenderPass>();
        m_DebugViewPass        = std::make_unique<Passes::DebugViewPass>();
        m_ImGuiPass            = std::make_unique<Passes::ImGuiPass>();

        m_PickingPass->Initialize(device, descriptorPool, globalLayout);
        m_ForwardPass->Initialize(device, descriptorPool, globalLayout);
        m_MeshPass->Initialize(device, descriptorPool, globalLayout);
        m_GraphPass->Initialize(device, descriptorPool, globalLayout);
        m_SelectionOutlinePass->Initialize(device, descriptorPool, globalLayout);
        m_LineRenderPass->Initialize(device, descriptorPool, globalLayout);
        m_PointCloudPass->Initialize(device, descriptorPool, globalLayout);
        m_DebugViewPass->Initialize(device, descriptorPool, globalLayout);
        m_ImGuiPass->Initialize(device, descriptorPool, globalLayout);

        m_PickingPass->SetPipeline(&pipelineLibrary.GetOrDie(kPipeline_Picking));

        m_ForwardPass->SetPipeline(&pipelineLibrary.GetOrDie(kPipeline_Forward));
        m_ForwardPass->SetLinePipeline(&pipelineLibrary.GetOrDie(kPipeline_ForwardLines));
        m_ForwardPass->SetPointPipeline(&pipelineLibrary.GetOrDie(kPipeline_ForwardPoints));
        m_ForwardPass->SetInstanceSetLayout(pipelineLibrary.GetStage1InstanceSetLayout());
        m_ForwardPass->SetCullPipeline(pipelineLibrary.GetCullPipeline());
        m_ForwardPass->SetCullSetLayout(pipelineLibrary.GetCullSetLayout());

        m_SelectionOutlinePass->SetShaderRegistry(shaderRegistry);
        m_LineRenderPass->SetShaderRegistry(shaderRegistry);
        m_PointCloudPass->SetShaderRegistry(shaderRegistry);
        m_DebugViewPass->SetShaderRegistry(shaderRegistry);
        
        m_PathDirty = true;
    }

    bool DefaultPipeline::IsFeatureEnabled(Core::Hash::StringID id) const
    {
        if (!m_Registry) return true; // No registry → all features enabled
        return m_Registry->IsEnabled(id);
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
        // 2. Mesh Pass — face rendering via ForwardPass (triangles / lines /
        //    point geometry).  ForwardPass is the "surface" sub-stage of the
        //    mesh pass; wireframe + vertex overlays follow in MeshPass.Viz.
        // ==================================================================
        if (m_ForwardPass && IsFeatureEnabled("ForwardPass"_id))
            m_Path.AddFeature("MeshPass.Forward", m_ForwardPass.get());

        // ==================================================================
        // Visualization collection
        // ==================================================================
        //
        // Three collection passes feed two shared GPU primitive renderers:
        //   - MeshRenderPass  → wireframe edges + vertex splats from mesh entities.
        //   - GraphRenderPass → edge lines + node splats from graph entities.
        //   - PointCloud collect inline → splats from PointCloudRenderer entities.
        //
        // Then the GPU draw passes render all accumulated data:
        //   - PointCloudRenderPass.AddPasses()  — draws all accumulated splats.
        //   - LineRenderPass.AddPasses()         — draws all accumulated lines.
        //
        // Execution contract: collectors must run after ResetPoints() and before
        // the GPU draw passes are added to the render graph.
        {
            const bool pcEnabled    = m_PointCloudPass && IsFeatureEnabled("PointCloudRenderPass"_id);
            const bool lineEnabled  = m_LineRenderPass  && IsFeatureEnabled("LineRenderPass"_id);
            const bool meshEnabled  = m_MeshPass        && IsFeatureEnabled("MeshPass"_id);
            const bool graphEnabled = m_GraphPass       && IsFeatureEnabled("GraphPass"_id);

            if (pcEnabled || lineEnabled || meshEnabled || graphEnabled)
            {
                m_Path.AddStage("VisualizationCollect",
                    [this, pcEnabled, lineEnabled, meshEnabled, graphEnabled](RenderPassContext& ctx)
                {
                    // Reset point splat staging before any collector runs.
                    if (pcEnabled)
                        m_PointCloudPass->ResetPoints();

                    // ----------------------------------------------------------
                    // 3. Mesh Pass — visualization overlays (wireframe + vertices)
                    // ----------------------------------------------------------
                    // MeshRenderPass extracts edges (→ DebugDraw) and vertex
                    // positions (→ PointCloudRenderPass) from MeshRenderer entities
                    // that have a RenderVisualization component.
                    if (meshEnabled)
                    {
                        m_MeshPass->SetPointCloudPass(pcEnabled ? m_PointCloudPass.get() : nullptr);
                        m_MeshPass->AddPasses(ctx);
                    }

                    // ----------------------------------------------------------
                    // 4. Graph Pass — node splats + edge lines from graph entities
                    // ----------------------------------------------------------
                    // GraphRenderPass submits node positions (→ PointCloudRenderPass)
                    // and edge segments (→ DebugDraw) for GraphRenderer entities.
                    if (graphEnabled)
                    {
                        m_GraphPass->SetPointCloudPass(pcEnabled ? m_PointCloudPass.get() : nullptr);
                        m_GraphPass->AddPasses(ctx);
                    }

                    // ----------------------------------------------------------
                    // 5. Point Cloud Pass — collect PointCloudRenderer entities
                    // ----------------------------------------------------------
                    // Inline collection: iterate PointCloudRenderer::Component entities
                    // and submit their points to the shared staging buffers, then add
                    // the GPU draw pass for all accumulated splats.
                    if (pcEnabled)
                    {
                        auto& registry = ctx.Scene.GetRegistry();
                        auto pcView = registry.view<ECS::PointCloudRenderer::Component>();

                        for (auto [entity, pc] : pcView.each())
                        {
                            // RenderVisualization can override point cloud visibility.
                            bool visible = pc.Visible;
                            if (auto* vis = registry.try_get<ECS::RenderVisualization::Component>(entity))
                                visible = vis->ShowVertices;

                            if (!visible || pc.Positions.empty())
                                continue;

                            const uint32_t defaultColor = Passes::PointCloudRenderPass::PackColorF(
                                pc.DefaultColor.r, pc.DefaultColor.g,
                                pc.DefaultColor.b, pc.DefaultColor.a);

                            glm::mat4 worldMatrix(1.0f);
                            if (auto* wm = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity))
                                worldMatrix = wm->Matrix;

                            for (std::size_t i = 0; i < pc.Positions.size(); ++i)
                            {
                                const glm::vec3 worldPos =
                                    glm::vec3(worldMatrix * glm::vec4(pc.Positions[i], 1.0f));
                                const glm::vec3 normal = pc.HasNormals()
                                    ? glm::normalize(glm::mat3(worldMatrix) * pc.Normals[i])
                                    : glm::vec3(0.0f, 1.0f, 0.0f);
                                const float radius = pc.HasRadii() ? pc.Radii[i] : pc.DefaultRadius;
                                const uint32_t color = pc.HasColors()
                                    ? Passes::PointCloudRenderPass::PackColorF(
                                        pc.Colors[i].r, pc.Colors[i].g,
                                        pc.Colors[i].b, pc.Colors[i].a)
                                    : defaultColor;

                                auto pt = Passes::PointCloudRenderPass::PackPoint(
                                    worldPos.x, worldPos.y, worldPos.z,
                                    normal.x, normal.y, normal.z,
                                    radius * pc.SizeMultiplier,
                                    color);
                                m_PointCloudPass->SubmitPoints(pc.RenderMode, &pt, 1);
                            }
                        }

                        // GPU draw: add a render-graph pass for every non-empty mode bucket.
                        if (m_PointCloudPass->HasContent())
                            m_PointCloudPass->AddPasses(ctx);
                    }
                }); // end VisualizationCollect stage
            }
        }

        // ==================================================================
        // 6. Selection Outline — post-process overlay for selected entities.
        // ==================================================================
        if (m_SelectionOutlinePass && IsFeatureEnabled("SelectionOutlinePass"_id))
            m_Path.AddFeature("SelectionOutline", m_SelectionOutlinePass.get());

        // ==================================================================
        // 7. Line Pass — GPU draw for all lines in DebugDraw accumulator.
        //    Consumes lines submitted by MeshRenderPass (wireframe), GraphRenderPass
        //    (edges), and any direct DebugDraw submissions from systems/tools.
        // ==================================================================
        if (m_LineRenderPass && IsFeatureEnabled("LineRenderPass"_id))
        {
            m_Path.AddStage("LinePass", [this](RenderPassContext& ctx)
            {
                if (ctx.DebugDrawPtr)
                {
                    m_LineRenderPass->SetDebugDraw(ctx.DebugDrawPtr);
                    m_LineRenderPass->AddPasses(ctx);
                }
            });
        }

        // ==================================================================
        // 8. Debug View — conditional texture inspector overlay.
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
        // 9. ImGui — editor UI overlay.
        // ==================================================================
        if (m_ImGuiPass && IsFeatureEnabled("ImGuiPass"_id))
            m_Path.AddFeature("ImGui", m_ImGuiPass.get());
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
        if (m_ForwardPass)          m_ForwardPass->OnResize(width, height);
        if (m_MeshPass)             m_MeshPass->OnResize(width, height);
        if (m_GraphPass)            m_GraphPass->OnResize(width, height);
        if (m_SelectionOutlinePass) m_SelectionOutlinePass->OnResize(width, height);
        if (m_LineRenderPass)       m_LineRenderPass->OnResize(width, height);
        if (m_PointCloudPass)       m_PointCloudPass->OnResize(width, height);
        if (m_DebugViewPass)        m_DebugViewPass->OnResize(width, height);
        if (m_ImGuiPass)            m_ImGuiPass->OnResize(width, height);
    }

    void DefaultPipeline::PostCompile(uint32_t frameIndex,
                                     std::span<const RenderGraphDebugImage> debugImages,
                                     std::span<const RenderGraphDebugPass>)
    {
        if (m_SelectionOutlinePass)
            m_SelectionOutlinePass->PostCompile(frameIndex, debugImages);
        if (m_DebugViewPass)
            m_DebugViewPass->PostCompile(frameIndex, debugImages);
    }
}
