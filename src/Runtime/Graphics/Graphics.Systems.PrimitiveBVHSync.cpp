module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>

module Graphics.Systems.PrimitiveBVHSync;

import Graphics.Components;
import Geometry.AABB;
import Geometry.HalfedgeMesh;
import Geometry.Graph;
import Geometry.PointCloud;
import Geometry.Properties;

import Core.Hash;
import Core.FrameGraph;

import ECS;

using namespace Core::Hash;

namespace Graphics::Systems::PrimitiveBVHSync
{
    namespace
    {
        [[nodiscard]] Geometry::AABB TriangleBounds(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
        {
            Geometry::AABB bounds{a, a};
            bounds = Geometry::Union(bounds, b);
            bounds = Geometry::Union(bounds, c);
            return bounds;
        }

        [[nodiscard]] bool BuildFromMesh(const Geometry::Halfedge::Mesh& mesh, ECS::PrimitiveBVH::Data& out)
        {
            out.Clear();
            out.Source = ECS::PrimitiveBVH::SourceKind::MeshTriangles;
            out.ActualBackend = ECS::PrimitiveBVH::Backend::CPU;

            std::vector<Geometry::AABB> primitiveBounds;
            primitiveBounds.reserve(mesh.FaceCount() * 2u);

            for (std::size_t faceIndex = 0; faceIndex < mesh.FacesSize(); ++faceIndex)
            {
                const Geometry::FaceHandle face{static_cast<Geometry::PropertyIndex>(faceIndex)};
                if (!mesh.IsValid(face) || mesh.IsDeleted(face))
                    continue;

                std::vector<Geometry::VertexHandle> faceVertices;
                faceVertices.reserve(mesh.Valence(face));
                for (const auto vertex : mesh.VerticesAroundFace(face))
                    faceVertices.push_back(vertex);

                if (faceVertices.size() < 3)
                    continue;

                const auto v0 = faceVertices.front();
                for (std::size_t corner = 1; corner + 1 < faceVertices.size(); ++corner)
                {
                    const auto v1 = faceVertices[corner];
                    const auto v2 = faceVertices[corner + 1];
                    const glm::vec3 a = mesh.Position(v0);
                    const glm::vec3 b = mesh.Position(v1);
                    const glm::vec3 c = mesh.Position(v2);

                    out.Triangles.push_back({
                        .Bounds = TriangleBounds(a, b, c),
                        .A = a,
                        .B = b,
                        .C = c,
                        .I0 = static_cast<uint32_t>(v0.Index),
                        .I1 = static_cast<uint32_t>(v1.Index),
                        .I2 = static_cast<uint32_t>(v2.Index),
                        .FaceIndex = static_cast<uint32_t>(face.Index),
                    });
                    primitiveBounds.push_back(out.Triangles.back().Bounds);
                }
            }

            if (primitiveBounds.empty())
            {
                out.Clear();
                return false;
            }

            out.LocalBounds = Geometry::Union(std::span<const Geometry::AABB>(primitiveBounds));
            const auto buildResult = out.LocalBVH.Build(std::move(primitiveBounds), out.BuildParams);
            if (!buildResult)
            {
                out.Clear();
                return false;
            }

            out.PrimitiveCount = static_cast<uint32_t>(out.Triangles.size());
            return true;
        }

        [[nodiscard]] bool BuildFromCollider(const ECS::MeshCollider::Component& collider, ECS::PrimitiveBVH::Data& out)
        {
            out.Clear();
            out.Source = ECS::PrimitiveBVH::SourceKind::MeshTriangles;
            out.ActualBackend = ECS::PrimitiveBVH::Backend::CPU;

            if (!collider.CollisionRef)
            {
                out.Clear();
                return false;
            }

            const auto& positions = collider.CollisionRef->Positions;
            const auto& indices = collider.CollisionRef->Indices;
            if (positions.empty() || indices.size() < 3)
            {
                out.Clear();
                return false;
            }

            std::vector<Geometry::AABB> primitiveBounds;
            primitiveBounds.reserve(indices.size() / 3u);

            for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                const uint32_t i0 = indices[i + 0];
                const uint32_t i1 = indices[i + 1];
                const uint32_t i2 = indices[i + 2];
                if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size())
                    continue;

                const glm::vec3 a = positions[i0];
                const glm::vec3 b = positions[i1];
                const glm::vec3 c = positions[i2];
                out.Triangles.push_back({
                    .Bounds = TriangleBounds(a, b, c),
                    .A = a,
                    .B = b,
                    .C = c,
                    .I0 = i0,
                    .I1 = i1,
                    .I2 = i2,
                    .FaceIndex = static_cast<uint32_t>(i / 3u),
                });
                primitiveBounds.push_back(out.Triangles.back().Bounds);
            }

            if (primitiveBounds.empty())
            {
                out.Clear();
                return false;
            }

            out.LocalBounds = Geometry::Union(std::span<const Geometry::AABB>(primitiveBounds));
            const auto buildResult = out.LocalBVH.Build(std::move(primitiveBounds), out.BuildParams);
            if (!buildResult)
            {
                out.Clear();
                return false;
            }

            out.PrimitiveCount = static_cast<uint32_t>(out.Triangles.size());
            return true;
        }

        [[nodiscard]] bool BuildFromGraph(const Geometry::Graph::Graph& graph, ECS::PrimitiveBVH::Data& out)
        {
            out.Clear();
            out.Source = ECS::PrimitiveBVH::SourceKind::GraphSegments;
            out.ActualBackend = ECS::PrimitiveBVH::Backend::CPU;

            std::vector<Geometry::AABB> primitiveBounds;
            primitiveBounds.reserve(graph.EdgeCount());

            for (uint32_t i = 0; i < graph.EdgesSize(); ++i)
            {
                const Geometry::EdgeHandle eh{static_cast<Geometry::PropertyIndex>(i)};
                if (!graph.IsValid(eh) || graph.IsDeleted(eh))
                    continue;

                const auto [v0h, v1h] = graph.EdgeVertices(eh);
                const glm::vec3 a = graph.VertexPosition(v0h);
                const glm::vec3 b = graph.VertexPosition(v1h);
                const Geometry::AABB bounds = Geometry::Union(Geometry::AABB{a, a}, b);
                out.Segments.push_back({
                    .Bounds = bounds,
                    .A = a,
                    .B = b,
                    .EdgeIndex = i,
                });
                primitiveBounds.push_back(bounds);
            }

            if (primitiveBounds.empty())
            {
                out.Clear();
                return false;
            }

            out.LocalBounds = Geometry::Union(std::span<const Geometry::AABB>(primitiveBounds));
            const auto buildResult = out.LocalBVH.Build(std::move(primitiveBounds), out.BuildParams);
            if (!buildResult)
            {
                out.Clear();
                return false;
            }

            out.PrimitiveCount = static_cast<uint32_t>(out.Segments.size());
            return true;
        }

        [[nodiscard]] bool BuildFromPointCloud(const Geometry::PointCloud::Cloud& cloud, ECS::PrimitiveBVH::Data& out)
        {
            out.Clear();
            out.Source = ECS::PrimitiveBVH::SourceKind::PointCloudPoints;
            out.ActualBackend = ECS::PrimitiveBVH::Backend::CPU;

            const auto positions = cloud.Positions();
            if (positions.empty())
            {
                out.Clear();
                return false;
            }

            std::vector<Geometry::AABB> primitiveBounds;
            primitiveBounds.reserve(positions.size());

            for (uint32_t i = 0; i < positions.size(); ++i)
            {
                const glm::vec3 p = positions[i];
                const Geometry::AABB bounds{p, p};
                out.Points.push_back({
                    .Bounds = bounds,
                    .Position = p,
                    .PointIndex = i,
                });
                primitiveBounds.push_back(bounds);
            }

            out.LocalBounds = Geometry::Union(std::span<const Geometry::AABB>(primitiveBounds));
            const auto buildResult = out.LocalBVH.Build(std::move(primitiveBounds), out.BuildParams);
            if (!buildResult)
            {
                out.Clear();
                return false;
            }

            out.PrimitiveCount = static_cast<uint32_t>(out.Points.size());
            return true;
        }

        [[nodiscard]] bool NeedsGraphRebuild(const ECS::PrimitiveBVH::Data& data, const ECS::Graph::Data* graphData)
        {
            return data.Dirty || !data.HasValidBVH() || (graphData != nullptr && graphData->GpuDirty);
        }

        [[nodiscard]] bool NeedsPointCloudRebuild(const ECS::PrimitiveBVH::Data& data, const ECS::PointCloud::Data* pointCloudData)
        {
            return data.Dirty || !data.HasValidBVH() || (pointCloudData != nullptr && pointCloudData->GpuDirty);
        }
    }

    void OnUpdate(entt::registry& registry)
    {
        auto view = registry.view<ECS::PrimitiveBVH::Data>();
        for (auto [entity, bvhData] : view.each())
        {
            const auto* meshData = registry.try_get<ECS::Mesh::Data>(entity);
            const auto* collider = registry.try_get<ECS::MeshCollider::Component>(entity);
            const auto* graphData = registry.try_get<ECS::Graph::Data>(entity);
            const auto* pointCloudData = registry.try_get<ECS::PointCloud::Data>(entity);

            if (meshData != nullptr && meshData->MeshRef)
            {
                if (bvhData.Dirty || !bvhData.HasValidBVH())
                    static_cast<void>(BuildFromMesh(*meshData->MeshRef, bvhData));
                bvhData.Dirty = false;
                continue;
            }

            if (collider != nullptr && collider->CollisionRef)
            {
                if (bvhData.Dirty || !bvhData.HasValidBVH())
                    static_cast<void>(BuildFromCollider(*collider, bvhData));
                bvhData.Dirty = false;
                continue;
            }

            if (graphData != nullptr && graphData->GraphRef)
            {
                if (NeedsGraphRebuild(bvhData, graphData))
                    static_cast<void>(BuildFromGraph(*graphData->GraphRef, bvhData));
                bvhData.Dirty = false;
                continue;
            }

            if (pointCloudData != nullptr && pointCloudData->CloudRef)
            {
                if (NeedsPointCloudRebuild(bvhData, pointCloudData))
                    static_cast<void>(BuildFromPointCloud(*pointCloudData->CloudRef, bvhData));
                bvhData.Dirty = false;
                continue;
            }

            bvhData.Clear();
            bvhData.Dirty = false;
        }
    }

    void RegisterSystem(Core::FrameGraph& graph,
                        entt::registry& registry)
    {
        graph.AddPass("PrimitiveBVHSync",
            [](Core::FrameGraphBuilder& builder)
            {
                builder.Read<ECS::Mesh::Data>();
                builder.Read<ECS::MeshCollider::Component>();
                builder.Read<ECS::Graph::Data>();
                builder.Read<ECS::PointCloud::Data>();
                builder.Write<ECS::PrimitiveBVH::Data>();
                builder.WaitFor("PropertySetDirtySync"_id);
            },
            [&registry]()
            {
                OnUpdate(registry);
            });
    }
}

