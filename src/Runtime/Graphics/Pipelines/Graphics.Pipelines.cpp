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

        // 2b. Visualization Collection — unified stage for point clouds,
        // wireframe overlays, and vertex rendering from any entity type.
        //
        // Collects data from three sources:
        //  (a) PointCloudRenderer entities — standard point cloud rendering.
        //  (b) MeshRenderer entities with RenderVisualization::ShowVertices — mesh
        //      vertices rendered as points via PointCloudRenderPass.
        //  (c) MeshRenderer entities with RenderVisualization::ShowWireframe — mesh
        //      edges rendered as lines via DebugDraw → LineRenderPass.
        //
        // This decouples the visual representation from the CPU data type:
        // a mesh can show its surface, wireframe, and vertices independently.
        {
            const bool pointCloudEnabled = m_PointCloudPass && IsFeatureEnabled("PointCloudRenderPass"_id);
            const bool lineEnabled = m_LineRenderPass && IsFeatureEnabled("LineRenderPass"_id);

            if (pointCloudEnabled || lineEnabled)
            {
                m_Path.AddStage("VisualizationCollect", [this, pointCloudEnabled, lineEnabled](RenderPassContext& ctx)
                {
                    if (pointCloudEnabled)
                        m_PointCloudPass->ResetPoints();

                    auto& registry = ctx.Scene.GetRegistry();

                    // --- (a) PointCloudRenderer entities ---
                    if (pointCloudEnabled)
                    {
                        auto pcView = registry.view<ECS::PointCloudRenderer::Component>();
                        for (auto [entity, pc] : pcView.each())
                        {
                            // RenderVisualization overrides Visible when present.
                            bool visible = pc.Visible;
                            if (auto* vis = registry.try_get<ECS::RenderVisualization::Component>(entity))
                                visible = vis->ShowVertices;

                            if (!visible || pc.Positions.empty()) continue;

                            const uint32_t defaultColor = Passes::PointCloudRenderPass::PackColorF(
                                pc.DefaultColor.r, pc.DefaultColor.g, pc.DefaultColor.b, pc.DefaultColor.a);

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
                    }

                    // --- (b) & (c) MeshRenderer entities with visualization overlays ---
                    {
                        auto meshView = registry.view<ECS::MeshRenderer::Component,
                                                      ECS::RenderVisualization::Component>();
                        for (auto [entity, mr, vis] : meshView.each())
                        {
                            if (!vis.ShowWireframe && !vis.ShowVertices)
                                continue;

                            // Need CPU mesh data from collision geometry.
                            auto* collider = registry.try_get<ECS::MeshCollider::Component>(entity);
                            if (!collider || !collider->CollisionRef)
                                continue;

                            const auto& positions = collider->CollisionRef->Positions;
                            const auto& indices = collider->CollisionRef->Indices;
                            if (positions.empty())
                                continue;

                            glm::mat4 worldMatrix(1.0f);
                            if (auto* wm = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity))
                                worldMatrix = wm->Matrix;

                            // Determine topology from GPU geometry data.
                            Graphics::PrimitiveTopology topology = Graphics::PrimitiveTopology::Triangles;
                            GeometryGpuData* geo = ctx.GeometryStorage.GetUnchecked(mr.Geometry);
                            if (geo)
                                topology = geo->GetTopology();

                            // --- Wireframe: extract edges and submit to DebugDraw ---
                            if (vis.ShowWireframe && ctx.DebugDrawPtr && lineEnabled)
                            {
                                // Build edge cache lazily.
                                if (vis.EdgeCacheDirty && !indices.empty())
                                {
                                    vis.CachedEdges.clear();

                                    if (topology == Graphics::PrimitiveTopology::Triangles)
                                    {
                                        // Triangle mesh: extract unique edges from index triplets.
                                        // Use a set keyed by (min, max) to deduplicate.
                                        struct PairHash {
                                            std::size_t operator()(std::pair<uint32_t, uint32_t> p) const noexcept {
                                                return std::hash<uint64_t>{}(
                                                    (uint64_t(p.first) << 32) | uint64_t(p.second));
                                            }
                                        };
                                        std::unordered_set<std::pair<uint32_t, uint32_t>, PairHash> edgeSet;
                                        edgeSet.reserve(indices.size()); // Upper bound

                                        for (std::size_t t = 0; t + 2 < indices.size(); t += 3)
                                        {
                                            uint32_t i0 = indices[t], i1 = indices[t+1], i2 = indices[t+2];
                                            auto addEdge = [&](uint32_t a, uint32_t b) {
                                                auto key = (a < b) ? std::pair{a, b} : std::pair{b, a};
                                                edgeSet.insert(key);
                                            };
                                            addEdge(i0, i1);
                                            addEdge(i1, i2);
                                            addEdge(i2, i0);
                                        }

                                        vis.CachedEdges.assign(edgeSet.begin(), edgeSet.end());
                                    }
                                    else if (topology == Graphics::PrimitiveTopology::Lines)
                                    {
                                        // Line topology: index pairs are already edges.
                                        vis.CachedEdges.reserve(indices.size() / 2);
                                        for (std::size_t e = 0; e + 1 < indices.size(); e += 2)
                                            vis.CachedEdges.emplace_back(indices[e], indices[e+1]);
                                    }

                                    vis.EdgeCacheDirty = false;
                                }

                                // Submit cached edges as DebugDraw lines.
                                const uint32_t wireColor = DebugDraw::PackColorF(
                                    vis.WireframeColor.r, vis.WireframeColor.g,
                                    vis.WireframeColor.b, vis.WireframeColor.a);

                                for (const auto& [i0, i1] : vis.CachedEdges)
                                {
                                    if (i0 >= positions.size() || i1 >= positions.size())
                                        continue;

                                    glm::vec3 a = glm::vec3(worldMatrix * glm::vec4(positions[i0], 1.0f));
                                    glm::vec3 b = glm::vec3(worldMatrix * glm::vec4(positions[i1], 1.0f));

                                    if (vis.WireframeOverlay)
                                        ctx.DebugDrawPtr->OverlayLine(a, b, wireColor);
                                    else
                                        ctx.DebugDrawPtr->Line(a, b, wireColor);
                                }
                            }

                            // --- Vertices: submit positions to PointCloudRenderPass ---
                            if (vis.ShowVertices && pointCloudEnabled)
                            {
                                const uint32_t vtxColor = Passes::PointCloudRenderPass::PackColorF(
                                    vis.VertexColor.r, vis.VertexColor.g,
                                    vis.VertexColor.b, vis.VertexColor.a);

                                for (std::size_t i = 0; i < positions.size(); ++i)
                                {
                                    glm::vec3 worldPos = glm::vec3(worldMatrix * glm::vec4(positions[i], 1.0f));

                                    auto pt = Passes::PointCloudRenderPass::PackPoint(
                                        worldPos.x, worldPos.y, worldPos.z,
                                        0.0f, 1.0f, 0.0f,    // Default up normal for flat disc.
                                        vis.VertexSize,
                                        vtxColor);
                                    m_PointCloudPass->SubmitPoints(&pt, 1);
                                }
                            }
                        }
                    }

                    if (pointCloudEnabled && m_PointCloudPass->HasContent())
                        m_PointCloudPass->AddPasses(ctx);
                });
            }
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
