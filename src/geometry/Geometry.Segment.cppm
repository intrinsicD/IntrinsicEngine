module;

#include <span>

#include <glm/glm.hpp>

export module Geometry.Segment;

export namespace Geometry
{
    struct Segment
    {
        glm::vec3 A{0.0f};
        glm::vec3 B{0.0f};

        [[nodiscard]] glm::vec3 GetCenter() const;
        [[nodiscard]] glm::vec3 GetDirection() const;
        [[nodiscard]] float GetLengthSquared() const;
        [[nodiscard]] float GetLength() const;
        [[nodiscard]] glm::vec3 GetPoint(float t) const;
    };

    [[nodiscard]] float ClosestPointParameter(const Segment& segment, const glm::vec3& point);
    [[nodiscard]] glm::vec3 ClosestPoint(const Segment& segment, const glm::vec3& point);
    [[nodiscard]] double SquaredDistance(const Segment& segment, const glm::vec3& point);
    [[nodiscard]] double Distance(const Segment& segment, const glm::vec3& point);
    [[nodiscard]] Segment ToSegment(std::span<const glm::vec3> points);
}
