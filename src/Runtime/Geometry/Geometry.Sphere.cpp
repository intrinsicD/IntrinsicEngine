module;

#include <glm/glm.hpp>
#include <optional>
#include <vector>
#include <span>

module Geometry.Sphere;

import Geometry.LinearSolver;

namespace Geometry
{
namespace SphereFitDetail
    {
        [[nodiscard]] bool IsFinite(const glm::vec3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        [[nodiscard]] std::optional<Sphere> FinalizeSphere(
            const glm::dvec3& center,
            double radius,
            std::span<const glm::dvec3> points,
            const FittingParams& params)
        {
            if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) || !std::isfinite(radius))
            {
                return std::nullopt;
            }

            const double minRadius = std::max(0.0, static_cast<double>(params.MinimumRadius));
            radius = std::max(radius, minRadius);

            if (params.EnforceContainment)
            {
                const double slack = std::max(0.0, static_cast<double>(params.ContainmentSlack));
                for (const glm::dvec3& p : points)
                {
                    radius = std::max(radius, glm::distance(p, center) + slack);
                }
            }

            if (!(radius > 0.0) || !std::isfinite(radius) || radius > static_cast<double>(std::numeric_limits<float>::max()))
            {
                return std::nullopt;
            }

            Sphere result;
            result.Center = glm::vec3(center);
            result.Radius = static_cast<float>(radius);
            return result;
        }

        [[nodiscard]] std::optional<Sphere> MakeBoundingSphere(
            std::span<const glm::dvec3> points,
            const FittingParams& params)
        {
            if (points.empty())
            {
                return std::nullopt;
            }

            glm::dvec3 bbMin(std::numeric_limits<double>::max());
            glm::dvec3 bbMax(-std::numeric_limits<double>::max());
            for (const glm::dvec3& p : points)
            {
                bbMin = glm::min(bbMin, p);
                bbMax = glm::max(bbMax, p);
            }

            const glm::dvec3 center = 0.5 * (bbMin + bbMax);
            double radius = 0.0;
            for (const glm::dvec3& p : points)
            {
                radius = std::max(radius, glm::distance(center, p));
            }

            return FinalizeSphere(center, radius, points, params);
        }

        [[nodiscard]] std::optional<Sphere> MakeDiameterSphere(
            const glm::dvec3& a,
            const glm::dvec3& b,
            const FittingParams& params,
            std::span<const glm::dvec3> allPoints)
        {
            const glm::dvec3 center = 0.5 * (a + b);
            const double radius = 0.5 * glm::distance(a, b);
            return FinalizeSphere(center, radius, allPoints, params);
        }

        [[nodiscard]] std::optional<Sphere> MakeCircleSphere(
            const glm::dvec3& a,
            const glm::dvec3& b,
            const glm::dvec3& c,
            const FittingParams& params,
            std::span<const glm::dvec3> allPoints)
        {
            const glm::dvec3 ab = b - a;
            const glm::dvec3 ac = c - a;
            const glm::dvec3 n = glm::cross(ab, ac);
            const double nLen2 = glm::dot(n, n);
            const double scale2 = std::max(glm::dot(ab, ab), glm::dot(ac, ac));
            const double eps = std::max(static_cast<double>(params.SingularThreshold), 1.0e-14 * std::max(1.0, scale2));

            if (!(nLen2 > eps * eps))
            {
                const double dab = glm::dot(ab, ab);
                const double dac = glm::dot(ac, ac);
                const double dbc = glm::dot(c - b, c - b);

                if (dab >= dac && dab >= dbc)
                {
                    return MakeDiameterSphere(a, b, params, allPoints);
                }
                if (dac >= dab && dac >= dbc)
                {
                    return MakeDiameterSphere(a, c, params, allPoints);
                }
                return MakeDiameterSphere(b, c, params, allPoints);
            }

            const double inv2NLen2 = 0.5 / nLen2;
            const glm::dvec3 center = a
                + (glm::cross(n, ab) * glm::dot(ac, ac)
                + glm::cross(ac, n) * glm::dot(ab, ab)) * inv2NLen2;
            const double radius = glm::distance(center, a);
            return FinalizeSphere(center, radius, allPoints, params);
        }

        [[nodiscard]] std::optional<Sphere> FitLeastSquares(
            std::span<const glm::dvec3> points,
            const FittingParams& params)
        {
            if (points.empty())
            {
                return std::nullopt;
            }

            if (points.size() == 1)
            {
                return FinalizeSphere(points.front(), 0.0, points, params);
            }

            if (points.size() == 2)
            {
                return MakeDiameterSphere(points[0], points[1], params, points);
            }

            if (points.size() == 3)
            {
                return MakeCircleSphere(points[0], points[1], points[2], params, points);
            }

            std::array<std::array<double, 4>, 4> ata{};
            std::array<double, 4> atb{};

            for (const glm::dvec3& p : points)
            {
                const double rhs = glm::dot(p, p);
                const std::array<double, 4> row{2.0 * p.x, 2.0 * p.y, 2.0 * p.z, 1.0};
                for (std::size_t i = 0; i < 4; ++i)
                {
                    atb[i] += row[i] * rhs;
                    for (std::size_t j = 0; j < 4; ++j)
                    {
                        ata[i][j] += row[i] * row[j];
                    }
                }
            }

            std::array<double, 4> solution{};
            if (!Solver::SolveLinearSystem(ata, atb, solution, static_cast<double>(params.SingularThreshold)))
            {
                return std::nullopt;
            }

            const glm::dvec3 center{solution[0], solution[1], solution[2]};
            const double radiusSq = glm::dot(center, center) + solution[3];
            if (!(radiusSq >= 0.0) || !std::isfinite(radiusSq))
            {
                return std::nullopt;
            }

            return FinalizeSphere(center, std::sqrt(radiusSq), points, params);
        }
    }

    [[nodiscard]] std::optional<Sphere> ToSphere(std::span<const glm::vec3> points, const FittingParams& params)
    {
        std::vector<glm::dvec3> samples;
        samples.reserve(points.size());

        for (const glm::vec3& p : points)
        {
            if (!SphereFitDetail::IsFinite(p))
            {
                continue;
            }

            samples.emplace_back(p);
        }

        if (samples.empty() || params.Method == FittingParams::FittingMethod::None)
        {
            return std::nullopt;
        }

        if (params.Method == FittingParams::FittingMethod::Bounding)
        {
            return SphereFitDetail::MakeBoundingSphere(samples, params);
        }

        auto fit = SphereFitDetail::FitLeastSquares(samples, params);
        if (!fit.has_value() && params.Method == FittingParams::FittingMethod::Hybrid)
        {
            fit = SphereFitDetail::MakeBoundingSphere(samples, params);
        }

        return fit;
    }
}