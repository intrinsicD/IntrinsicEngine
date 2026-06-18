module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include <entt/entity/registry.hpp>

module Extrinsic.Runtime.CameraFocusCommand;

import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.SelectionController;
import Geometry.Sphere;

namespace Extrinsic::Runtime
{
    namespace
    {
        // Mirror the import-time auto-focus floor (Runtime.Engine.cpp
        // `kMinimumVisibleRadius`) so selection focus and import focus agree on
        // the smallest framable radius. Final view-distance flooring is still
        // owned by the controllers' `SafeFocusRadius`.
        constexpr float kMinimumFocusRadius = 0.05f;

        [[nodiscard]] bool IsFiniteSphere(const Geometry::Sphere& sphere) noexcept
        {
            return std::isfinite(sphere.Center.x) && std::isfinite(sphere.Center.y) &&
                   std::isfinite(sphere.Center.z) && std::isfinite(sphere.Radius);
        }
    } // namespace

    std::optional<CameraFocusTarget> ComputeFocusTargetForBoundingSpheres(
        const std::span<const Geometry::Sphere> worldSpheres) noexcept
    {
        glm::vec3    centerSum{0.0f};
        std::size_t  count = 0u;
        for (const Geometry::Sphere& sphere : worldSpheres)
        {
            if (!IsFiniteSphere(sphere))
                continue;
            centerSum += sphere.Center;
            ++count;
        }
        if (count == 0u)
            return std::nullopt;

        // Center of mass: equal weight per entity (deterministic, dependency
        // free). For a single sphere this is just its center.
        const glm::vec3 center = centerSum / static_cast<float>(count);

        // Largest enclosing extent: the radius that contains every contributing
        // sphere measured from the shared center, so (center, radius) bounds all
        // of them and the controller's Focus distance frames the whole set.
        float radius = 0.0f;
        for (const Geometry::Sphere& sphere : worldSpheres)
        {
            if (!IsFiniteSphere(sphere))
                continue;
            const float reach =
                glm::length(sphere.Center - center) + std::max(0.0f, sphere.Radius);
            radius = std::max(radius, reach);
        }

        if (!std::isfinite(radius))
            radius = kMinimumFocusRadius;
        radius = std::max(kMinimumFocusRadius, radius);

        return CameraFocusTarget{.Center = center, .Radius = radius};
    }

    std::optional<CameraFocusTarget> ComputeFocusTargetForEntities(
        const ECS::Scene::Registry&                scene,
        const std::span<const ECS::EntityHandle>   entities) noexcept
    {
        std::vector<Geometry::Sphere> spheres;
        spheres.reserve(entities.size());

        const entt::registry& raw = scene.Raw();
        for (const ECS::EntityHandle entity : entities)
        {
            if (!scene.IsValid(entity))
                continue;
            const auto* bounds =
                raw.try_get<ECS::Components::Culling::World::Bounds>(entity);
            if (bounds == nullptr)
                continue;
            spheres.push_back(bounds->WorldBoundingSphere);
        }

        return ComputeFocusTargetForBoundingSpheres(spheres);
    }

    bool ApplyCameraFocus(CameraControllerRegistry&  cameras,
                          const CameraControllerSlot slot,
                          const CameraFocusTarget&   target) noexcept
    {
        ICameraController* controller = cameras.ResolveOrNull(slot);
        if (controller == nullptr)
            return false;

        controller->Focus(target);
        cameras.MarkCameraTransition(slot);
        return true;
    }

    bool FocusCameraOnEntities(CameraControllerRegistry&                cameras,
                               const ECS::Scene::Registry&              scene,
                               const std::span<const ECS::EntityHandle> entities,
                               const CameraControllerSlot               slot) noexcept
    {
        const std::optional<CameraFocusTarget> target =
            ComputeFocusTargetForEntities(scene, entities);
        if (!target.has_value())
            return false;
        return ApplyCameraFocus(cameras, slot, *target);
    }

    bool FocusCameraOnSelection(CameraControllerRegistry&   cameras,
                                const SelectionController&  selection,
                                const ECS::Scene::Registry& scene,
                                const CameraControllerSlot  slot) noexcept
    {
        const std::span<const std::uint32_t> stableIds = selection.SelectedStableIds();
        if (stableIds.empty())
            return false;

        std::vector<ECS::EntityHandle> entities;
        entities.reserve(stableIds.size());
        for (const std::uint32_t stableId : stableIds)
            entities.push_back(SelectionController::ToEntityHandle(stableId));

        return FocusCameraOnEntities(cameras, scene, entities, slot);
    }
} // namespace Extrinsic::Runtime
