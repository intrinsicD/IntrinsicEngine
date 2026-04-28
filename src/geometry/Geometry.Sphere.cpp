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

            const glm::dvec3 ref = points.front();
            std::array<std::array<double, 3>, 3> ata{};
            std::array<double, 3> atb{};

            const double refNormSq = glm::dot(ref, ref);
            for (std::size_t i = 1; i < points.size(); ++i)
            {
                const glm::dvec3 rowVec = points[i] - ref;
                const std::array<double, 3> row{2.0 * rowVec.x, 2.0 * rowVec.y, 2.0 * rowVec.z};
                const double rhs = glm::dot(points[i], points[i]) - refNormSq;

                for (std::size_t r = 0; r < 3; ++r)
                {
                    atb[r] += row[r] * rhs;
                    for (std::size_t c = 0; c < 3; ++c)
                    {
                        ata[r][c] += row[r] * row[c];
                    }
                }
            }

            std::array<double, 3> center{};
            if (!Solver::SolveLinearSystem(ata, atb, center, static_cast<double>(params.SingularThreshold)))
            {
                return std::nullopt;
            }

            glm::dvec3 centerD{center[0], center[1], center[2]};
            double radius = 0.0;
            for (const glm::dvec3& p : points)
            {
                radius += glm::distance(p, centerD);
            }
            radius /= static_cast<double>(points.size());

            return FinalizeSphere(centerD, radius, points, params);
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