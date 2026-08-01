module;

#include <algorithm>
#include <array>
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

import Geometry.GaussianMixture;
import Geometry.KDTree;
import Geometry.PointCloud;
import Geometry.PointCloud.Kernels;
import Geometry.PointCloud.Utils;

namespace Geometry::PointCloud::Consolidation
{
    namespace
    {
        namespace Kernels = Geometry::PointCloud::Kernels;
        namespace GMM = Geometry::GaussianMixture;

        struct ContinuousAttractionModel
        {
            GMM::Model Mixture{};
        };

        struct ClopGaussianTerm
        {
            double Weight{0.0};
            double Sigma{0.0};
        };

        // Figure 5 of Preiner et al. 2014. These normalized Gaussian terms
        // approximate theta(r)/r over r/h in [0, 1].
        inline constexpr std::array<ClopGaussianTerm, 3u>
            kClopAttractionTerms{{
                {11.453, 0.11772},
                {29.886, 0.03287},
                {97.761, 0.01010},
            }};

        [[nodiscard]] bool IsFinite(const glm::vec3 value) noexcept
        {
            return std::isfinite(value.x) &&
                   std::isfinite(value.y) &&
                   std::isfinite(value.z);
        }

        [[nodiscard]] bool IsFinite(const glm::dvec3 value) noexcept
        {
            return std::isfinite(value.x) &&
                   std::isfinite(value.y) &&
                   std::isfinite(value.z);
        }

        [[nodiscard]] bool IsFinite(const glm::dmat3& value) noexcept
        {
            for (std::size_t column = 0u; column < 3u; ++column)
            {
                for (std::size_t row = 0u; row < 3u; ++row)
                {
                    if (!std::isfinite(value[column][row]))
                        return false;
                }
            }
            return true;
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
            result.Diagnostics.UsedContinuousAttraction =
                std::holds_alternative<ClopStrategy>(params.Method);
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
            if (positions.size() > params.MaxInputPointCount ||
                positions.size() > static_cast<std::size_t>(
                    std::numeric_limits<std::uint32_t>::max()))
            {
                result.State = Status::ResourceLimit;
                return result;
            }
            if (!std::ranges::all_of(
                    positions,
                    [](const glm::vec3 value) noexcept
                    {
                        return IsFinite(value);
                    }))
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
            if (const auto* clop =
                    std::get_if<ClopStrategy>(&params.Method))
            {
                result.Diagnostics.MixtureComponentCount =
                    clop->MixtureComponentCount;
                if (clop->MixtureComponentCount == 0u ||
                    clop->MixtureComponentCount > positions.size())
                {
                    result.State = Status::InvalidMixtureComponentCount;
                    return result;
                }
                if (clop->MixtureMaxIterations == 0u ||
                    !std::isfinite(clop->MixtureRelativeTolerance) ||
                    clop->MixtureRelativeTolerance < 0.0 ||
                    !std::isfinite(clop->CovarianceFloor) ||
                    !(clop->CovarianceFloor > 0.0))
                {
                    result.State = Status::InvalidMixtureParameters;
                    return result;
                }
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

        [[nodiscard]] bool FitContinuousAttractionModel(
            const std::span<const glm::vec3> positions,
            const ClopStrategy& strategy,
            const std::uint32_t seed,
            ContinuousAttractionModel& out,
            Diagnostics& diagnostics,
            Status& failure)
        {
            GMM::FitParams fitParams{};
            fitParams.MaxIterations = strategy.MixtureMaxIterations;
            fitParams.RelativeTolerance =
                strategy.MixtureRelativeTolerance;
            fitParams.CovarianceFloor = strategy.CovarianceFloor;
            fitParams.Seed = seed;
            const auto fit = GMM::FitEM(
                positions, strategy.MixtureComponentCount, fitParams);
            diagnostics.MixtureIterations = fit.Diagnostics.Iterations;
            diagnostics.MixtureConverged = fit.Diagnostics.Converged;
            diagnostics.MixtureComponentCount = fit.Mixture.Components.size();
            if (!fit.Succeeded())
            {
                failure = Status::MixtureFitFailed;
                return false;
            }
            if (!fit.Diagnostics.Converged)
            {
                failure = Status::MixtureNotConverged;
                return false;
            }
            out.Mixture = fit.Mixture;
            return true;
        }

        [[nodiscard]] bool AccumulateGaussianProduct(
            const GMM::MultivariateGaussian& gaussian,
            const double mixtureWeight,
            const glm::dvec3 query,
            const double supportRadius,
            const ClopGaussianTerm term,
            glm::dvec3& weightedMeanSum,
            double& weightSum,
            std::size_t& contributionCount) noexcept
        {
            const double scaledSigma = term.Sigma * supportRadius;
            const double variance = scaledSigma * scaledSigma;
            if (!std::isfinite(variance) || !(variance > 0.0) ||
                !std::isfinite(mixtureWeight) ||
                !(mixtureWeight > 0.0) || !IsFinite(query))
            {
                return false;
            }

            const glm::dmat3 covarianceSum =
                gaussian.Covariance + glm::dmat3{variance};
            const double determinant = glm::determinant(covarianceSum);
            if (!IsFinite(covarianceSum) || !std::isfinite(determinant) ||
                !(determinant > 0.0))
            {
                return false;
            }
            const glm::dmat3 inverse = glm::inverse(covarianceSum);
            if (!IsFinite(inverse))
                return false;

            const glm::dvec3 delta = gaussian.Mean - query;
            double mahalanobisSquared = glm::dot(delta, inverse * delta);
            if (!std::isfinite(mahalanobisSquared) ||
                mahalanobisSquared < -1.0e-12)
            {
                return false;
            }
            mahalanobisSquared = std::max(0.0, mahalanobisSquared);
            const double weight = mixtureWeight * term.Weight *
                scaledSigma * scaledSigma * scaledSigma /
                std::sqrt(determinant) *
                std::exp(-0.5 * mahalanobisSquared);
            if (!std::isfinite(weight) || weight < 0.0)
                return false;
            if (!(weight > 0.0))
                return true;

            const glm::dvec3 productMean =
                query + variance * (inverse * delta);
            if (!IsFinite(productMean))
                return false;
            weightedMeanSum += weight * productMean;
            weightSum += weight;
            ++contributionCount;
            return IsFinite(weightedMeanSum) && std::isfinite(weightSum);
        }

        [[nodiscard]] bool ContinuousAttraction(
            const ContinuousAttractionModel& model,
            const glm::vec3 query,
            const double supportRadius,
            const std::span<const ClopGaussianTerm> terms,
            glm::vec3& out,
            Diagnostics& diagnostics,
            Status& failure)
        {
            glm::dvec3 weightedMeanSum{0.0};
            double weightSum = 0.0;
            std::size_t contributionCount = 0u;
            for (std::size_t component = 0u;
                 component < model.Mixture.Components.size();
                 ++component)
            {
                for (const ClopGaussianTerm term : terms)
                {
                    if (!AccumulateGaussianProduct(
                            model.Mixture.Components[component],
                            model.Mixture.Weights[component],
                            glm::dvec3(query), supportRadius, term,
                            weightedMeanSum, weightSum,
                            contributionCount))
                    {
                        failure = Status::NumericalFailure;
                        return false;
                    }
                }
            }
            diagnostics.AttractionContributionCount += contributionCount;
            if (!(weightSum > 0.0) || !std::isfinite(weightSum))
            {
                ++diagnostics.EmptyNeighborhoodCount;
                failure = Status::EmptyContinuousAttraction;
                return false;
            }
            if (!ToFiniteVec3(weightedMeanSum / weightSum, out))
            {
                failure = Status::NumericalFailure;
                return false;
            }
            return true;
        }

        [[nodiscard]] bool ContinuousL2Initialize(
            const ContinuousAttractionModel& model,
            const double supportRadius,
            std::vector<glm::vec3>& projected,
            Diagnostics& diagnostics,
            Status& failure)
        {
            constexpr ClopGaussianTerm thetaTerm{
                1.0, 0.1767766952966369}; // sqrt(1/32)
            std::vector<glm::vec3> initialized(projected.size());
            for (std::size_t i = 0u; i < projected.size(); ++i)
            {
                if (!ContinuousAttraction(
                        model, projected[i], supportRadius,
                        std::span<const ClopGaussianTerm>{&thetaTerm, 1u},
                        initialized[i], diagnostics, failure))
                {
                    return false;
                }
            }
            projected = std::move(initialized);
            return true;
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
            const ContinuousAttractionModel* continuousModel,
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
                glm::dvec3 attraction{0.0};
                double attractionWeight = 0.0;
                if (continuousModel != nullptr)
                {
                    glm::vec3 continuousAttraction{0.0f};
                    if (!ContinuousAttraction(
                            *continuousModel, projected[i],
                            params.SupportRadius,
                            std::span<const ClopGaussianTerm>{
                                kClopAttractionTerms},
                            continuousAttraction, diagnostics, failure))
                    {
                        return false;
                    }
                    attraction = glm::dvec3(continuousAttraction);
                    attractionWeight = 1.0;
                }
                else
                {
                    if (!QueryNeighbors(sourceIndex, projected[i],
                                        params.SupportRadius, neighbors))
                    {
                        failure = Status::SpatialQueryFailed;
                        return false;
                    }

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
                        const double weight =
                            *radial * sourceWeights[neighbor] /
                            std::max(distance, distanceFloor);
                        if (!(weight > 0.0) || !std::isfinite(weight))
                            continue;
                        attraction +=
                            glm::dvec3(source[neighbor]) * weight;
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
        case StrategyKind::Clop: return "clop";
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
        case Status::InvalidMixtureComponentCount:
            return "invalid_mixture_component_count";
        case Status::InvalidMixtureParameters:
            return "invalid_mixture_parameters";
        case Status::ResourceLimit: return "resource_limit";
        case Status::SpatialIndexBuildFailed:
            return "spatial_index_build_failed";
        case Status::SpatialQueryFailed: return "spatial_query_failed";
        case Status::EmptyNeighborhood: return "empty_neighborhood";
        case Status::DensityEstimationFailed:
            return "density_estimation_failed";
        case Status::MixtureFitFailed: return "mixture_fit_failed";
        case Status::MixtureNotConverged:
            return "mixture_not_converged";
        case Status::EmptyContinuousAttraction:
            return "empty_continuous_attraction";
        case Status::NumericalFailure: return "numerical_failure";
        case Status::NotConverged: return "not_converged";
        }
        return "invalid";
    }

    StrategyKind Kind(const Strategy& strategy) noexcept
    {
        if (std::holds_alternative<LopStrategy>(strategy))
            return StrategyKind::Lop;
        if (std::holds_alternative<WlopStrategy>(strategy))
            return StrategyKind::Wlop;
        return StrategyKind::Clop;
    }

    Result Consolidate(
        const std::span<const glm::vec3> positions,
        const Params& params)
    {
        Result result = InvalidRequest(positions, params);
        if (!result.Succeeded())
            return result;

        Status failure = Status::NumericalFailure;
        ContinuousAttractionModel continuousModel{};
        const ContinuousAttractionModel* continuousModelPtr = nullptr;
        if (const auto* clop = std::get_if<ClopStrategy>(&params.Method))
        {
            if (!FitContinuousAttractionModel(
                    positions, *clop, params.Seed, continuousModel,
                    result.Diagnostics, failure))
            {
                result.State = failure;
                return result;
            }
            continuousModelPtr = &continuousModel;
        }

        Geometry::KDTree sourceIndex{};
        if (continuousModelPtr == nullptr &&
            !BuildIndex(positions, sourceIndex))
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

        const bool initialized = continuousModelPtr != nullptr
            ? ContinuousL2Initialize(
                *continuousModelPtr, params.SupportRadius, projected,
                result.Diagnostics, failure)
            : L2Initialize(
                positions, sourceIndex, sourceWeights,
                params.SupportRadius, projected,
                result.Diagnostics, failure);
        if (!initialized)
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
                    continuousModelPtr,
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
            result.Diagnostics.UsedContinuousAttraction =
                std::holds_alternative<ClopStrategy>(params.Method);
            if (const auto* clop =
                    std::get_if<ClopStrategy>(&params.Method))
            {
                result.Diagnostics.MixtureComponentCount =
                    clop->MixtureComponentCount;
            }
            return result;
        }
        return Consolidate(cloud.Positions(), params);
    }
}
