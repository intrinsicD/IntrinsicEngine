module;
#include <cstdint>
#include <optional>
#include <glm/glm.hpp>

export module Geometry.Raycast;

import Geometry.Primitives;
import Geometry.Validation;
import Geometry.IntersectionClassification;

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

    // GEOM-007 Slice 3 — classifying companion to `RayTriangle_Watertight`.
    //
    // Returns a `Geometry::Intersection::RayTriangleResult` carrying the same
    // geometric data as `RayTriangle_Watertight` (ray parameter, barycentric
    // weights, hit point) plus the shared intersection-classification
    // diagnostics:
    //
    //  - `Kind::DegenerateInput`  ray is invalid or triangle has zero area.
    //  - `Kind::None`             ray misses the triangle, or hit is outside
    //                             `[tMin, tMax]`, or weights have mixed sign.
    //  - `Kind::Proper`           strict interior hit; `OnTriangle == Interior`.
    //  - `Kind::Touching`         hit on a triangle vertex or edge (the
    //                             `OnTriangle` field identifies which one).
    //
    // `OnRay` is `Origin` when the hit parameter is within a scale-aware
    // epsilon of zero, otherwise `Interior`.
    //
    // This entry point shares the numerical kernel with `RayTriangle_Watertight`
    // so callers migrating to the classification surface get bit-exact parity
    // on `T`, `U`, `V`, and hit-position for the cases both functions decide.
    // Adoption is tracked under GEOM-007 Slice 3; existing callers in
    // `src/legacy/Runtime/Runtime.Selection.cpp` and `Test_Raycast.cpp` may
    // continue to use `RayTriangle_Watertight` until their own migration
    // commit.
    [[nodiscard]] Intersection::RayTriangleResult
    RayTriangle_Classify(const Ray& ray, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                         float tMin = 0.0f, float tMax = std::numeric_limits<float>::infinity());
}



