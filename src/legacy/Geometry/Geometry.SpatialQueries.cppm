module;

#include <concepts>
#include <cstddef>
#include <cstdint>

export module Geometry.SpatialQueries;

import Geometry.AABB;
import Geometry.Overlap;

export namespace Geometry
{
    /// Concept satisfied by any shape for which TestOverlap(AABB, Shape) is defined.
    /// Shared by Octree, KDTree, and BVH to avoid repeated concept definitions.
    template <typename Shape>
    concept SpatialQueryShape =
        requires(const Shape& s, const AABB& box)
        {
            { TestOverlap(box, s) } -> std::convertible_to<bool>;
        };

    /// Common build-result fields shared across spatial index builders.
    struct SpatialBuildResult
    {
        std::size_t ElementCount{0};
        std::size_t NodeCount{0};
        std::uint32_t MaxDepthReached{0};
    };

    /// Common KNN query diagnostics shared across spatial indices.
    struct SpatialKNNResult
    {
        std::size_t ReturnedCount{0};
        std::size_t VisitedNodes{0};
        std::size_t DistanceEvaluations{0};
        float MaxDistanceSquared{0.0f};
    };

    /// Common radius query diagnostics shared across spatial indices.
    struct SpatialRadiusResult
    {
        std::size_t ReturnedCount{0};
        std::size_t VisitedNodes{0};
        std::size_t DistanceEvaluations{0};
    };
}
