module;

#include <cstddef>
#include <memory>
#include <span>
#include <glm/glm.hpp>
#include <entt/entt.hpp>

module Graphics:Pipelines.Impl;

import :RenderPipeline;
import :RenderGraph;
import :Components;
import :Passes.Picking;
import :Passes.Forward;
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
        if (m_PickingPass) m_PickingPass->Shutdown();
        if (m_ForwardPass) m_ForwardPass->Shutdown();
        if (m_SelectionOutlinePass) m_SelectionOutlinePass->Shutdown();
        if (m_LineRenderPass) m_LineRenderPass->Shutdown();
        if (m_PointCloudPass) m_PointCloudPass->Shutdown();
        if (m_DebugViewPass) m_DebugViewPass->Shutdown();
        if (m_ImGuiPass) m_ImGuiPass->Shutdown();

        m_PickingPass.reset();
        m_ForwardPass.reset();
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
        m_PickingPass = std::make_unique<Passes::PickingPass>();
        m_ForwardPass = std::make_unique<Passes::ForwardPass>();
        m_SelectionOutlinePass = std::make_unique<Passes::SelectionOutlinePass>();
        m_LineRenderPass = std::make_unique<Passes::LineRenderPass>();
        m_PointCloudPass = std::make_unique<Passes::PointCloudRenderPass>();
        m_DebugViewPass = std::make_unique<Passes::DebugViewPass>();
        m_ImGuiPass = std::make_unique<Passes::ImGuiPass>();

        m_PickingPass->Initialize(device, descriptorPool, globalLayout);
        m_ForwardPass->Initialize(device, descriptorPool, globalLayout);
        m_SelectionOutlinePass->Initialize(device, descriptorPool, globalLayout);
        m_LineRenderPass->Initialize(device, descriptorPool, globalLayout);
        m_PointCloudPass->Initialize(device, descriptorPool, globalLayout);
        m_DebugViewPass->Initialize(device, descriptorPool, globalLayout);
        m_ImGuiPass->Initialize(device, descriptorPool, globalLayout);

        m_PickingPass->SetPipeline(&pipelineLibrary.GetOrDie(kPipeline_Picking));

        m_ForwardPass->SetPipeline(&pipelineLibrary.GetOrDie(kPipeline_Forward));
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

        // 1. Picking (Readback) — gated by FeatureRegistry
        if (m_PickingPass && IsFeatureEnabled("PickingPass"_id))
            m_Path.AddFeature("Picking", m_PickingPass.get());

        // 2. Forward (Main Scene) — gated by FeatureRegistry
        if (m_ForwardPass && IsFeatureEnabled("ForwardPass"_id))
            m_Path.AddFeature("Forward", m_ForwardPass.get());

        // 2b. Point Cloud Rendering — gated by FeatureRegistry.
        // Collects point cloud data from ECS PointCloudRenderer components,
        // uploads to SSBO, and renders billboard/surfel/EWA splats.
        if (m_PointCloudPass && IsFeatureEnabled("PointCloudRenderPass"_id))
        {
            m_Path.AddStage("PointCloud", [this](RenderPassContext& ctx)
            {
                // Reset and collect point data from all PointCloudRenderer entities.
                m_PointCloudPass->ResetPoints();

                auto& registry = ctx.Scene.GetRegistry();
                auto view = registry.view<ECS::PointCloudRenderer::Component>();
                for (auto [entity, pc] : view.each())
                {
                    if (!pc.Visible || pc.Positions.empty()) continue;

                    const uint32_t defaultColor = Passes::PointCloudRenderPass::PackColorF(
                        pc.DefaultColor.r, pc.DefaultColor.g, pc.DefaultColor.b, pc.DefaultColor.a);

                    // Get world transform if available.
                    glm::mat4 worldMatrix(1.0f);
                    if (auto* wm = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity))
                        worldMatrix = wm->Matrix;

                    for (std::size_t i = 0; i < pc.Positions.size(); ++i)
                    {
                        glm::vec3 worldPos = glm::vec3(worldMatrix * glm::vec4(pc.Positions[i], 1.0f));
                        glm::vec3 normal = pc.HasNormals()
                            ? glm::normalize(glm::mat3(worldMatrix) * pc.Normals[i])
                            : glm::vec3(0.0f, 1.0f, 0.0f);
                        float radius = pc.HasRadii() ? pc.Radii[i] : pc.DefaultRadius;
                        uint32_t color = pc.HasColors()
                            ? Passes::PointCloudRenderPass::PackColorF(
                                pc.Colors[i].r, pc.Colors[i].g, pc.Colors[i].b, pc.Colors[i].a)
                            : defaultColor;

                        auto pt = Passes::PointCloudRenderPass::PackPoint(
                            worldPos.x, worldPos.y, worldPos.z,
                            normal.x, normal.y, normal.z,
                            radius * pc.SizeMultiplier,
                            color);
                        m_PointCloudPass->SubmitPoints(&pt, 1);
                    }
                }

                if (m_PointCloudPass->HasContent())
                    m_PointCloudPass->AddPasses(ctx);
            });
        }

        // 3. Selection Outline (post-process overlay on selected/hovered entities)
        if (m_SelectionOutlinePass && IsFeatureEnabled("SelectionOutlinePass"_id))
            m_Path.AddFeature("SelectionOutline", m_SelectionOutlinePass.get());

        // 4. Debug Lines (depth-tested + overlay, consumes DebugDraw accumulator)
        if (m_LineRenderPass && IsFeatureEnabled("LineRenderPass"_id))
        {
            m_Path.AddStage("DebugLines", [this](RenderPassContext& ctx)
            {
                if (ctx.DebugDrawPtr)
                {
                    m_LineRenderPass->SetDebugDraw(ctx.DebugDrawPtr);
                    m_LineRenderPass->AddPasses(ctx);
                }
            });
        }

        // 5. Debug View (Conditional on both registry and per-frame debug state)
        if (m_DebugViewPass && IsFeatureEnabled("DebugViewPass"_id))
        {
            m_Path.AddStage("DebugView", [this](RenderPassContext& ctx)
            {
                if (ctx.Debug.Enabled)
                {
                    m_DebugViewPass->AddPasses(ctx);
                }
            });
        }

        // 6. ImGui (Overlay) — gated by FeatureRegistry
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
        if (m_PickingPass) m_PickingPass->OnResize(width, height);
        if (m_ForwardPass) m_ForwardPass->OnResize(width, height);
        if (m_SelectionOutlinePass) m_SelectionOutlinePass->OnResize(width, height);
        if (m_LineRenderPass) m_LineRenderPass->OnResize(width, height);
        if (m_PointCloudPass) m_PointCloudPass->OnResize(width, height);
        if (m_DebugViewPass) m_DebugViewPass->OnResize(width, height);
        if (m_ImGuiPass) m_ImGuiPass->OnResize(width, height);
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
