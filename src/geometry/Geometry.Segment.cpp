module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

module Geometry.Segment;

import Geometry.PCA;

namespace Geometry
{
    glm::vec3 Segment::GetCenter() const
    {
        return (A + B) * 0.5f;
    }

    glm::vec3 Segment::GetDirection() const
    {
        return B - A;
    }

    float Segment::GetLengthSquared() const
    {
        return glm::distance2(A, B);
    }

    float Segment::GetLength() const
    {
        return glm::distance(A, B);
    }

    glm::vec3 Segment::GetPoint(float t) const
    {
        return A + (B - A) * t;
    }

    float ClosestPointParameter(const Segment& segment, const glm::vec3& point)
    {
        const glm::vec3 ab = segment.B - segment.A;
        const float abLenSq = glm::dot(ab, ab);
        if (abLenSq <= 1e-12f)
        {
            return 0.0f;
        }
        return glm::clamp(glm::dot(point - segment.A, ab) / abLenSq, 0.0f, 1.0f);
    }

    glm::vec3 ClosestPoint(const Segment& segment, const glm::vec3& point)
    {
        return segment.GetPoint(ClosestPointParameter(segment, point));
    }

    double SquaredDistance(const Segment& segment, const glm::vec3& point)
    {
        const glm::vec3 delta = point - ClosestPoint(segment, point);
        return static_cast<double>(glm::dot(delta, delta));
    }

    double Distance(const Segment& segment, const glm::vec3& point)
    {
        return std::sqrt(SquaredDistance(segment, point));
    }

    Segment ToSegment(std::span<const glm::vec3> points)
    {
        const auto isFinite = [](const glm::vec3& point)
        {
            return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
        };

        Segment segment{};

        glm::vec3 firstFinite{0.0f};
        bool hasFirstFinite = false;
        std::size_t finiteCount = 0;
        for (const glm::vec3& point : points)
        {
            if (!isFinite(point))
            {
                continue;
            }

            if (!hasFirstFinite)
            {
                firstFinite = point;
                hasFirstFinite = true;
            }
            ++finiteCount;
        }

        if (!hasFirstFinite)
        {
            return segment;
        }

        if (finiteCount == 1)
        {
            segment.A = firstFinite;
            segment.B = firstFinite;
            return segment;
        }

        const PCAResult pca = ToPCA(points);
        glm::vec3 axis = glm::normalize(glm::vec3{pca.Eigenvectors[0]});
        if (!std::isfinite(axis.x) || !std::isfinite(axis.y) || !std::isfinite(axis.z)
            || glm::dot(axis, axis) <= 1.0e-12f)
        {
            axis = glm::vec3{1.0f, 0.0f, 0.0f};
        }

        float minProjection = std::numeric_limits<float>::max();
        float maxProjection = -std::numeric_limits<float>::max();
        for (const glm::vec3& point : points)
        {
            if (!isFinite(point))
            {
                continue;
            }

            const float projection = glm::dot(point - pca.Mean, axis);
            minProjection = std::min(minProjection, projection);
            maxProjection = std::max(maxProjection, projection);
        }

        if (!(minProjection <= maxProjection))
        {
            segment.A = pca.Mean;
            segment.B = pca.Mean;
            return segment;
        }

        segment.A = pca.Mean + axis * minProjection;
        segment.B = pca.Mean + axis * maxProjection;
        return segment;
    }
}
