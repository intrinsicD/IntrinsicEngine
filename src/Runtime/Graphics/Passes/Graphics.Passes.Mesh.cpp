module;

#include <cstdint>
#include <unordered_set>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

module Graphics:Passes.Mesh.Impl;

import :Passes.Mesh;
import :RenderPipeline;
import :Components;
import :Passes.PointCloud;
import :DebugDraw;
import :Geometry;
import ECS;

namespace Graphics::Passes
{

    // =========================================================================
    // AddPasses
    // =========================================================================
    //
    // Iterates all entities with MeshRenderer::Component + RenderVisualization::Component
    // and performs CPU-side data collection for:
    //
    //   ShowWireframe:
    //     Extracts unique edges from the collision mesh (lazily cached per entity)
    //     and submits them to ctx.DebugDrawPtr (→ LineRenderPass GPU draw).
    //     Uses OverlayLine when vis.WireframeOverlay is true.
    //
    //   ShowVertices:
    //     Submits mesh vertex positions (with area-weighted normals for surfel/EWA modes)
    //     to PointCloudRenderPass staging buffers.
    //
    // Entities for which GPU-side derived geometry views already exist
    // (vis.WireframeView.IsValid() / vis.VertexView.IsValid()) skip the CPU path —
    // their rendering is handled by ForwardPass via GPUScene geometry instances.

    void MeshRenderPass::AddPasses(RenderPassContext& ctx)
    {
        const bool canDrawPoints = (m_PointCloudPass != nullptr);
        const bool canDrawLines  = (ctx.DebugDrawPtr != nullptr);

        if (!canDrawPoints && !canDrawLines)
            return;

        auto& registry = ctx.Scene.GetRegistry();
        auto meshView  = registry.view<ECS::MeshRenderer::Component,
                                       ECS::RenderVisualization::Component>();

        for (auto [entity, mr, vis] : meshView.each())
        {
            if (!vis.ShowWireframe && !vis.ShowVertices)
                continue;

            const bool hasGpuWire  = vis.WireframeView.IsValid();
            const bool hasGpuVerts = vis.VertexView.IsValid();

            // If GPU views exist for both requested modes, skip the CPU path entirely.
            {
                const bool needsCpuWire  = vis.ShowWireframe && !hasGpuWire;
                const bool needsCpuVerts = vis.ShowVertices  && !hasGpuVerts;
                if (!needsCpuWire && !needsCpuVerts)
                    continue;
            }

            // CPU path requires collision mesh data (positions + indices).
            auto* collider = registry.try_get<ECS::MeshCollider::Component>(entity);
            if (!collider || !collider->CollisionRef)
                continue;

            const auto& positions = collider->CollisionRef->Positions;
            const auto& indices   = collider->CollisionRef->Indices;
            if (positions.empty())
                continue;

            glm::mat4 worldMatrix(1.0f);
            if (auto* wm = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity))
                worldMatrix = wm->Matrix;

            // Determine topology from GPU geometry.
            Graphics::PrimitiveTopology topology = Graphics::PrimitiveTopology::Triangles;
            GeometryGpuData* geo = ctx.GeometryStorage.GetUnchecked(mr.Geometry);
            if (geo)
                topology = geo->GetTopology();

            // ------------------------------------------------------------------
            // Wireframe: extract unique edges → DebugDraw → LineRenderPass
            // ------------------------------------------------------------------
            const bool doCpuWireframe = vis.ShowWireframe && !hasGpuWire && canDrawLines;
            if (doCpuWireframe && !indices.empty())
            {
                // Build edge cache lazily (invalidated by EdgeCacheDirty flag).
                if (vis.EdgeCacheDirty)
                {
                    vis.CachedEdges.clear();

                    if (topology == Graphics::PrimitiveTopology::Triangles)
                    {
                        struct PairHash {
                            std::size_t operator()(std::pair<uint32_t, uint32_t> p) const noexcept {
                                return std::hash<uint64_t>{}(
                                    (uint64_t(p.first) << 32) | uint64_t(p.second));
                            }
                        };
                        std::unordered_set<std::pair<uint32_t, uint32_t>, PairHash> edgeSet;
                        edgeSet.reserve(indices.size());

                        for (std::size_t t = 0; t + 2 < indices.size(); t += 3)
                        {
                            const uint32_t i0 = indices[t], i1 = indices[t + 1], i2 = indices[t + 2];
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
                        vis.CachedEdges.reserve(indices.size() / 2);
                        for (std::size_t e = 0; e + 1 < indices.size(); e += 2)
                            vis.CachedEdges.emplace_back(indices[e], indices[e + 1]);
                    }

                    vis.EdgeCacheDirty = false;
                }

                const uint32_t wireColor = DebugDraw::PackColorF(
                    vis.WireframeColor.r, vis.WireframeColor.g,
                    vis.WireframeColor.b, vis.WireframeColor.a);

                for (const auto& [i0, i1] : vis.CachedEdges)
                {
                    if (i0 >= positions.size() || i1 >= positions.size())
                        continue;

                    const glm::vec3 a =
                        glm::vec3(worldMatrix * glm::vec4(positions[i0], 1.0f));
                    const glm::vec3 b =
                        glm::vec3(worldMatrix * glm::vec4(positions[i1], 1.0f));

                    if (vis.WireframeOverlay)
                        ctx.DebugDrawPtr->OverlayLine(a, b, wireColor);
                    else
                        ctx.DebugDrawPtr->Line(a, b, wireColor);
                }
            }

            // ------------------------------------------------------------------
            // Vertices: submit positions to PointCloudRenderPass
            // ------------------------------------------------------------------
            const bool doCpuVertices = vis.ShowVertices && !hasGpuVerts && canDrawPoints;
            if (doCpuVertices)
            {
                const bool wantsAligned =
                    (vis.VertexRenderMode == Geometry::PointCloud::RenderMode::Surfel) ||
                    (vis.VertexRenderMode == Geometry::PointCloud::RenderMode::EWA);

                // Compute area-weighted vertex normals lazily (needed for surfel/EWA).
                if (vis.VertexNormalsDirty
                    && wantsAligned
                    && topology == Graphics::PrimitiveTopology::Triangles
                    && !indices.empty())
                {
                    vis.CachedVertexNormals.assign(positions.size(), glm::vec3(0.0f));
                    for (std::size_t t = 0; t + 2 < indices.size(); t += 3)
                    {
                        const uint32_t i0 = indices[t], i1 = indices[t + 1], i2 = indices[t + 2];
                        if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size())
                            continue;
                        const glm::vec3 e1 = positions[i1] - positions[i0];
                        const glm::vec3 e2 = positions[i2] - positions[i0];
                        const glm::vec3 fn = glm::cross(e1, e2); // magnitude = 2*area
                        vis.CachedVertexNormals[i0] += fn;
                        vis.CachedVertexNormals[i1] += fn;
                        vis.CachedVertexNormals[i2] += fn;
                    }
                    for (auto& vn : vis.CachedVertexNormals)
                    {
                        const float len = glm::length(vn);
                        vn = (len > 1e-12f) ? (vn / len) : glm::vec3(0.0f, 1.0f, 0.0f);
                    }
                    vis.VertexNormalsDirty = false;
                }

                const uint32_t vtxColor = PointCloudRenderPass::PackColorF(
                    vis.VertexColor.r, vis.VertexColor.g,
                    vis.VertexColor.b, vis.VertexColor.a);

                // Inverse-transpose of the linear part for correct normal transform under non-uniform scale.
                const glm::mat3 normalMatrix =
                    glm::transpose(glm::inverse(glm::mat3(worldMatrix)));

                for (std::size_t i = 0; i < positions.size(); ++i)
                {
                    const glm::vec3 worldPos =
                        glm::vec3(worldMatrix * glm::vec4(positions[i], 1.0f));

                    glm::vec3 normal(0.0f, 1.0f, 0.0f);
                    if (wantsAligned && i < vis.CachedVertexNormals.size())
                    {
                        const glm::vec3 n  = normalMatrix * vis.CachedVertexNormals[i];
                        const float     n2 = glm::dot(n, n);
                        normal = (n2 > 1e-12f)
                            ? (n * (1.0f / glm::sqrt(n2)))
                            : glm::vec3(0.0f, 1.0f, 0.0f);
                    }

                    auto pt = PointCloudRenderPass::PackPoint(
                        worldPos.x, worldPos.y, worldPos.z,
                        normal.x, normal.y, normal.z,
                        vis.VertexSize,
                        vtxColor);
                    m_PointCloudPass->SubmitPoints(vis.VertexRenderMode, &pt, 1);
                }
            }
        }
    }

} // namespace Graphics::Passes
