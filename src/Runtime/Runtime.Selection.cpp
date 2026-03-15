module;

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <entt/entity/registry.hpp>
#include <entt/signal/dispatcher.hpp>

module Runtime.Selection;

import ECS;
import Geometry;
import Core.Logging;
import Graphics;

namespace Runtime::Selection
{
    namespace
    {
        [[nodiscard]] inline bool IsSelectable(const entt::registry& r, entt::entity e)
        {
            return r.valid(e) && r.all_of<ECS::Components::Selection::SelectableTag>(e);
        }

        [[nodiscard]] inline Picked MakeBackgroundPicked()
        {
            return {};
        }

        [[nodiscard]] inline Picked MakeEntityPicked(entt::entity entity, float pickRadius)
        {
            Picked picked{};
            picked.entity.id = entity;
            picked.entity.is_background = (entity == entt::null);
            picked.entity.pick_radius = pickRadius;
            return picked;
        }

        [[nodiscard]] inline float SquaredLength(const glm::vec3& v)
        {
            return glm::dot(v, v);
        }

        // Compute the unit normal of a triangle given three world-space vertices.
        // Returns zero for degenerate (collinear/coincident) triangles.
        [[nodiscard]] inline glm::vec3 ComputeTriangleNormal(
            const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2)
        {
            glm::vec3 n = glm::cross(v1 - v0, v2 - v0);
            const float lenSq = SquaredLength(n);
            return lenSq > 1.0e-20f ? n / std::sqrt(lenSq) : glm::vec3(0.0f);
        }

        // Conservative ray vs OBB broadphase: we approximate by AABB of the OBB in world space.
        [[nodiscard]] inline Geometry::AABB OBBToAABB(const Geometry::OBB& obb)
        {
            const glm::mat3 R = glm::mat3_cast(obb.Rotation);
            const glm::mat3 absR = glm::mat3(glm::abs(R[0]), glm::abs(R[1]), glm::abs(R[2]));
            const glm::vec3 worldExtents = absR * obb.Extents;
            return Geometry::AABB{obb.Center - worldExtents, obb.Center + worldExtents};
        }

        [[nodiscard]] inline glm::vec3 TransformPoint(const glm::mat4& m, const glm::vec3& p)
        {
            return glm::vec3(m * glm::vec4(p, 1.0f));
        }

        [[nodiscard]] inline glm::mat4 WorldMatrixFor(const entt::registry& reg,
                                                      entt::entity entity,
                                                      const ECS::Components::Transform::Component& transform)
        {
            if (const auto* world = reg.try_get<ECS::Components::Transform::WorldMatrix>(entity))
                return world->Matrix;
            return ECS::Components::Transform::GetMatrix(transform);
        }

        [[nodiscard]] inline float ComputeWorldPickRadius(float rayT, const PickRequest& request)
        {
            const float clampedDepth = std::max(rayT, 0.0f);
            const float viewportHeight = std::max(request.ViewportHeightPixels, 1.0f);
            const float worldUnitsPerPixel =
                (2.0f * clampedDepth * std::tan(0.5f * request.CameraFovYRadians)) / viewportHeight;
            return std::max(request.PickRadiusPixels * worldUnitsPerPixel, 1.0e-5f);
        }

        [[nodiscard]] inline Geometry::AABB PointCloudWorldAABB(
            const ECS::PointCloud::Data& pcd,
            const ECS::Components::Transform::Component& transform,
            const entt::registry& reg, entt::entity entity)
        {
            if (!pcd.CloudRef || pcd.CloudRef->Size() == 0)
                return {};

            const auto positions = pcd.CloudRef->Positions();
            Geometry::AABB local;
            for (const auto& p : positions)
            {
                local.Min = glm::min(local.Min, p);
                local.Max = glm::max(local.Max, p);
            }

            const glm::mat4 world = WorldMatrixFor(reg, entity, transform);
            const glm::vec3 corners[8] = {
                {local.Min.x, local.Min.y, local.Min.z}, {local.Max.x, local.Min.y, local.Min.z},
                {local.Min.x, local.Max.y, local.Min.z}, {local.Max.x, local.Max.y, local.Min.z},
                {local.Min.x, local.Min.y, local.Max.z}, {local.Max.x, local.Min.y, local.Max.z},
                {local.Min.x, local.Max.y, local.Max.z}, {local.Max.x, local.Max.y, local.Max.z},
            };

            Geometry::AABB result;
            for (const auto& c : corners)
            {
                const glm::vec3 wc = TransformPoint(world, c);
                result.Min = glm::min(result.Min, wc);
                result.Max = glm::max(result.Max, wc);
            }

            // Degenerate point clouds often collapse one or more axes exactly,
            // which makes the ray/AABB slab test numerically fragile when a ray
            // direction component is zero. Inflate zero-width axes slightly.
            constexpr float kAxisEpsilon = 1.0e-4f;
            const glm::vec3 size = result.Max - result.Min;
            if (size.x < kAxisEpsilon)
            {
                result.Min.x -= 0.5f * kAxisEpsilon;
                result.Max.x += 0.5f * kAxisEpsilon;
            }
            if (size.y < kAxisEpsilon)
            {
                result.Min.y -= 0.5f * kAxisEpsilon;
                result.Max.y += 0.5f * kAxisEpsilon;
            }
            if (size.z < kAxisEpsilon)
            {
                result.Min.z -= 0.5f * kAxisEpsilon;
                result.Max.z += 0.5f * kAxisEpsilon;
            }

            return result;
        }

        [[nodiscard]] inline const Geometry::Halfedge::Mesh* ResolveAuthoritativeMesh(
            entt::entity entity,
            const ECS::MeshCollider::Component& collider,
            const entt::registry& reg)
        {
            if (const auto* meshData = reg.try_get<ECS::Mesh::Data>(entity))
            {
                if (meshData->MeshRef)
                    return meshData->MeshRef.get();
            }

            if (collider.CollisionRef && collider.CollisionRef->SourceMesh)
                return collider.CollisionRef->SourceMesh.get();

            return nullptr;
        }

        [[nodiscard]] inline float ComputeLocalPickRadius(const glm::mat4& invWorld, float worldPickRadius)
        {
            const glm::mat3 invLinear(invWorld);
            const float sx = glm::length(invLinear[0]);
            const float sy = glm::length(invLinear[1]);
            const float sz = glm::length(invLinear[2]);
            return std::max(worldPickRadius * std::max(sx, std::max(sy, sz)), 1.0e-6f);
        }

        struct LocalVertexLookupView
        {
            const Geometry::KDTree* Tree = nullptr;
            std::span<const glm::vec3> Points;
            std::span<const uint32_t> Indices;

            Geometry::KDTree ScratchTree{};
            std::vector<glm::vec3> ScratchPoints{};
            std::vector<uint32_t> ScratchIndices{};
        };

        [[nodiscard]] LocalVertexLookupView BuildLocalVertexLookupView(
            const ECS::MeshCollider::Component& collider,
            const Geometry::Halfedge::Mesh* mesh)
        {
            LocalVertexLookupView view{};

            if (collider.CollisionRef &&
                !collider.CollisionRef->LocalVertexLookupPoints.empty() &&
                collider.CollisionRef->LocalVertexLookupPoints.size() == collider.CollisionRef->LocalVertexLookupIndices.size() &&
                !collider.CollisionRef->LocalVertexKdTree.Nodes().empty())
            {
                view.Tree = &collider.CollisionRef->LocalVertexKdTree;
                view.Points = collider.CollisionRef->LocalVertexLookupPoints;
                view.Indices = collider.CollisionRef->LocalVertexLookupIndices;
                return view;
            }

            if (mesh != nullptr)
            {
                view.ScratchPoints.reserve(mesh->VertexCount());
                view.ScratchIndices.reserve(mesh->VertexCount());
                for (std::size_t i = 0; i < mesh->VerticesSize(); ++i)
                {
                    const Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
                    if (!mesh->IsValid(vh) || mesh->IsDeleted(vh))
                        continue;

                    view.ScratchPoints.push_back(mesh->Position(vh));
                    view.ScratchIndices.push_back(static_cast<uint32_t>(vh.Index));
                }
            }
            else if (collider.CollisionRef)
            {
                view.ScratchPoints = collider.CollisionRef->Positions;
                view.ScratchIndices.reserve(collider.CollisionRef->Positions.size());
                for (uint32_t i = 0; i < collider.CollisionRef->Positions.size(); ++i)
                    view.ScratchIndices.push_back(i);
            }

            if (!view.ScratchPoints.empty())
            {
                static_cast<void>(view.ScratchTree.BuildFromPoints(view.ScratchPoints));
                view.Tree = &view.ScratchTree;
                view.Points = view.ScratchPoints;
                view.Indices = view.ScratchIndices;
            }

            return view;
        }

        [[nodiscard]] uint32_t ResolveNearestVertexIndex(
            const ECS::MeshCollider::Component& collider,
            const Geometry::Halfedge::Mesh* mesh,
            const glm::vec3& localPoint,
            float localPickRadius)
        {
            auto lookup = BuildLocalVertexLookupView(collider, mesh);
            if (lookup.Tree == nullptr || lookup.Points.empty() || lookup.Indices.empty())
                return Picked::Entity::InvalidIndex;

            std::vector<Geometry::KDTree::ElementIndex> candidates;
            static_cast<void>(lookup.Tree->QueryRadius(localPoint, localPickRadius, candidates));

            if (candidates.empty())
            {
                static_cast<void>(lookup.Tree->QueryKnn(localPoint, 1u, candidates));
            }

            uint32_t bestIndex = Picked::Entity::InvalidIndex;
            float bestDistanceSq = std::numeric_limits<float>::infinity();
            const float radiusSq = localPickRadius * localPickRadius;
            for (const auto elementIndex : candidates)
            {
                if (elementIndex >= lookup.Points.size() || elementIndex >= lookup.Indices.size())
                    continue;

                const float distSq = SquaredLength(lookup.Points[elementIndex] - localPoint);
                if (distSq > radiusSq)
                    continue;

                const uint32_t candidateIndex = lookup.Indices[elementIndex];
                if (distSq < bestDistanceSq || (distSq == bestDistanceSq && candidateIndex < bestIndex))
                {
                    bestDistanceSq = distSq;
                    bestIndex = candidateIndex;
                }
            }

            return bestIndex;
        }

        [[nodiscard]] std::optional<PickResult> PickMeshEntityAuthoritative(
            entt::entity entity,
            const ECS::MeshCollider::Component& collider,
            const Geometry::Halfedge::Mesh& mesh,
            const glm::mat4& world,
            const glm::mat4& invWorld,
            const Geometry::Ray& rayLocal,
            const PickRequest& request)
        {
            PickResult best{};
            best.PickedData = MakeBackgroundPicked();

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

                    const auto hit = Geometry::RayTriangle_Watertight(
                        rayLocal,
                        mesh.Position(v0),
                        mesh.Position(v1),
                        mesh.Position(v2),
                        0.0f,
                        request.MaxDistance);
                    if (!hit)
                        continue;

                    const glm::vec3 localPoint = rayLocal.Origin + hit->T * rayLocal.Direction;
                    const glm::vec3 worldPoint = TransformPoint(world, localPoint);
                    const float worldT = glm::dot(worldPoint - request.WorldRay.Origin, request.WorldRay.Direction);
                    if (!(worldT >= 0.0f && worldT <= request.MaxDistance))
                        continue;
                    if (worldT >= best.T)
                        continue;

                    const glm::vec3 world0 = TransformPoint(world, mesh.Position(v0));
                    const glm::vec3 world1 = TransformPoint(world, mesh.Position(v1));
                    const glm::vec3 world2 = TransformPoint(world, mesh.Position(v2));

                    const float pickRadius = ComputeWorldPickRadius(worldT, request);
                    Picked picked = MakeEntityPicked(entity, pickRadius);
                    picked.entity.face_idx = static_cast<uint32_t>(face.Index);
                    picked.spaces.World = worldPoint;
                    picked.spaces.Local = localPoint;
                    picked.spaces.WorldNormal = ComputeTriangleNormal(world0, world1, world2);
                    picked.spaces.Barycentric = glm::vec3(hit->U, hit->V, 1.0f - hit->U - hit->V);

                    float bestEdgeDistSq = std::numeric_limits<float>::infinity();
                    for (const auto halfedge : mesh.HalfedgesAroundFace(face))
                    {
                        const auto from = mesh.FromVertex(halfedge);
                        const auto to = mesh.ToVertex(halfedge);
                        const glm::vec3 edgeA = TransformPoint(world, mesh.Position(from));
                        const glm::vec3 edgeB = TransformPoint(world, mesh.Position(to));
                        const float distSq = Geometry::ClosestPointSegment(worldPoint, edgeA, edgeB).DistanceSq;
                        if (distSq < bestEdgeDistSq && distSq <= pickRadius * pickRadius)
                        {
                            bestEdgeDistSq = distSq;
                            picked.entity.edge_idx = static_cast<uint32_t>(mesh.Edge(halfedge).Index);
                        }
                    }

                    best.Entity = entity;
                    best.T = worldT;
                    best.PickedData = picked;
                }
            }

            if (best.Entity != entt::null)
            {
                const float localPickRadius = ComputeLocalPickRadius(invWorld, best.PickedData.entity.pick_radius);
                best.PickedData.entity.vertex_idx = ResolveNearestVertexIndex(
                    collider,
                    &mesh,
                    best.PickedData.spaces.Local,
                    localPickRadius);
            }

            return best.Entity != entt::null ? std::optional<PickResult>(best) : std::nullopt;
        }

        [[nodiscard]] std::optional<PickResult> PickMeshEntity(
            entt::entity entity,
            const ECS::Components::Transform::Component& transform,
            const ECS::MeshCollider::Component& collider,
            const entt::registry& reg,
            const PickRequest& request)
        {
            if (!collider.CollisionRef)
                return std::nullopt;

            const Geometry::AABB worldAabb = OBBToAABB(collider.WorldOBB);
            if (!Geometry::TestOverlap(request.WorldRay, worldAabb))
                return std::nullopt;

            const glm::mat4 world = WorldMatrixFor(reg, entity, transform);
            const glm::mat4 invWorld = glm::inverse(world);
            Geometry::Ray rayLocal{
                TransformPoint(invWorld, request.WorldRay.Origin),
                glm::normalize(glm::mat3(invWorld) * request.WorldRay.Direction)
            };
            rayLocal = Geometry::Validation::Sanitize(rayLocal);

            if (const auto* mesh = ResolveAuthoritativeMesh(entity, collider, reg))
            {
                if (const auto candidate = PickMeshEntityAuthoritative(entity, collider, *mesh, world, invWorld, rayLocal, request))
                    return candidate;
            }

            const auto& positions = collider.CollisionRef->Positions;
            const auto& indices = collider.CollisionRef->Indices;
            if (positions.empty() || indices.size() < 3)
                return std::nullopt;

            PickResult best{};
            best.PickedData = MakeBackgroundPicked();

            for (size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                const uint32_t i0 = indices[i + 0];
                const uint32_t i1 = indices[i + 1];
                const uint32_t i2 = indices[i + 2];
                if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size())
                    continue;

                const auto hit = Geometry::RayTriangle_Watertight(
                    rayLocal,
                    positions[i0], positions[i1], positions[i2],
                    0.0f,
                    request.MaxDistance);
                if (!hit)
                    continue;

                const glm::vec3 localPoint = rayLocal.Origin + hit->T * rayLocal.Direction;
                const glm::vec3 worldPoint = TransformPoint(world, localPoint);
                const float worldT = glm::dot(worldPoint - request.WorldRay.Origin, request.WorldRay.Direction);
                if (!(worldT >= 0.0f && worldT <= request.MaxDistance))
                    continue;
                if (worldT >= best.T)
                    continue;

                const glm::vec3 w0 = TransformPoint(world, positions[i0]);
                const glm::vec3 w1 = TransformPoint(world, positions[i1]);
                const glm::vec3 w2 = TransformPoint(world, positions[i2]);

                const float pickRadius = ComputeWorldPickRadius(worldT, request);
                Picked picked = MakeEntityPicked(entity, pickRadius);
                picked.entity.face_idx = static_cast<uint32_t>(i / 3u);
                picked.spaces.World = worldPoint;
                picked.spaces.Local = localPoint;
                picked.spaces.WorldNormal = ComputeTriangleNormal(w0, w1, w2);
                picked.spaces.Barycentric = glm::vec3(hit->U, hit->V, 1.0f - hit->U - hit->V);

                const glm::vec3 triWorld[3] = {w0, w1, w2};
                const uint32_t triIndices[3] = {i0, i1, i2};
                float bestVertexDistSq = std::numeric_limits<float>::infinity();
                for (uint32_t corner = 0; corner < 3; ++corner)
                {
                    const float distSq = SquaredLength(triWorld[corner] - worldPoint);
                    if (distSq < bestVertexDistSq && distSq <= pickRadius * pickRadius)
                    {
                        bestVertexDistSq = distSq;
                        picked.entity.vertex_idx = triIndices[corner];
                    }
                }

                const uint32_t edgePairs[3][2] = {{0u, 1u}, {1u, 2u}, {2u, 0u}};
                float bestEdgeDistSq = std::numeric_limits<float>::infinity();
                for (uint32_t edgeLocal = 0; edgeLocal < 3; ++edgeLocal)
                {
                    const glm::vec3 a = triWorld[edgePairs[edgeLocal][0]];
                    const glm::vec3 b = triWorld[edgePairs[edgeLocal][1]];
                    const float distSq = Geometry::ClosestPointSegment(worldPoint, a, b).DistanceSq;
                    if (distSq < bestEdgeDistSq && distSq <= pickRadius * pickRadius)
                    {
                        bestEdgeDistSq = distSq;
                        picked.entity.edge_idx = static_cast<uint32_t>((i / 3u) * 3u + edgeLocal);
                    }
                }

                best.Entity = entity;
                best.T = worldT;
                best.PickedData = picked;
            }

            if (best.Entity != entt::null)
            {
                const float localPickRadius = ComputeLocalPickRadius(invWorld, best.PickedData.entity.pick_radius);
                best.PickedData.entity.vertex_idx = ResolveNearestVertexIndex(
                    collider,
                    nullptr,
                    best.PickedData.spaces.Local,
                    localPickRadius);
            }

            return best.Entity != entt::null ? std::optional<PickResult>(best) : std::nullopt;
        }

        [[nodiscard]] std::optional<PickResult> PickPointCloudEntity(
            entt::entity entity,
            const ECS::Components::Transform::Component& transform,
            const ECS::PointCloud::Data& pcd,
            const entt::registry& reg,
            const PickRequest& request)
        {
            if (!pcd.CloudRef || pcd.CloudRef->Size() == 0)
                return std::nullopt;

            const Geometry::AABB worldAabb = PointCloudWorldAABB(pcd, transform, reg, entity);
            if (!worldAabb.IsValid() || !Geometry::TestOverlap(request.WorldRay, worldAabb))
                return std::nullopt;

            const glm::mat4 world = WorldMatrixFor(reg, entity, transform);
            PickResult best{};
            best.PickedData = MakeBackgroundPicked();

            const auto positions = pcd.CloudRef->Positions();
            const bool hasNormals = pcd.CloudRef->HasNormals();
            const auto normals = hasNormals ? pcd.CloudRef->Normals() : pcd.CloudRef->Positions().subspan(0, 0);
            for (uint32_t i = 0; i < positions.size(); ++i)
            {
                const glm::vec3 localPoint = positions[i];
                const glm::vec3 worldPoint = TransformPoint(world, localPoint);
                const auto closest = Geometry::ClosestPointRay(worldPoint, request.WorldRay);
                const float rayT = closest.RayT;
                const float distSq = closest.DistanceSq;
                if (!(rayT >= 0.0f && rayT <= request.MaxDistance))
                    continue;

                const float pickRadius = ComputeWorldPickRadius(rayT, request);
                if (distSq > pickRadius * pickRadius)
                    continue;

                const float currentDistSq = SquaredLength(best.PickedData.spaces.World - (request.WorldRay.Origin + best.T * request.WorldRay.Direction));
                if (rayT < best.T || (rayT == best.T && distSq < currentDistSq))
                {
                    Picked picked = MakeEntityPicked(entity, pickRadius);
                    picked.entity.vertex_idx = i;
                    picked.spaces.World = worldPoint;
                    picked.spaces.Local = localPoint;
                    if (i < normals.size())
                    {
                        glm::vec3 wn = glm::transpose(glm::inverse(glm::mat3(world))) * normals[i];
                        const float wnLenSq = SquaredLength(wn);
                        if (wnLenSq > 1.0e-20f)
                            picked.spaces.WorldNormal = wn / std::sqrt(wnLenSq);
                    }

                    best.Entity = entity;
                    best.T = rayT;
                    best.PickedData = picked;
                }
            }

            return best.Entity != entt::null ? std::optional<PickResult>(best) : std::nullopt;
        }

        [[nodiscard]] std::optional<PickResult> PickGraphEntity(
            entt::entity entity,
            const ECS::Components::Transform::Component& transform,
            const ECS::Graph::Data& graphData,
            const entt::registry& reg,
            const PickRequest& request)
        {
            if (!graphData.GraphRef || graphData.GraphRef->VertexCount() == 0)
                return std::nullopt;

            const glm::mat4 world = WorldMatrixFor(reg, entity, transform);
            PickResult best{};
            best.PickedData = MakeBackgroundPicked();
            float bestDistanceSq = std::numeric_limits<float>::infinity();

            for (uint32_t i = 0; i < graphData.GraphRef->VerticesSize(); ++i)
            {
                const Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
                if (!graphData.GraphRef->IsValid(vh) || graphData.GraphRef->IsDeleted(vh))
                    continue;

                const glm::vec3 localPoint = graphData.GraphRef->VertexPosition(vh);
                const glm::vec3 worldPoint = TransformPoint(world, localPoint);
                const auto nodeQuery = Geometry::ClosestPointRay(worldPoint, request.WorldRay);
                const float rayT = nodeQuery.RayT;
                const float distSq = nodeQuery.DistanceSq;
                if (!(rayT >= 0.0f && rayT <= request.MaxDistance))
                    continue;

                const float pickRadius = ComputeWorldPickRadius(rayT, request);
                if (distSq > pickRadius * pickRadius)
                    continue;

                if (rayT < best.T || (rayT == best.T && distSq < bestDistanceSq))
                {
                    Picked picked = MakeEntityPicked(entity, pickRadius);
                    picked.entity.vertex_idx = i;
                    picked.spaces.World = worldPoint;
                    picked.spaces.Local = localPoint;

                    best.Entity = entity;
                    best.T = rayT;
                    best.PickedData = picked;
                    bestDistanceSq = distSq;
                }
            }

            for (uint32_t i = 0; i < graphData.GraphRef->EdgesSize(); ++i)
            {
                const Geometry::EdgeHandle eh{static_cast<Geometry::PropertyIndex>(i)};
                if (!graphData.GraphRef->IsValid(eh) || graphData.GraphRef->IsDeleted(eh))
                    continue;

                const auto [v0h, v1h] = graphData.GraphRef->EdgeVertices(eh);
                const glm::vec3 localA = graphData.GraphRef->VertexPosition(v0h);
                const glm::vec3 localB = graphData.GraphRef->VertexPosition(v1h);
                const glm::vec3 worldA = TransformPoint(world, localA);
                const glm::vec3 worldB = TransformPoint(world, localB);
                const auto closest = Geometry::ClosestRaySegment(request.WorldRay, worldA, worldB);
                if (!(closest.RayT >= 0.0f && closest.RayT <= request.MaxDistance))
                    continue;

                const float pickRadius = ComputeWorldPickRadius(closest.RayT, request);
                if (closest.DistanceSq > pickRadius * pickRadius)
                    continue;

                if (closest.RayT < best.T || (closest.RayT == best.T && closest.DistanceSq < bestDistanceSq))
                {
                    const float segmentT = closest.SegmentT;
                    Picked picked = MakeEntityPicked(entity, pickRadius);
                    picked.entity.edge_idx = i;
                    picked.spaces.World = closest.PointOnSegment;
                    picked.spaces.Local = glm::mix(localA, localB, segmentT);

                    glm::vec3 tangent = worldB - worldA;
                    const float tangentLenSq = SquaredLength(tangent);
                    if (tangentLenSq > 1.0e-20f)
                        picked.spaces.WorldNormal = tangent / std::sqrt(tangentLenSq);

                    best.Entity = entity;
                    best.T = closest.RayT;
                    best.PickedData = picked;
                    bestDistanceSq = closest.DistanceSq;
                }
            }

            return best.Entity != entt::null ? std::optional<PickResult>(best) : std::nullopt;
        }
    }

    Geometry::Ray RayFromNDC(const Graphics::CameraComponent& camera, const glm::vec2& ndc)
    {
        const glm::mat4 invViewProj = glm::inverse(camera.ProjectionMatrix * camera.ViewMatrix);

        const glm::vec4 pNear = invViewProj * glm::vec4(ndc.x, ndc.y, 0.0f, 1.0f);
        const glm::vec4 pFar  = invViewProj * glm::vec4(ndc.x, ndc.y, 1.0f, 1.0f);

        const glm::vec3 nearW = glm::vec3(pNear) / pNear.w;
        const glm::vec3 farW  = glm::vec3(pFar) / pFar.w;

        Geometry::Ray ray;
        ray.Origin = nearW;
        ray.Direction = glm::normalize(farW - nearW);
        return Geometry::Validation::Sanitize(ray);
    }

    PickResult PickCPU(const ECS::Scene& scene, const PickRequest& request)
    {
        PickResult best{};
        best.PickedData = MakeBackgroundPicked();

        const auto& reg = scene.GetRegistry();

        auto meshView = reg.view<ECS::Components::Transform::Component,
                                 ECS::MeshCollider::Component,
                                 ECS::Components::Selection::SelectableTag>();
        for (auto [entity, transform, collider] : meshView.each())
        {
            const auto candidate = PickMeshEntity(entity, transform, collider, reg, request);
            if (candidate && candidate->T < best.T)
                best = *candidate;
        }

        auto pointCloudView = reg.view<ECS::Components::Transform::Component,
                                       ECS::PointCloud::Data,
                                       ECS::Components::Selection::SelectableTag>();
        for (auto [entity, transform, pcd] : pointCloudView.each())
        {
            const auto candidate = PickPointCloudEntity(entity, transform, pcd, reg, request);
            if (candidate && candidate->T < best.T)
                best = *candidate;
        }

        auto graphView = reg.view<ECS::Components::Transform::Component,
                                  ECS::Graph::Data,
                                  ECS::Components::Selection::SelectableTag>();
        for (auto [entity, transform, graphData] : graphView.each())
        {
            const auto candidate = PickGraphEntity(entity, transform, graphData, reg, request);
            if (candidate && candidate->T < best.T)
                best = *candidate;
        }

        return best;
    }

    PickResult PickEntityCPU(const ECS::Scene& scene, entt::entity entity, const PickRequest& request)
    {
        PickResult result{};
        result.PickedData = MakeBackgroundPicked();

        const auto& reg = scene.GetRegistry();
        if (!reg.valid(entity))
            return result;

        const auto* transform = reg.try_get<ECS::Components::Transform::Component>(entity);
        if (!transform)
            return result;

        if (const auto* collider = reg.try_get<ECS::MeshCollider::Component>(entity))
        {
            if (const auto candidate = PickMeshEntity(entity, *transform, *collider, reg, request))
                return *candidate;
        }

        if (const auto* pcd = reg.try_get<ECS::PointCloud::Data>(entity))
        {
            if (const auto candidate = PickPointCloudEntity(entity, *transform, *pcd, reg, request))
                return *candidate;
        }

        if (const auto* graphData = reg.try_get<ECS::Graph::Data>(entity))
        {
            if (const auto candidate = PickGraphEntity(entity, *transform, *graphData, reg, request))
                return *candidate;
        }

        return result;
    }

    void ApplySelection(ECS::Scene& scene, entt::entity hitEntity, PickMode mode)
    {
        auto& reg = scene.GetRegistry();

        if (mode == PickMode::Replace)
        {
            auto selectedView = reg.view<ECS::Components::Selection::SelectedTag>();
            for (auto e : selectedView)
                reg.remove<ECS::Components::Selection::SelectedTag>(e);
        }

        if (hitEntity == entt::null || !reg.valid(hitEntity))
        {
            scene.GetDispatcher().enqueue<ECS::Events::SelectionChanged>({entt::null});
            return;
        }
        if (!IsSelectable(reg, hitEntity))
            return;

        const bool isSelected = reg.all_of<ECS::Components::Selection::SelectedTag>(hitEntity);

        switch (mode)
        {
        case PickMode::Replace:
        case PickMode::Add:
            if (!isSelected)
                reg.emplace<ECS::Components::Selection::SelectedTag>(hitEntity);
            break;
        case PickMode::Toggle:
            if (isSelected)
                reg.remove<ECS::Components::Selection::SelectedTag>(hitEntity);
            else
                reg.emplace<ECS::Components::Selection::SelectedTag>(hitEntity);
            break;
        }

        scene.GetDispatcher().enqueue<ECS::Events::SelectionChanged>({hitEntity});
    }

    void ApplyHover(ECS::Scene& scene, entt::entity hoveredEntity)
    {
        auto& reg = scene.GetRegistry();

        auto hoveredView = reg.view<ECS::Components::Selection::HoveredTag>();
        for (auto e : hoveredView)
            reg.remove<ECS::Components::Selection::HoveredTag>(e);

        if (hoveredEntity == entt::null || !reg.valid(hoveredEntity))
        {
            scene.GetDispatcher().enqueue<ECS::Events::HoverChanged>({entt::null});
            return;
        }
        if (!IsSelectable(reg, hoveredEntity))
            return;

        reg.emplace<ECS::Components::Selection::HoveredTag>(hoveredEntity);
        scene.GetDispatcher().enqueue<ECS::Events::HoverChanged>({hoveredEntity});
    }
}

