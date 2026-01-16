module;
#include <cstdint>
#include <optional>
#include <glm/glm.hpp>

export module Geometry:Raycast;

import :Primitives;
import :Validation;

export namespace Geometry
{
    struct RayTriangleHit
    {
        float T = std::numeric_limits<float>::infinity();
        float U = 0.0f;
        float V = 0.0f;
    };

    // Watertight ray-triangle test (WoP / Ize style).
    // Returns the closest positive hit along the ray.
    // Notes:
    // - Robust to edge hits and shared edges (reduces cracks).
    // - Handles degenerate triangles by returning nullopt.
    [[nodiscard]] std::optional<RayTriangleHit>
    RayTriangle_Watertight(const Ray& ray, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                           float tMin = 0.0f, float tMax = std::numeric_limits<float>::infinity());
}

