// Point-cloud quality metrics shared by the LOP-family reference smokes
// (CLOP and LOP/WLOP): mean plane/sphere error, minimum spacing, finiteness.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>

#include <glm/glm.hpp>

namespace Intrinsic::Bench::Geometry::PointCloudMetrics
{
    [[nodiscard]] inline double MeanPlaneError(
        const std::span<const glm::vec3> points)
    {
        double sum = 0.0;
        for (const glm::vec3 point : points)
            sum += std::abs(static_cast<double>(point.z));
        return sum / static_cast<double>(points.size());
    }

    [[nodiscard]] inline double MeanSphereError(
        const std::span<const glm::vec3> points)
    {
        double sum = 0.0;
        for (const glm::vec3 point : points)
        {
            sum += std::abs(
                static_cast<double>(glm::length(point)) - 1.0);
        }
        return sum / static_cast<double>(points.size());
    }

    [[nodiscard]] inline double MinimumPairwiseDistance(
        const std::span<const glm::vec3> points)
    {
        double minimum = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0u; i < points.size(); ++i)
        {
            for (std::size_t j = i + 1u; j < points.size(); ++j)
            {
                minimum = std::min(
                    minimum,
                    static_cast<double>(
                        glm::distance(points[i], points[j])));
            }
        }
        return minimum;
    }

    [[nodiscard]] inline bool Finite(
        const std::span<const glm::vec3> points)
    {
        return std::ranges::all_of(points, [](const glm::vec3 point)
        {
            return std::isfinite(point.x) && std::isfinite(point.y) &&
                   std::isfinite(point.z);
        });
    }
}
