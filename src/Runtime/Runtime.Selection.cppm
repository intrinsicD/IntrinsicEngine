module;
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/entity.hpp>

export module Runtime.Selection;

import ECS;
import Geometry;
import Graphics;

export namespace Runtime::Selection
{
    enum class PickBackend : uint8_t
    {
        CPU = 0,
        GPU = 1
    };

    enum class PickMode : uint8_t
    {
        Replace = 0,
        Add = 1,
        Toggle = 2
    };

    struct PickRequest
    {
        Geometry::Ray WorldRay;
        PickBackend Backend = PickBackend::CPU;
        PickMode Mode = PickMode::Replace;

        // If true, also update HoveredTag (for UI hover highlight).
        bool UpdateHover = false;

        // Optional: max distance.
        float MaxDistance = std::numeric_limits<float>::infinity();
    };

    struct PickResult
    {
        entt::entity Entity = entt::null;
        float T = std::numeric_limits<float>::infinity();
    };

    // CPU picking: uses MeshCollider broadphase (WorldOBB) and watertight ray/triangle on mesh data.
    [[nodiscard]] PickResult PickCPU(const ECS::Scene& scene, const PickRequest& request);

    // Apply selection state changes on the registry (SelectedTag/HoveredTag).
    void ApplySelection(ECS::Scene& scene, entt::entity hitEntity, PickMode mode);
    void ApplyHover(ECS::Scene& scene, entt::entity hoveredEntity);

    // Helper: build a world ray from a normalized pixel coordinate in NDC (-1..1) using camera matrices.
    [[nodiscard]] Geometry::Ray RayFromNDC(const Graphics::CameraComponent& camera, const glm::vec2& ndc);
}
