module;

#include <cstdint>
#include <unordered_set>

module Graphics:Passes.Mesh.Impl;

import :Passes.Mesh;
import :RenderPipeline;
import :Components;
import :Geometry;
import ECS;

namespace Graphics::Passes
{

    // =========================================================================
    // AddPasses
    // =========================================================================
    //
    // Iterates all entities with MeshRenderer::Component + RenderVisualization::Component
    // and builds the wireframe edge cache for entities with ShowWireframe enabled.
    //
    // Edge rendering is handled by LinePass via ECS::Line::Component.
    // Vertex point rendering is handled by PointPass (BDA retained-mode).
    // This pass only builds the edge cache — no GPU rendering.

    void MeshRenderPass::AddPasses(RenderPassContext& ctx)
    {
        auto& registry = ctx.Scene.GetRegistry();
        auto meshView  = registry.view<ECS::MeshRenderer::Component,
                                       ECS::RenderVisualization::Component>();

        for (auto [entity, mr, vis] : meshView.each())
        {
            if (!vis.ShowWireframe)
                continue;

            if (!vis.EdgeCacheDirty)
                continue;

            // CPU paths source positions from the collision mesh (CPU-resident copy).
            auto* collider = registry.try_get<ECS::MeshCollider::Component>(entity);
            if (!collider || !collider->CollisionRef)
                continue;

            const auto& indices = collider->CollisionRef->Indices;
            if (indices.empty())
                continue;

            // Determine topology from GPU geometry.
            Graphics::PrimitiveTopology topology = Graphics::PrimitiveTopology::Triangles;
            GeometryGpuData* geo = ctx.GeometryStorage.GetUnchecked(mr.Geometry);
            if (geo)
                topology = geo->GetTopology();

            // ------------------------------------------------------------------
            // Edge cache: build lazily (consumed by LinePass via Line::Component).
            // ------------------------------------------------------------------
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
                vis.CachedEdges.reserve(edgeSet.size());
                for (const auto& [a, b] : edgeSet)
                    vis.CachedEdges.push_back({a, b});
            }
            else if (topology == Graphics::PrimitiveTopology::Lines)
            {
                vis.CachedEdges.reserve(indices.size() / 2);
                for (std::size_t e = 0; e + 1 < indices.size(); e += 2)
                    vis.CachedEdges.push_back({indices[e], indices[e + 1]});
            }

            vis.EdgeCacheDirty = false;
        }
    }

} // namespace Graphics::Passes
