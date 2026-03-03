module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <glm/glm.hpp>
#include <entt/entity/entity.hpp>

module Graphics:Passes.Graph.Impl;

import :Passes.Graph;
import :RenderPipeline;
import :Components;
import :Passes.PointCloud;
import :DebugDraw;
import ECS;
import Geometry;

namespace Graphics::Passes
{

    // =========================================================================
    // AddPasses
    // =========================================================================
    //
    // Iterates all ECS::Graph::Data entities and:
    //   - Submits node positions (transformed to world space) to PointCloudRenderPass.
    //   - Submits edge segments to ctx.DebugDrawPtr.
    //
    // When retained-mode passes are active, entities with valid GpuGeometry are
    // skipped — the LinePass and RetainedPointCloudRenderPass
    // handle them via BDA. Only entities that lack GPU state (e.g., before
    // GraphGeometrySyncSystem runs) fall through to this CPU path.
    //
    // This method does NOT add any GPU render graph passes. Actual GPU drawing is
    // performed by PointCloudRenderPass::AddPasses() and LinePass::AddPasses()
    // after all collection stages complete.

    void GraphRenderPass::AddPasses(RenderPassContext& ctx)
    {
        auto& registry = ctx.Scene.GetRegistry();
        auto graphView = registry.view<const ECS::Graph::Data>();

        graphView.each([&](const entt::entity entity, const ECS::Graph::Data& graphData)
        {
            if (!graphData.Visible || !graphData.GraphRef || graphData.GraphRef->VertexCount() == 0)
                return;

            // Skip entities that the retained BDA passes will handle.
            // An entity has retained GPU state when GpuGeometry is valid (uploaded by
            // GraphGeometrySyncSystem). Only fall through to the CPU path if the
            // retained pass for that primitive type is disabled.
            const bool hasRetainedGpu = graphData.GpuGeometry.IsValid();
            const bool retainedHandlesEdges = hasRetainedGpu && m_RetainedLinesActive;
            const bool retainedHandlesNodes = hasRetainedGpu && m_RetainedPointsActive;

            // If both primitive types are handled by retained passes, skip entirely.
            if (retainedHandlesEdges && retainedHandlesNodes)
                return;

            const auto& graph = *graphData.GraphRef;

            // Fetch world transform (identity if not present).
            glm::mat4 worldMatrix(1.0f);
            if (auto* wm = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity))
                worldMatrix = wm->Matrix;

            // Resolve optional per-node properties from PropertySets.
            const auto& vProps = graph.VertexProperties();
            const bool hasColors = vProps.Exists("v:color");
            const bool hasRadii  = vProps.Exists("v:radius");

            // Cache property accessors outside the vertex loop (avoids per-vertex lookup).
            // GetOrAdd is used only when the property is confirmed to exist.
            // We const_cast because GetOrAddVertexProperty is non-const, but we
            // only read — the property already exists per the Exists() check above.
            std::optional<Geometry::VertexProperty<glm::vec4>> colorProp;
            std::optional<Geometry::VertexProperty<float>> radiusProp;
            if (hasColors)
                colorProp = const_cast<Geometry::Graph::Graph&>(graph)
                    .GetOrAddVertexProperty<glm::vec4>("v:color");
            if (hasRadii)
                radiusProp = const_cast<Geometry::Graph::Graph&>(graph)
                    .GetOrAddVertexProperty<float>("v:radius");

            // --- Submit nodes to PointCloudRenderPass (CPU fallback) ---
            if (m_PointCloudPass && !retainedHandlesNodes)
            {
                const uint32_t defaultColor = PointCloudRenderPass::PackColorF(
                    graphData.DefaultNodeColor.r, graphData.DefaultNodeColor.g,
                    graphData.DefaultNodeColor.b, graphData.DefaultNodeColor.a);

                const std::size_t vSize = graph.VerticesSize();
                for (std::size_t i = 0; i < vSize; ++i)
                {
                    const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
                    if (graph.IsDeleted(v))
                        continue;

                    const glm::vec3 localPos = graph.VertexPosition(v);
                    const glm::vec3 worldPos =
                        glm::vec3(worldMatrix * glm::vec4(localPos, 1.0f));

                    const float radius = (hasRadii && radiusProp)
                        ? (*radiusProp)[v]
                        : graphData.DefaultNodeRadius;

                    const uint32_t color = (hasColors && colorProp)
                        ? PointCloudRenderPass::PackColorF(
                            (*colorProp)[v].r, (*colorProp)[v].g,
                            (*colorProp)[v].b, (*colorProp)[v].a)
                        : defaultColor;

                    // Nodes have no meaningful surface normal — use world-up as default.
                    auto pt = PointCloudRenderPass::PackPoint(
                        worldPos.x, worldPos.y, worldPos.z,
                        0.0f, 1.0f, 0.0f,
                        radius * graphData.NodeSizeMultiplier,
                        color);
                    m_PointCloudPass->SubmitPoints(graphData.NodeRenderMode, &pt, 1);
                }
            }

            // --- Submit edges to DebugDraw (CPU fallback → LinePass) ---
            if (ctx.DebugDrawPtr && graph.EdgeCount() > 0 && !retainedHandlesEdges)
            {
                const uint32_t edgeColor = DebugDraw::PackColorF(
                    graphData.DefaultEdgeColor.r, graphData.DefaultEdgeColor.g,
                    graphData.DefaultEdgeColor.b, graphData.DefaultEdgeColor.a);

                const std::size_t eSize = graph.EdgesSize();
                for (std::size_t i = 0; i < eSize; ++i)
                {
                    const Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
                    if (graph.IsDeleted(e))
                        continue;

                    const auto [v0, v1] = graph.EdgeVertices(e);
                    const glm::vec3 a =
                        glm::vec3(worldMatrix * glm::vec4(graph.VertexPosition(v0), 1.0f));
                    const glm::vec3 b =
                        glm::vec3(worldMatrix * glm::vec4(graph.VertexPosition(v1), 1.0f));

                    if (graphData.EdgesOverlay)
                        ctx.DebugDrawPtr->OverlayLine(a, b, edgeColor);
                    else
                        ctx.DebugDrawPtr->Line(a, b, edgeColor);
                }
            }
        });
    }

} // namespace Graphics::Passes
