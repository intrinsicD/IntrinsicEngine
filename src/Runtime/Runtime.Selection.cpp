module;

#include <limits>
#include <algorithm>
#include <optional>
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

        // Conservative ray vs OBB broadphase: we approximate by AABB of the OBB in world space.
        // This keeps CPU picking cheap without adding another exact OBB solver.
        [[nodiscard]] inline Geometry::AABB OBBToAABB(const Geometry::OBB& obb)
        {
            // Build rotation matrix; extents are half sizes.
            const glm::mat3 R = glm::mat3_cast(obb.Rotation);
            const glm::mat3 absR = glm::mat3(glm::abs(R[0]), glm::abs(R[1]), glm::abs(R[2]));
            const glm::vec3 worldExtents = absR * obb.Extents;
            return Geometry::AABB{obb.Center - worldExtents, obb.Center + worldExtents};
        }

        [[nodiscard]] inline glm::vec3 TransformPoint(const glm::mat4& m, const glm::vec3& p)
        {
            return glm::vec3(m * glm::vec4(p, 1.0f));
        }

        // Ray vs AABB slab test returning entry distance (or nullopt on miss).
        [[nodiscard]] inline std::optional<float> RayAABBDistance(const Geometry::Ray& r, const Geometry::AABB& b)
        {
            const glm::vec3 invDir = 1.0f / r.Direction;
            const glm::vec3 t0s = (b.Min - r.Origin) * invDir;
            const glm::vec3 t1s = (b.Max - r.Origin) * invDir;

            const glm::vec3 tsmaller = glm::min(t0s, t1s);
            const glm::vec3 tbigger = glm::max(t0s, t1s);

            const float tmin = glm::max(tsmaller.x, glm::max(tsmaller.y, tsmaller.z));
            const float tmax = glm::min(tbigger.x, glm::min(tbigger.y, tbigger.z));

            if (tmax < tmin || tmax < 0.0f) return std::nullopt;
            return tmin > 0.0f ? tmin : tmax;
        }

        // Compute world-space AABB for a point cloud entity.
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

            glm::mat4 world;
            if (auto* w = reg.try_get<ECS::Components::Transform::WorldMatrix>(entity))
                world = w->Matrix;
            else
                world = ECS::Components::Transform::GetMatrix(transform);

            // Transform AABB corners to world space.
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
            return result;
        }
    }

    Geometry::Ray RayFromNDC(const Graphics::CameraComponent& camera, const glm::vec2& ndc)
    {
        // Invert clip -> world for near/far points.
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
        const auto& reg = scene.GetRegistry();

        // Broadphase: ray vs (approx) AABB of collider OBB.
        // Narrowphase: ray vs triangles; still O(m) per mesh but good enough for editor clicks.
        auto view = reg.view<ECS::Components::Transform::Component,
                             ECS::MeshCollider::Component,
                             ECS::Components::Selection::SelectableTag>();

        for (auto [entity, transform, collider] : view.each())
        {
            if (!collider.CollisionRef) continue;

            const Geometry::AABB worldAabb = OBBToAABB(collider.WorldOBB);
            if (!Geometry::TestOverlap(request.WorldRay, worldAabb))
                continue;

            // Transform ray into local space.
            glm::mat4 world;
            if (auto* w = reg.try_get<ECS::Components::Transform::WorldMatrix>(entity))
                world = w->Matrix;
            else
                world = ECS::Components::Transform::GetMatrix(transform);

            const glm::mat4 invWorld = glm::inverse(world);
            const glm::vec3 oL = TransformPoint(invWorld, request.WorldRay.Origin);
            const glm::vec3 dL = glm::normalize(glm::mat3(invWorld) * request.WorldRay.Direction);
            Geometry::Ray rayLocal{ oL, dL };
            rayLocal = Geometry::Validation::Sanitize(rayLocal);

            // Triangle list assumed.
            const auto& positions = collider.CollisionRef->Positions;
            const auto& indices   = collider.CollisionRef->Indices;
            if (positions.empty() || indices.size() < 3) continue;

            // Optional: broadphase using the mesh's local octree (vertex AABBs).
            // NOTE: Disabled for robustness.
            // The current collision data stores a vertex octree, which is not a triangle BVH.
            // Additionally, Ray-vs-AABB slab tests can produce NaNs/inf for degenerate rays
            // (e.g., zero components in direction) and trigger ASAN/UBSAN failures.
            // We rely on the conservative world AABB broadphase + watertight triangle test.

            // Narrow phase.
            const float tMax = std::min(request.MaxDistance, best.T);
            for (size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                const uint32_t i0 = indices[i + 0];
                const uint32_t i1 = indices[i + 1];
                const uint32_t i2 = indices[i + 2];
                if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size())
                    continue;

                const auto hit = Geometry::RayTriangle_Watertight(rayLocal,
                    positions[i0], positions[i1], positions[i2],
                    0.0f, tMax);

                if (!hit) continue;

                // Convert hit distance back to world-ish metric.
                // NOTE: for non-uniform scales, local t != world t; we still use it for ordering inside a single mesh.
                // For editor picking this is usually fine, and broadphase already pruned.
                if (hit->T < best.T)
                {
                    best.Entity = entity;
                    best.T = hit->T;
                }
            }
        }

        // Point cloud entities: ray vs world AABB for entity-level selection.
        // Point clouds have no triangle mesh, so AABB intersection is the narrowphase.
        {
            auto pcView = reg.view<ECS::Components::Transform::Component,
                                   ECS::PointCloud::Data,
                                   ECS::Components::Selection::SelectableTag>();

            for (auto [entity, transform, pcd] : pcView.each())
            {
                if (!pcd.CloudRef || pcd.CloudRef->Size() == 0) continue;

                const Geometry::AABB worldAabb = PointCloudWorldAABB(pcd, transform, reg, entity);
                if (!worldAabb.IsValid()) continue;

                const auto hitT = RayAABBDistance(request.WorldRay, worldAabb);
                if (!hitT) continue;

                if (*hitT < best.T && *hitT <= request.MaxDistance)
                {
                    best.Entity = entity;
                    best.T = *hitT;
                }
            }
        }

        return best;
    }

    void ApplySelection(ECS::Scene& scene, entt::entity hitEntity, PickMode mode)
    {
        auto& reg = scene.GetRegistry();

        // Replace clears everything first.
        if (mode == PickMode::Replace)
        {
            auto selectedView = reg.view<ECS::Components::Selection::SelectedTag>();
            for (auto e : selectedView)
                reg.remove<ECS::Components::Selection::SelectedTag>(e);
        }

        if (hitEntity == entt::null || !reg.valid(hitEntity))
        {
            // Deselect-all: notify with null entity.
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

