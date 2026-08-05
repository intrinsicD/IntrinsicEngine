module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

module Geometry.PointCloud.Kernels;

import Geometry.AABB;
import Geometry.KDTree;

namespace Geometry::PointCloud::Kernels
{
    namespace
    {
        [[nodiscard]] bool IsFinite(
            const glm::vec3 value) noexcept
        {
            return std::isfinite(value.x) &&
                   std::isfinite(value.y) &&
                   std::isfinite(value.z);
        }

        [[nodiscard]] bool IsValid(
            const KernelType kernel) noexcept
        {
            switch (kernel)
            {
            case KernelType::Gaussian:
            case KernelType::ThetaLop:
            case KernelType::WendlandC2:
                return true;
            }
            return false;
        }

        [[nodiscard]] bool IsValid(
            const DensityWeightMode mode) noexcept
        {
            switch (mode)
            {
            case DensityWeightMode::Direct:
            case DensityWeightMode::Reciprocal:
                return true;
            }
            return false;
        }

        [[nodiscard]] double DistanceSquared(
            const glm::vec3 lhs,
            const glm::vec3 rhs) noexcept
        {
            const double dx =
                static_cast<double>(lhs.x) - rhs.x;
            const double dy =
                static_cast<double>(lhs.y) - rhs.y;
            const double dz =
                static_cast<double>(lhs.z) - rhs.z;
            return dx * dx + dy * dy + dz * dz;
        }

        [[nodiscard]] DensityWeightResult InvalidRequest(
            const std::span<const glm::vec3> points,
            const double supportRadius,
            const KernelType kernel,
            const DensityWeightMode mode,
            const bool suppliedIndex)
        {
            DensityWeightResult result{};
            result.Diagnostics.PointCount = points.size();
            result.Diagnostics.UsedSuppliedIndex = suppliedIndex;
            if (points.empty())
            {
                result.Status = DensityWeightStatus::EmptyInput;
                return result;
            }
            if (!std::isfinite(supportRadius) ||
                !(supportRadius > 0.0) ||
                supportRadius >
                    static_cast<double>(
                        std::numeric_limits<float>::max()))
            {
                result.Status =
                    DensityWeightStatus::InvalidSupportRadius;
                return result;
            }
            if (!IsValid(kernel))
            {
                result.Status = DensityWeightStatus::InvalidKernel;
                return result;
            }
            if (!IsValid(mode))
            {
                result.Status = DensityWeightStatus::InvalidMode;
                return result;
            }
            if (points.size() >
                static_cast<std::size_t>(
                    std::numeric_limits<std::uint32_t>::max()))
            {
                result.Status = DensityWeightStatus::ResourceLimit;
                return result;
            }
            if (!std::ranges::all_of(points, IsFinite))
            {
                result.Status = DensityWeightStatus::NonFiniteInput;
                return result;
            }
            result.Status = DensityWeightStatus::Success;
            return result;
        }

        [[nodiscard]] bool MatchesPoints(
            const Geometry::KDTree& index,
            const std::span<const glm::vec3> points) noexcept
        {
            if (index.Nodes().empty() ||
                index.ElementAabbs().size() != points.size())
            {
                return false;
            }
            for (std::size_t i = 0u; i < points.size(); ++i)
            {
                const Geometry::AABB& box = index.ElementAabbs()[i];
                if (box.Min != points[i] || box.Max != points[i])
                    return false;
            }
            return true;
        }

        [[nodiscard]] DensityWeightResult ComputeWithIndex(
            const std::span<const glm::vec3> points,
            const Geometry::KDTree& index,
            const double supportRadius,
            const KernelType kernel,
            const DensityWeightMode mode,
            const bool suppliedIndex,
            Geometry::KDTree::RadiusQueryScratch* const scratch = nullptr)
        {
            DensityWeightResult result = InvalidRequest(
                points,
                supportRadius,
                kernel,
                mode,
                suppliedIndex);
            if (!result.Succeeded())
                return result;
            if (!MatchesPoints(index, points))
            {
                result.Status =
                    DensityWeightStatus::SpatialIndexMismatch;
                return result;
            }

            std::vector<float> weights(
                points.size(), 0.0f);
            std::vector<Geometry::KDTree::ElementIndex> neighbors{};
            for (std::size_t i = 0u; i < points.size(); ++i)
            {
                float queryRadius =
                    static_cast<float>(supportRadius);
                if (static_cast<double>(queryRadius) < supportRadius)
                {
                    queryRadius = std::nextafter(
                        queryRadius,
                        std::numeric_limits<float>::infinity());
                }
                const auto query = scratch != nullptr
                    ? index.QueryRadius(
                        points[i], queryRadius, neighbors, *scratch)
                    : index.QueryRadius(
                        points[i], queryRadius, neighbors);
                ++result.Diagnostics.QueryCount;
                if (!query.has_value())
                {
                    result.Status =
                        DensityWeightStatus::SpatialQueryFailed;
                    return result;
                }

                double density = 1.0;
                std::size_t contributionCount = 0u;
                for (const std::uint32_t neighbor : neighbors)
                {
                    if (neighbor == i || neighbor >= points.size())
                        continue;
                    const auto weight = Weight(
                        DistanceSquared(points[i], points[neighbor]),
                        supportRadius,
                        kernel);
                    if (!weight.has_value())
                    {
                        result.Status =
                            DensityWeightStatus::NumericalFailure;
                        return result;
                    }
                    if (!(*weight > 0.0))
                        continue;
                    density += *weight;
                    ++contributionCount;
                }
                result.Diagnostics.NeighborContributionCount +=
                    contributionCount;
                const double output =
                    mode == DensityWeightMode::Direct
                    ? density
                    : 1.0 / density;
                if (!std::isfinite(output) ||
                    output >
                        static_cast<double>(
                            std::numeric_limits<float>::max()))
                {
                    result.Status =
                        DensityWeightStatus::NumericalFailure;
                    return result;
                }
                weights[i] = static_cast<float>(output);
            }

            result.Status = DensityWeightStatus::Success;
            result.Weights = std::move(weights);
            return result;
        }
    }

    std::string_view DebugName(const KernelType kernel) noexcept
    {
        switch (kernel)
        {
        case KernelType::Gaussian: return "gaussian";
        case KernelType::ThetaLop: return "theta_lop";
        case KernelType::WendlandC2: return "wendland_c2";
        }
        return "invalid";
    }

    std::string_view DebugName(const DensityWeightMode mode) noexcept
    {
        switch (mode)
        {
        case DensityWeightMode::Direct: return "direct";
        case DensityWeightMode::Reciprocal: return "reciprocal";
        }
        return "invalid";
    }

    std::string_view DebugName(
        const DensityWeightStatus status) noexcept
    {
        switch (status)
        {
        case DensityWeightStatus::Success: return "success";
        case DensityWeightStatus::EmptyInput: return "empty_input";
        case DensityWeightStatus::InvalidSupportRadius:
            return "invalid_support_radius";
        case DensityWeightStatus::InvalidKernel: return "invalid_kernel";
        case DensityWeightStatus::InvalidMode: return "invalid_mode";
        case DensityWeightStatus::NonFiniteInput:
            return "non_finite_input";
        case DensityWeightStatus::ResourceLimit:
            return "resource_limit";
        case DensityWeightStatus::SpatialIndexBuildFailed:
            return "spatial_index_build_failed";
        case DensityWeightStatus::SpatialIndexMismatch:
            return "spatial_index_mismatch";
        case DensityWeightStatus::SpatialQueryFailed:
            return "spatial_query_failed";
        case DensityWeightStatus::EmptyNeighborhood:
            return "empty_neighborhood";
        case DensityWeightStatus::NumericalFailure:
            return "numerical_failure";
        }
        return "invalid";
    }

    std::optional<double> Weight(
        const double distanceSquared,
        const double supportRadius,
        const KernelType kernel) noexcept
    {
        if (!std::isfinite(distanceSquared) ||
            distanceSquared < 0.0 ||
            !std::isfinite(supportRadius) ||
            !(supportRadius > 0.0) ||
            !IsValid(kernel))
        {
            return std::nullopt;
        }

        if (distanceSquared == 0.0)
            return 1.0;

        const double distance = std::sqrt(distanceSquared);
        if (distance >= supportRadius)
            return 0.0;

        const double normalizedDistance =
            distance / supportRadius;
        const double normalizedSquared =
            normalizedDistance * normalizedDistance;
        double value = 0.0;
        switch (kernel)
        {
        case KernelType::Gaussian:
            value = std::exp(-8.0 * normalizedSquared);
            break;
        case KernelType::ThetaLop:
            value = std::exp(-16.0 * normalizedSquared);
            break;
        case KernelType::WendlandC2:
        {
            const double t = 1.0 - normalizedDistance;
            const double t2 = t * t;
            value = t2 * t2 *
                (1.0 + 4.0 * normalizedDistance);
            break;
        }
        }
        if (!std::isfinite(value) || value < 0.0)
            return std::nullopt;
        return value;
    }

    std::optional<double> DirectionalWeight(
        const glm::vec3 offset,
        const glm::vec3 direction,
        const double supportRadius) noexcept
    {
        if (!IsFinite(offset) || !IsFinite(direction) ||
            !std::isfinite(supportRadius) || !(supportRadius > 0.0))
        {
            return std::nullopt;
        }

        const glm::dvec3 delta{offset};
        const glm::dvec3 normal{direction};
        const double distanceSquared = glm::dot(delta, delta);
        const double normalLengthSquared = glm::dot(normal, normal);
        if (!std::isfinite(distanceSquared) || distanceSquared < 0.0 ||
            !std::isfinite(normalLengthSquared) ||
            !(normalLengthSquared > std::numeric_limits<double>::epsilon()))
        {
            return std::nullopt;
        }
        if (distanceSquared >= supportRadius * supportRadius)
            return 0.0;

        const double projection =
            glm::dot(normal, delta) / std::sqrt(normalLengthSquared);
        const double normalizedProjection = projection / supportRadius;
        const double value = std::exp(
            -normalizedProjection * normalizedProjection);
        if (!std::isfinite(value) || value < 0.0)
            return std::nullopt;
        return value;
    }

    std::optional<double> Repulsion(
        const double distance,
        const double supportRadius) noexcept
    {
        if (!std::isfinite(distance) || distance < 0.0 ||
            !std::isfinite(supportRadius) ||
            !(supportRadius > 0.0))
        {
            return std::nullopt;
        }
        const double value = -distance / supportRadius;
        if (!std::isfinite(value))
            return std::nullopt;
        return value;
    }

    std::optional<double> RepulsionDerivative(
        const double distance,
        const double supportRadius) noexcept
    {
        if (!std::isfinite(distance) || distance < 0.0 ||
            !std::isfinite(supportRadius) ||
            !(supportRadius > 0.0))
        {
            return std::nullopt;
        }
        const double value = -1.0 / supportRadius;
        if (!std::isfinite(value))
            return std::nullopt;
        return value;
    }

    DensityWeightResult ComputeDensityWeights(
        const std::span<const glm::vec3> points,
        const double supportRadius,
        const KernelType kernel,
        const DensityWeightMode mode)
    {
        DensityWeightResult result = InvalidRequest(
            points,
            supportRadius,
            kernel,
            mode,
            false);
        if (!result.Succeeded())
            return result;

        Geometry::KDTree index{};
        const auto build = index.BuildFromPoints(points);
        if (!build.has_value())
        {
            result.Status =
                DensityWeightStatus::SpatialIndexBuildFailed;
            return result;
        }
        return ComputeWithIndex(
            points,
            index,
            supportRadius,
            kernel,
            mode,
            false);
    }

    DensityWeightResult ComputeDensityWeights(
        const std::span<const glm::vec3> points,
        const Geometry::KDTree& index,
        const double supportRadius,
        const KernelType kernel,
        const DensityWeightMode mode)
    {
        return ComputeWithIndex(
            points,
            index,
            supportRadius,
            kernel,
            mode,
            true);
    }

    DensityWeightResult ComputeDensityWeights(
        const std::span<const glm::vec3> points,
        const Geometry::KDTree& index,
        const double supportRadius,
        const KernelType kernel,
        const DensityWeightMode mode,
        Geometry::KDTree::RadiusQueryScratch& scratch)
    {
        return ComputeWithIndex(
            points,
            index,
            supportRadius,
            kernel,
            mode,
            true,
            &scratch);
    }
}
