module;

#include <limits>

#include <glm/glm.hpp>

export module Geometry.Queries;

import Geometry.Ray;
import Geometry.Segment;

export namespace Geometry
{
    struct RaySegmentResult
    {
        float DistanceSq = std::numeric_limits<float>::infinity();
        float RayT = 0.0f;
        float SegmentT = 0.0f;
        glm::vec3 PointOnRay{0.0f};
        glm::vec3 PointOnSegment{0.0f};
    };

    [[nodiscard]] RaySegmentResult ClosestRaySegment(const Ray& ray,
                                                     const glm::vec3& segA,
                                                     const glm::vec3& segB);

    struct PointSegmentResult
    {
        float DistanceSq = std::numeric_limits<float>::infinity();
        float SegmentT = 0.0f;
        glm::vec3 ClosestPoint{0.0f};
    };

    [[nodiscard]] PointSegmentResult ClosestPointSegment(
        const glm::vec3& point, const glm::vec3& segA, const glm::vec3& segB);

    struct PointRayResult
    {
        float DistanceSq = std::numeric_limits<float>::infinity();
        float RayT = 0.0f;
        glm::vec3 ClosestPoint{0.0f};
    };

    [[nodiscard]] PointRayResult ClosestPointRay(const glm::vec3& point, const Ray& ray);
}
