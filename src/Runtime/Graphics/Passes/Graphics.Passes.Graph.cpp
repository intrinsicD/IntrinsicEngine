module;

#include <cstdint>
#include <glm/glm.hpp>

module Graphics:Passes.Graph.Impl;

import :Passes.Graph;
import :RenderPipeline;
import :Components;
import :Passes.PointCloud;
import :DebugDraw;
import ECS;

namespace Graphics::Passes
{

    // =========================================================================
    // AddPasses
    // =========================================================================
    //
    // Iterates all ECS::GraphRenderer::Component entities and:
    //   - Submits node positions (transformed to world space) to PointCloudRenderPass.
    //   - Submits edge segments (index pairs) to ctx.DebugDrawPtr.
    //
    // This method does NOT add any GPU render graph passes. Actual GPU drawing is
    // performed by PointCloudRenderPass::AddPasses() and LineRenderPass::AddPasses()
    // after all collection stages complete.

    void GraphRenderPass::AddPasses(RenderPassContext& ctx)
    {
        auto& registry = ctx.Scene.GetRegistry();
        auto graphView = registry.view<ECS::GraphRenderer::Component>();

        for (auto [entity, graph] : graphView.each())
        {
            if (!graph.Visible || graph.NodePositions.empty())
                continue;

            // Fetch world transform (identity if not present).
            glm::mat4 worldMatrix(1.0f);
            if (auto* wm = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity))
                worldMatrix = wm->Matrix;

            // --- Submit nodes to PointCloudRenderPass ---
            if (m_PointCloudPass)
            {
                const uint32_t defaultColor = PointCloudRenderPass::PackColorF(
                    graph.DefaultNodeColor.r, graph.DefaultNodeColor.g,
                    graph.DefaultNodeColor.b, graph.DefaultNodeColor.a);

                for (std::size_t i = 0; i < graph.NodePositions.size(); ++i)
                {
                    const glm::vec3 worldPos =
                        glm::vec3(worldMatrix * glm::vec4(graph.NodePositions[i], 1.0f));

                    const float radius = graph.HasNodeRadii()
                        ? graph.NodeRadii[i]
                        : graph.DefaultNodeRadius;

                    const uint32_t color = graph.HasNodeColors()
                        ? PointCloudRenderPass::PackColorF(
                            graph.NodeColors[i].r, graph.NodeColors[i].g,
                            graph.NodeColors[i].b, graph.NodeColors[i].a)
                        : defaultColor;

                    // Nodes have no meaningful surface normal — use world-up as default.
                    auto pt = PointCloudRenderPass::PackPoint(
                        worldPos.x, worldPos.y, worldPos.z,
                        0.0f, 1.0f, 0.0f,
                        radius * graph.NodeSizeMultiplier,
                        color);
                    m_PointCloudPass->SubmitPoints(graph.NodeRenderMode, &pt, 1);
                }
            }

            // --- Submit edges to DebugDraw (→ LineRenderPass) ---
            if (ctx.DebugDrawPtr && !graph.Edges.empty())
            {
                const uint32_t edgeColor = DebugDraw::PackColorF(
                    graph.DefaultEdgeColor.r, graph.DefaultEdgeColor.g,
                    graph.DefaultEdgeColor.b, graph.DefaultEdgeColor.a);

                for (const auto& [i0, i1] : graph.Edges)
                {
                    if (i0 >= graph.NodePositions.size() || i1 >= graph.NodePositions.size())
                        continue;

                    const glm::vec3 a =
                        glm::vec3(worldMatrix * glm::vec4(graph.NodePositions[i0], 1.0f));
                    const glm::vec3 b =
                        glm::vec3(worldMatrix * glm::vec4(graph.NodePositions[i1], 1.0f));

                    if (graph.EdgesOverlay)
                        ctx.DebugDrawPtr->OverlayLine(a, b, edgeColor);
                    else
                        ctx.DebugDrawPtr->Line(a, b, edgeColor);
                }
            }
        }
    }

} // namespace Graphics::Passes
