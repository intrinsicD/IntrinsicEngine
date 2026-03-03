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
import ECS;
import Geometry;

namespace Graphics::Passes
{

    // =========================================================================
    // AddPasses
    // =========================================================================
    //
    // Iterates all ECS::Graph::Data entities and submits node positions
    // (transformed to world space) to PointCloudRenderPass.
    //
    // Edge rendering is fully owned by LinePass which iterates
    // ECS::Line::Component directly — no CPU edge submission here.
    //
    // When the retained point pass is active, entities with valid GpuGeometry
    // are skipped — PointPass handles them via BDA.

    void GraphRenderPass::AddPasses(RenderPassContext& ctx)
    {
        if (!m_PointCloudPass)
            return;

        auto& registry = ctx.Scene.GetRegistry();
        auto graphView = registry.view<const ECS::Graph::Data>();

        graphView.each([&](const entt::entity entity, const ECS::Graph::Data& graphData)
        {
            if (!graphData.Visible || !graphData.GraphRef || graphData.GraphRef->VertexCount() == 0)
                return;

            // Skip entities that the retained point pass will handle.
            const bool hasRetainedGpu = graphData.GpuGeometry.IsValid();
            const bool retainedHandlesNodes = hasRetainedGpu && m_RetainedPointsActive;

            if (retainedHandlesNodes)
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

            std::optional<Geometry::VertexProperty<glm::vec4>> colorProp;
            std::optional<Geometry::VertexProperty<float>> radiusProp;
            if (hasColors)
                colorProp = const_cast<Geometry::Graph::Graph&>(graph)
                    .GetOrAddVertexProperty<glm::vec4>("v:color");
            if (hasRadii)
                radiusProp = const_cast<Geometry::Graph::Graph&>(graph)
                    .GetOrAddVertexProperty<float>("v:radius");

            // --- Submit nodes to PointCloudRenderPass (CPU fallback) ---
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

                auto pt = PointCloudRenderPass::PackPoint(
                    worldPos.x, worldPos.y, worldPos.z,
                    0.0f, 1.0f, 0.0f,
                    radius * graphData.NodeSizeMultiplier,
                    color);
                m_PointCloudPass->SubmitPoints(graphData.NodeRenderMode, &pt, 1);
            }
        });
    }

} // namespace Graphics::Passes
