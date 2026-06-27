module;

#include <algorithm>
#include <cmath>
#include <span>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

module Geometry.RotationAveraging;

import Geometry.Rotation;

namespace Geometry::Rotation
{
    namespace
    {
        [[nodiscard]] double WeightAt(std::span<const float> weights, std::size_t i, bool use)
        {
            return use ? static_cast<double>(weights[i]) : 1.0;
        }

        [[nodiscard]] bool FiniteMat(const glm::mat3& m)
        {
            for (int c = 0; c < 3; ++c)
                for (int r = 0; r < 3; ++r)
                    if (!std::isfinite(m[c][r])) return false;
            return true;
        }
    }

    glm::mat3 ChordalMean(std::span<const glm::mat3> rotations, std::span<const float> weights)
    {
        if (rotations.empty())
        {
            return glm::mat3(1.0f);
        }
        const bool useW = (weights.size() == rotations.size());
        glm::dmat3 sum(0.0);
        double total = 0.0;
        for (std::size_t i = 0; i < rotations.size(); ++i)
        {
            if (!FiniteMat(rotations[i])) continue;
            const double w = WeightAt(weights, i, useW);
            if (!std::isfinite(w)) continue;
            sum += glm::dmat3(rotations[i]) * w;
            total += w;
        }
        if (!(total > 0.0))
        {
            return glm::mat3(1.0f);
        }
        return ProjectOnSO3(glm::mat3(sum));
    }

    glm::mat3 QuaternionMean(std::span<const glm::mat3> rotations, std::span<const float> weights)
    {
        if (rotations.empty())
        {
            return glm::mat3(1.0f);
        }
        const bool useW = (weights.size() == rotations.size());
        glm::quat ref{};
        bool haveRef = false;
        glm::dvec4 acc(0.0);
        double total = 0.0;
        for (std::size_t i = 0; i < rotations.size(); ++i)
        {
            if (!FiniteMat(rotations[i])) continue;
            const double w = WeightAt(weights, i, useW);
            if (!std::isfinite(w)) continue;
            glm::quat q = glm::quat_cast(rotations[i]);
            if (!haveRef)
            {
                ref = q;
                haveRef = true;
            }
            // Hemisphere alignment: flip q to the same half-space as the reference.
            const double dot = static_cast<double>(q.w) * ref.w + static_cast<double>(q.x) * ref.x
                             + static_cast<double>(q.y) * ref.y + static_cast<double>(q.z) * ref.z;
            const double s = (dot < 0.0) ? -1.0 : 1.0;
            acc += glm::dvec4(s * q.w, s * q.x, s * q.y, s * q.z) * w;
            total += w;
        }
        if (!(total > 0.0))
        {
            return glm::mat3(1.0f);
        }
        const double norm = std::sqrt(glm::dot(acc, acc));
        if (!(norm > 0.0))
        {
            return glm::mat3(1.0f);
        }
        acc /= norm;
        const glm::quat mean(static_cast<float>(acc.x), static_cast<float>(acc.y),
                             static_cast<float>(acc.z), static_cast<float>(acc.w));
        return glm::mat3_cast(mean);
    }

    RotationAverageResult KarcherMean(std::span<const glm::mat3> rotations,
                                      std::span<const float> weights,
                                      int maxIterations,
                                      float tolerance)
    {
        RotationAverageResult result{};
        if (rotations.empty())
        {
            return result;
        }
        const bool useW = (weights.size() == rotations.size());
        glm::mat3 mean = ChordalMean(rotations, weights);

        for (int iter = 0; iter < maxIterations; ++iter)
        {
            result.Iterations = iter + 1;
            const glm::mat3 meanT = glm::transpose(mean);
            glm::dvec3 accum(0.0);
            double total = 0.0;
            for (std::size_t i = 0; i < rotations.size(); ++i)
            {
                if (!FiniteMat(rotations[i])) continue;
                const double w = WeightAt(weights, i, useW);
                if (!std::isfinite(w)) continue;
                accum += glm::dvec3(Log(meanT * rotations[i])) * w;
                total += w;
            }
            if (!(total > 0.0))
            {
                return result; // invalid
            }
            const glm::vec3 delta(accum / total);
            mean = mean * Exp(delta);
            if (glm::length(delta) < tolerance)
            {
                result.Converged = true;
                break;
            }
        }
        result.Rotation = mean;
        result.Valid = true;
        return result;
    }

    RotationAverageResult GeodesicMedian(std::span<const glm::mat3> rotations,
                                         std::span<const float> weights,
                                         int maxIterations,
                                         float tolerance)
    {
        RotationAverageResult result{};
        if (rotations.empty())
        {
            return result;
        }
        const bool useW = (weights.size() == rotations.size());
        // Seed from the Karcher (L2) mean.
        const RotationAverageResult seed = KarcherMean(rotations, weights, maxIterations, tolerance);
        if (!seed.Valid)
        {
            return result;
        }
        glm::mat3 median = seed.Rotation;
        constexpr double kEps = 1e-8;

        for (int iter = 0; iter < maxIterations; ++iter)
        {
            result.Iterations = iter + 1;
            const glm::mat3 medianT = glm::transpose(median);
            glm::dvec3 numer(0.0);
            double denom = 0.0;
            for (std::size_t i = 0; i < rotations.size(); ++i)
            {
                if (!FiniteMat(rotations[i])) continue;
                const double w = WeightAt(weights, i, useW);
                if (!std::isfinite(w)) continue;
                const glm::dvec3 tangent(Log(medianT * rotations[i]));
                const double dist = glm::length(tangent);
                const double inv = w / std::max(dist, kEps); // Weiszfeld reweight
                numer += tangent * inv;
                denom += inv;
            }
            if (!(denom > 0.0))
            {
                return result;
            }
            const glm::vec3 delta(numer / denom);
            median = median * Exp(delta);
            if (glm::length(delta) < tolerance)
            {
                result.Converged = true;
                break;
            }
        }
        result.Rotation = median;
        result.Valid = true;
        return result;
    }
}
