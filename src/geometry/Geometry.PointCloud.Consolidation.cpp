module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

module Geometry.PointCloud.Consolidation;

import Geometry.KDTree;
import Geometry.PointCloud;
import Geometry.PointCloud.Kernels;
import Geometry.PointCloud.Utils;

namespace Geometry::PointCloud::Consolidation
{
    namespace
    {
        namespace Kernels = Geometry::PointCloud::Kernels;

        [[nodiscard]] bool IsFinite(const glm::vec3 value) noexcept
        {
            return std::isfinite(value.x) &&
                   std::isfinite(value.y) &&
                   std::isfinite(value.z);
        }

        [[nodiscard]] double DistanceSquared(
            const glm::vec3 lhs,
            const glm::vec3 rhs) noexcept
        {
            const double dx = static_cast<double>(lhs.x) - rhs.x;
            const double dy = static_cast<double>(lhs.y) - rhs.y;
            const double dz = static_cast<double>(lhs.z) - rhs.z;
            return dx * dx + dy * dy + dz * dz;
        }

        [[nodiscard]] bool ToFiniteVec3(
            const glm::dvec3 value,
            glm::vec3& out) noexcept
        {
            constexpr double limit =
                static_cast<double>(std::numeric_limits<float>::max());
            if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
                !std::isfinite(value.z) ||
                std::abs(value.x) > limit || std::abs(value.y) > limit ||
                std::abs(value.z) > limit)
            {
                return false;
            }
            out = glm::vec3(value);
            return IsFinite(out);
        }

        [[nodiscard]] float BroadPhaseRadius(
            const double supportRadius) noexcept
        {
            float radius = static_cast<float>(supportRadius);
            if (static_cast<double>(radius) < supportRadius)
            {
                radius = std::nextafter(
                    radius, std::numeric_limits<float>::infinity());
            }
            return radius;
        }

        [[nodiscard]] Result InvalidRequest(
            const std::span<const glm::vec3> positions,
            const Params& params)
        {
            Result result{};
            result.Diagnostics.Strategy = Kind(params.Method);
            result.Diagnostics.InputPointCount = positions.size();
            result.Diagnostics.UsedDensityWeighting =
                std::holds_alternative<WlopStrategy>(params.Method);
            if (positions.empty())
            {
                result.State = Status::EmptyInput;
                return result;
            }
            if (positions.size() < 2u)
            {
                result.State = Status::TooFewPoints;
                return result;
            }
            if (positions.size() > static_cast<std::size_t>(
                    std::numeric_limits<std::uint32_t>::max()))
            {
                result.State = Status::ResourceLimit;
                return result;
            }
            if (!std::ranges::all_of(positions, IsFinite))
            {
                result.State = Status::NonFiniteInput;
                return result;
            }
            if (!std::isfinite(params.SupportRadius) ||
                !(params.SupportRadius > 0.0) ||
                params.SupportRadius > static_cast<double>(
                    std::numeric_limits<float>::max()))
            {
                result.State = Status::InvalidSupportRadius;
                return result;
            }
            if (!std::isfinite(params.RepulsionWeight) ||
                params.RepulsionWeight < 0.0 ||
                !(params.RepulsionWeight < 0.5))
            {
                result.State = Status::InvalidRepulsionWeight;
                return result;
            }
            if (params.MaxIterations == 0u)
            {
                result.State = Status::InvalidIterationLimit;
                return result;
            }
            if (!std::isfinite(params.ConvergenceTolerance) ||
                params.ConvergenceTolerance < 0.0)
            {
                result.State = Status::InvalidConvergenceTolerance;
                return result;
            }
            const std::size_t target = params.TargetPointCount == 0u
                ? positions.size()
                : params.TargetPointCount;
            if (target < 2u || target > positions.size() ||
                target > static_cast<std::size_t>(
                    std::numeric_limits<std::uint32_t>::max()))
            {
                result.State = Status::InvalidTargetCount;
                return result;
            }
            result.State = Status::Success;
            return result;
        }

        [[nodiscard]] bool BuildIndex(
            const std::span<const glm::vec3> positions,
            Geometry::KDTree& index) noexcept
        {
            return index.BuildFromPoints(positions).has_value();
        }

        [[nodiscard]] bool QueryNeighbors(
            const Geometry::KDTree& index,
            const glm::vec3 point,
            const double supportRadius,
            std::vector<Geometry::KDTree::ElementIndex>& neighbors) noexcept
        {
            return index.QueryRadius(
                point, BroadPhaseRadius(supportRadius), neighbors)
                .has_value();
        }

        [[nodiscard]] bool InitializeProjected(
            const std::span<const glm::vec3> positions,
            const Params& params,
            std::vector<glm::vec3>& projected)
        {
            Cloud cloud{};
            cloud.Reserve(positions.size());
            for (const glm::vec3 point : positions)
                static_cast<void>(cloud.AddPoint(point));

            const std::size_t target = params.TargetPointCount == 0u
                ? positions.size()
                : params.TargetPointCount;
            const auto sample = RandomSubsample(
                cloud,
                SubsampleParams{
                    .TargetCount = target,
                    .Seed = params.Seed,
                });
            if (!sample.has_value())
                return false;
            projected.assign(
                sample->Subsampled.Positions().begin(),
                sample->Subsampled.Positions().end());
            return projected.size() == target;
        }

        [[nodiscard]] bool L2Initialize(
            const std::span<const glm::vec3> source,
            const Geometry::KDTree& sourceIndex,
            const std::span<const float> sourceWeights,
            const double supportRadius,
            std::vector<glm::vec3>& projected,
            Diagnostics& diagnostics,
            Status& failure)
        {
            std::vector<glm::vec3> initialized(projected.size());
            std::vector<Geometry::KDTree::ElementIndex> neighbors{};
            for (std::size_t i = 0u; i < projected.size(); ++i)
            {
                if (!QueryNeighbors(
                        sourceIndex, projected[i], supportRadius, neighbors))
                {
                    failure = Status::SpatialQueryFailed;
                    return false;
                }
                glm::dvec3 sum{0.0};
                double weightSum = 0.0;
                for (const auto neighbor : neighbors)
                {
                    if (neighbor >= source.size())
                        continue;
                    const auto radial = Kernels::Weight(
                        DistanceSquared(projected[i], source[neighbor]),
                        supportRadius,
                        Kernels::KernelType::ThetaLop);
                    if (!radial.has_value())
                    {
                        failure = Status::NumericalFailure;
                        return false;
                    }
                    const double weight = *radial * sourceWeights[neighbor];
                    if (!(weight > 0.0))
                        continue;
                    sum += glm::dvec3(source[neighbor]) * weight;
                    weightSum += weight;
                    ++diagnostics.AttractionContributionCount;
                }
                if (!(weightSum > 0.0) || !std::isfinite(weightSum))
                {
                    ++diagnostics.EmptyNeighborhoodCount;
                    failure = Status::EmptyNeighborhood;
                    return false;
                }
                if (!ToFiniteVec3(sum / weightSum, initialized[i]))
                {
                    failure = Status::NumericalFailure;
                    return false;
                }
            }
            projected = std::move(initialized);
            return true;
        }

        [[nodiscard]] bool Iterate(
            const std::span<const glm::vec3> source,
            const Geometry::KDTree& sourceIndex,
            const std::span<const float> sourceWeights,
            const Params& params,
            std::vector<glm::vec3>& projected,
            Diagnostics& diagnostics,
            Status& failure)
        {
            Geometry::KDTree projectedIndex{};
            if (!BuildIndex(projected, projectedIndex))
            {
                failure = Status::SpatialIndexBuildFailed;
                return false;
            }

            std::vector<float> projectedWeights(projected.size(), 1.0f);
            if (diagnostics.UsedDensityWeighting)
            {
                const auto density = Kernels::ComputeDensityWeights(
                    projected,
                    projectedIndex,
                    params.SupportRadius,
                    Kernels::KernelType::ThetaLop,
                    Kernels::DensityWeightMode::Direct);
                if (!density.Succeeded())
                {
                    failure = density.Status ==
                            Kernels::DensityWeightStatus::EmptyNeighborhood
                        ? Status::EmptyNeighborhood
                        : Status::DensityEstimationFailed;
                    diagnostics.EmptyNeighborhoodCount +=
                        density.Diagnostics.EmptyNeighborhoodCount;
                    return false;
                }
                projectedWeights = density.Weights;
                diagnostics.DensityContributionCount +=
                    density.Diagnostics.NeighborContributionCount;
            }

            const double distanceFloor = std::max(
                params.SupportRadius * 1.0e-2,
                std::numeric_limits<double>::min());
            std::vector<glm::vec3> next(projected.size());
            std::vector<Geometry::KDTree::ElementIndex> neighbors{};
            double displacementSum = 0.0;
            double maxDisplacement = 0.0;

            for (std::size_t i = 0u; i < projected.size(); ++i)
            {
                if (!QueryNeighbors(sourceIndex, projected[i],
                                    params.SupportRadius, neighbors))
                {
                    failure = Status::SpatialQueryFailed;
                    return false;
                }

                glm::dvec3 attraction{0.0};
                double attractionWeight = 0.0;
                for (const auto neighbor : neighbors)
                {
                    if (neighbor >= source.size())
                        continue;
                    const double distanceSquared =
                        DistanceSquared(projected[i], source[neighbor]);
                    const auto radial = Kernels::Weight(
                        distanceSquared,
                        params.SupportRadius,
                        Kernels::KernelType::ThetaLop);
                    if (!radial.has_value())
                    {
                        failure = Status::NumericalFailure;
                        return false;
                    }
                    const double distance = std::sqrt(distanceSquared);
                    const double weight = *radial * sourceWeights[neighbor] /
                        std::max(distance, distanceFloor);
                    if (!(weight > 0.0) || !std::isfinite(weight))
                        continue;
                    attraction += glm::dvec3(source[neighbor]) * weight;
                    attractionWeight += weight;
                    ++diagnostics.AttractionContributionCount;
                }
                if (!(attractionWeight > 0.0) ||
                    !std::isfinite(attractionWeight))
                {
                    ++diagnostics.EmptyNeighborhoodCount;
                    failure = Status::EmptyNeighborhood;
                    return false;
                }

                if (!QueryNeighbors(projectedIndex, projected[i],
                                    params.SupportRadius, neighbors))
                {
                    failure = Status::SpatialQueryFailed;
                    return false;
                }
                glm::dvec3 repulsion{0.0};
                double repulsionWeight = 0.0;
                for (const auto neighbor : neighbors)
                {
                    if (neighbor == i || neighbor >= projected.size())
                        continue;
                    const glm::dvec3 delta =
                        glm::dvec3(projected[i]) -
                        glm::dvec3(projected[neighbor]);
                    const double distanceSquared = glm::dot(delta, delta);
                    const auto radial = Kernels::Weight(
                        distanceSquared,
                        params.SupportRadius,
                        Kernels::KernelType::ThetaLop);
                    const auto derivative = Kernels::RepulsionDerivative(
                        std::sqrt(distanceSquared), params.SupportRadius);
                    if (!radial.has_value() || !derivative.has_value())
                    {
                        failure = Status::NumericalFailure;
                        return false;
                    }
                    const double distance = std::sqrt(distanceSquared);
                    const double weight = *radial * std::abs(*derivative) *
                        projectedWeights[neighbor] /
                        std::max(distance, distanceFloor);
                    if (!(weight > 0.0) || !std::isfinite(weight))
                        continue;
                    repulsion += delta * weight;
                    repulsionWeight += weight;
                    ++diagnostics.RepulsionContributionCount;
                }

                glm::dvec3 updated = attraction / attractionWeight;
                if (params.RepulsionWeight > 0.0 &&
                    repulsionWeight > 0.0)
                {
                    updated += params.RepulsionWeight *
                        (repulsion / repulsionWeight);
                }
                if (!ToFiniteVec3(updated, next[i]))
                {
                    failure = Status::NumericalFailure;
                    return false;
                }
                const double displacement = std::sqrt(
                    DistanceSquared(projected[i], next[i]));
                displacementSum += displacement;
                maxDisplacement = std::max(maxDisplacement, displacement);
            }

            projected = std::move(next);
            diagnostics.AverageDisplacement =
                displacementSum / static_cast<double>(projected.size());
            diagnostics.MaxDisplacement = maxDisplacement;
            return true;
        }
    }

    std::string_view DebugName(const StrategyKind strategy) noexcept
    {
        switch (strategy)
        {
        case StrategyKind::Lop: return "lop";
        case StrategyKind::Wlop: return "wlop";
        }
        return "invalid";
    }

    std::string_view DebugName(const Status status) noexcept
    {
        switch (status)
        {
        case Status::Success: return "success";
        case Status::EmptyInput: return "empty_input";
        case Status::TooFewPoints: return "too_few_points";
        case Status::InvalidCloud: return "invalid_cloud";
        case Status::NonFiniteInput: return "non_finite_input";
        case Status::InvalidSupportRadius: return "invalid_support_radius";
        case Status::InvalidRepulsionWeight: return "invalid_repulsion_weight";
        case Status::InvalidIterationLimit: return "invalid_iteration_limit";
        case Status::InvalidConvergenceTolerance:
            return "invalid_convergence_tolerance";
        case Status::InvalidTargetCount: return "invalid_target_count";
        case Status::ResourceLimit: return "resource_limit";
        case Status::SpatialIndexBuildFailed:
            return "spatial_index_build_failed";
        case Status::SpatialQueryFailed: return "spatial_query_failed";
        case Status::EmptyNeighborhood: return "empty_neighborhood";
        case Status::DensityEstimationFailed:
            return "density_estimation_failed";
        case Status::NumericalFailure: return "numerical_failure";
        case Status::NotConverged: return "not_converged";
        }
        return "invalid";
    }

    StrategyKind Kind(const Strategy& strategy) noexcept
    {
        return std::holds_alternative<LopStrategy>(strategy)
            ? StrategyKind::Lop
            : StrategyKind::Wlop;
    }

    Result Consolidate(
        const std::span<const glm::vec3> positions,
        const Params& params)
    {
        Result result = InvalidRequest(positions, params);
        if (!result.Succeeded())
            return result;

        Geometry::KDTree sourceIndex{};
        if (!BuildIndex(positions, sourceIndex))
        {
            result.State = Status::SpatialIndexBuildFailed;
            return result;
        }

        std::vector<float> sourceWeights(positions.size(), 1.0f);
        if (result.Diagnostics.UsedDensityWeighting)
        {
            const auto density = Kernels::ComputeDensityWeights(
                positions,
                sourceIndex,
                params.SupportRadius,
                Kernels::KernelType::ThetaLop,
                Kernels::DensityWeightMode::Reciprocal);
            if (!density.Succeeded())
            {
                result.State = density.Status ==
                        Kernels::DensityWeightStatus::EmptyNeighborhood
                    ? Status::EmptyNeighborhood
                    : Status::DensityEstimationFailed;
                result.Diagnostics.EmptyNeighborhoodCount +=
                    density.Diagnostics.EmptyNeighborhoodCount;
                return result;
            }
            sourceWeights = density.Weights;
            result.Diagnostics.DensityContributionCount +=
                density.Diagnostics.NeighborContributionCount;
        }

        std::vector<glm::vec3> projected{};
        if (!InitializeProjected(positions, params, projected))
        {
            result.State = Status::NumericalFailure;
            return result;
        }

        Status failure = Status::NumericalFailure;
        if (!L2Initialize(
                positions,
                sourceIndex,
                sourceWeights,
                params.SupportRadius,
                projected,
                result.Diagnostics,
                failure))
        {
            result.State = failure;
            return result;
        }

        for (std::uint32_t iteration = 0u;
             iteration < params.MaxIterations;
             ++iteration)
        {
            if (!Iterate(
                    positions,
                    sourceIndex,
                    sourceWeights,
                    params,
                    projected,
                    result.Diagnostics,
                    failure))
            {
                result.State = failure;
                return result;
            }
            result.Diagnostics.Iterations = iteration + 1u;
            if (result.Diagnostics.MaxDisplacement <=
                params.ConvergenceTolerance)
            {
                result.Diagnostics.Converged = true;
                break;
            }
        }

        result.Diagnostics.OutputPointCount = projected.size();
        result.Positions = std::move(projected);
        result.State = result.Diagnostics.Converged
            ? Status::Success
            : Status::NotConverged;
        return result;
    }

    Result Consolidate(const Cloud& cloud, const Params& params)
    {
        if (!cloud.IsValid() || cloud.HasGarbage() || cloud.IsSubmeshView())
        {
            Result result{};
            result.State = Status::InvalidCloud;
            result.Diagnostics.Strategy = Kind(params.Method);
            result.Diagnostics.InputPointCount = cloud.VerticesSize();
            result.Diagnostics.UsedDensityWeighting =
                std::holds_alternative<WlopStrategy>(params.Method);
            return result;
        }
        return Consolidate(cloud.Positions(), params);
    }
}
