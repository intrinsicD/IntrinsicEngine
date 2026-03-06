module;

#include <cmath>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

export module Geometry:Segment;

export namespace Geometry
{
    struct Segment
    {
        glm::vec3 A{0.0f};
        glm::vec3 B{0.0f};

        [[nodiscard]] glm::vec3 GetCenter() const
        {
            return (A + B) * 0.5f;
        }

        [[nodiscard]] glm::vec3 GetDirection() const
        {
            return B - A;
        }

        [[nodiscard]] float GetLengthSquared() const
        {
            return glm::distance2(A, B);
        }

        [[nodiscard]] float GetLength() const
        {
            return glm::distance(A, B);
        }

        [[nodiscard]] glm::vec3 GetPoint(float t) const
        {
            return A + (B - A) * t;
        }
    };

    [[nodiscard]] float ClosestPointParameter(const Segment& segment, const glm::vec3& point)
    {
        const glm::vec3 ab = segment.B - segment.A;
        const float abLenSq = glm::dot(ab, ab);
        if (abLenSq <= 1e-12f)
        {
            return 0.0f;
        }
        return glm::clamp(glm::dot(point - segment.A, ab) / abLenSq, 0.0f, 1.0f);
    }

    [[nodiscard]] glm::vec3 ClosestPoint(const Segment& segment, const glm::vec3& point)
    {
        return segment.GetPoint(ClosestPointParameter(segment, point));
    }

    [[nodiscard]] double SquaredDistance(const Segment& segment, const glm::vec3& point)
    {
        const glm::vec3 delta = point - ClosestPoint(segment, point);
        return static_cast<double>(glm::dot(delta, delta));
    }

    [[nodiscard]] double Distance(const Segment& segment, const glm::vec3& point)
    {
        return std::sqrt(SquaredDistance(segment, point));
    }
}
