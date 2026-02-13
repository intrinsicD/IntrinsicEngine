module;

#include <limits>
#include <algorithm>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <entt/entity/registry.hpp>

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
            return;
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
    }

    void ApplyHover(ECS::Scene& scene, entt::entity hoveredEntity)
    {
        auto& reg = scene.GetRegistry();

        auto hoveredView = reg.view<ECS::Components::Selection::HoveredTag>();
        for (auto e : hoveredView)
            reg.remove<ECS::Components::Selection::HoveredTag>(e);

        if (hoveredEntity == entt::null || !reg.valid(hoveredEntity))
            return;
        if (!IsSelectable(reg, hoveredEntity))
            return;

        reg.emplace<ECS::Components::Selection::HoveredTag>(hoveredEntity);
    }
}

