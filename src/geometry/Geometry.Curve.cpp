module;

#include <cmath>
#include <optional>
#include <span>
#include <vector>
#include <glm/glm.hpp>

module Geometry.Curve;

namespace Geometry
{
    namespace
    {
        [[nodiscard]] bool ValidParam(std::span<const glm::vec3> controlPoints, float t)
        {
            return !controlPoints.empty() && std::isfinite(t) && t >= 0.0f && t <= 1.0f;
        }
    }

    std::optional<glm::vec3> EvaluateBezier(std::span<const glm::vec3> controlPoints, float t)
    {
        if (!ValidParam(controlPoints, t))
        {
            return std::nullopt;
        }

        // de Casteljau: repeatedly lerp adjacent points in double precision.
        std::vector<glm::dvec3> work;
        work.reserve(controlPoints.size());
        for (const glm::vec3& p : controlPoints)
        {
            work.emplace_back(p);
        }

        const double td = static_cast<double>(t);
        for (std::size_t level = work.size(); level > 1; --level)
        {
            for (std::size_t i = 0; i + 1 < level; ++i)
            {
                work[i] = work[i] * (1.0 - td) + work[i + 1] * td;
            }
        }
        return glm::vec3{static_cast<float>(work[0].x), static_cast<float>(work[0].y), static_cast<float>(work[0].z)};
    }

    std::optional<glm::vec3> EvaluateBezierBernstein(std::span<const glm::vec3> controlPoints, float t)
    {
        if (!ValidParam(controlPoints, t))
        {
            return std::nullopt;
        }

        const std::size_t n = controlPoints.size() - 1; // degree
        const double td = static_cast<double>(t);
        const double omt = 1.0 - td;

        glm::dvec3 acc{0.0};
        // Bernstein basis B_{i,n}(t) = C(n,i) t^i (1-t)^(n-i), computed with an
        // incrementally maintained binomial coefficient.
        double binom = 1.0; // C(n, 0)
        for (std::size_t i = 0; i <= n; ++i)
        {
            const double basis = binom * std::pow(td, static_cast<double>(i)) * std::pow(omt, static_cast<double>(n - i));
            acc += glm::dvec3{controlPoints[i]} * basis;
            // C(n, i+1) = C(n, i) * (n - i) / (i + 1)
            binom = binom * static_cast<double>(n - i) / static_cast<double>(i + 1);
        }
        return glm::vec3{static_cast<float>(acc.x), static_cast<float>(acc.y), static_cast<float>(acc.z)};
    }

    std::optional<glm::vec3> EvaluateBezierDerivative(std::span<const glm::vec3> controlPoints, float t)
    {
        if (!ValidParam(controlPoints, t))
        {
            return std::nullopt;
        }
        const std::size_t count = controlPoints.size();
        if (count == 1)
        {
            return glm::vec3{0.0f};
        }

        // The derivative of a degree-n Bézier is a degree-(n-1) Bézier over the
        // scaled difference control points n*(P_{i+1} - P_i). Evaluate that via
        // de Casteljau.
        const std::size_t n = count - 1;
        std::vector<glm::dvec3> work;
        work.reserve(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            work.emplace_back((glm::dvec3{controlPoints[i + 1]} - glm::dvec3{controlPoints[i]}) * static_cast<double>(n));
        }

        const double td = static_cast<double>(t);
        for (std::size_t level = work.size(); level > 1; --level)
        {
            for (std::size_t i = 0; i + 1 < level; ++i)
            {
                work[i] = work[i] * (1.0 - td) + work[i + 1] * td;
            }
        }
        return glm::vec3{static_cast<float>(work[0].x), static_cast<float>(work[0].y), static_cast<float>(work[0].z)};
    }
}
