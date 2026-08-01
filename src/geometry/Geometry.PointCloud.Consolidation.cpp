module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
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
import Geometry.PointCloud.Normals;
import Geometry.PointCloud.Utils;

namespace Geometry::PointCloud::Consolidation
{
    namespace
    {
        namespace Kernels = Geometry::PointCloud::Kernels;
        namespace PointNormals = Geometry::PointCloud::Normals;
        namespace GMM = Geometry::GaussianMixture;

        struct ClopGaussianTerm
        {
            double Weight{0.0};
            double Sigma{0.0};
        };

        struct PreparedGaussianProduct
        {
            glm::dmat3 InverseCovarianceSum{1.0};
            double Variance{0.0};
            double Coefficient{0.0};
            double ExactZeroRadiusSquared{0.0};
        };

        struct PreparedContinuousComponent
        {
            PreparedGaussianProduct Initialization{};
            std::array<PreparedGaussianProduct, 3u> Attraction{};
            double ExactZeroRadiusSquared{0.0};
        };

        struct ContinuousAttractionModel
        {
            GMM::Model Mixture{};
            std::vector<PreparedContinuousComponent> Prepared{};
        };

        struct NeighborhoodCache
        {
            std::vector<std::size_t> Offsets{};
            std::vector<Geometry::KDTree::ElementIndex> Indices{};

            [[nodiscard]] std::span<const Geometry::KDTree::ElementIndex>
            Neighbors(const std::size_t point) const noexcept
            {
                return std::span<const Geometry::KDTree::ElementIndex>{Indices}
                    .subspan(
                        Offsets[point],
                        Offsets[point + 1u] - Offsets[point]);
            }
        };

        struct OptimizedExecutionScratch
        {
            Geometry::KDTree::RadiusQueryScratch RadiusQuery{};
            std::vector<Geometry::KDTree::ElementIndex> Neighbors{};
            std::vector<Geometry::KDTree::ElementIndex> LocalNeighbors{};
            NeighborhoodCache ProjectedNeighborhoods{};
            std::vector<glm::dvec3> EarPoints{};
            std::vector<glm::dvec3> EarNormals{};
        };

        // Figure 5 of Preiner et al. 2014. These normalized Gaussian terms
        // approximate theta(r)/r over r/h in [0, 1].
        inline constexpr std::array<ClopGaussianTerm, 3u>
            kClopAttractionTerms{{
                {11.453, 0.11772},
                {29.886, 0.03287},
                {97.761, 0.01010},
            }};
        inline constexpr ClopGaussianTerm kClopInitializationTerm{
            1.0, 0.1767766952966369}; // sqrt(1/32)

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

        [[nodiscard]] bool IsAnisotropic(
            const Strategy& strategy) noexcept
        {
            if (const auto* wlop = std::get_if<WlopStrategy>(&strategy))
                return wlop->Weighting == WeightingMode::Anisotropic;
            return std::holds_alternative<EarStrategy>(strategy);
        }

        [[nodiscard]] NormalSourcePolicy NormalPolicy(
            const Strategy& strategy) noexcept
        {
            if (const auto* wlop = std::get_if<WlopStrategy>(&strategy))
                return wlop->NormalSource;
            if (const auto* ear = std::get_if<EarStrategy>(&strategy))
                return ear->NormalSource;
            return NormalSourcePolicy::AuthoredOrEstimate;
        }

        [[nodiscard]] double NormalAngle(
            const Strategy& strategy) noexcept
        {
            if (const auto* wlop = std::get_if<WlopStrategy>(&strategy))
                return wlop->NormalAngleRadians;
            if (const auto* ear = std::get_if<EarStrategy>(&strategy))
                return ear->NormalAngleRadians;
            return 0.0;
        }

        [[nodiscard]] std::uint32_t NormalRefinementRounds(
            const Strategy& strategy) noexcept
        {
            if (const auto* wlop = std::get_if<WlopStrategy>(&strategy))
                return wlop->NormalRefinementRounds;
            if (const auto* ear = std::get_if<EarStrategy>(&strategy))
                return ear->NormalRefinementRounds;
            return 0u;
        }

        [[nodiscard]] Diagnostics InitialDiagnostics(
            const std::span<const glm::vec3> positions,
            const Params& params) noexcept
        {
            Diagnostics diagnostics{};
            diagnostics.Strategy = Kind(params.Method);
            diagnostics.InputPointCount = positions.size();
            diagnostics.UsedDensityWeighting =
                std::holds_alternative<WlopStrategy>(params.Method) ||
                std::holds_alternative<EarStrategy>(params.Method);
            diagnostics.UsedContinuousAttraction =
                std::holds_alternative<ClopStrategy>(params.Method);
            diagnostics.UsedAnisotropicWeighting =
                IsAnisotropic(params.Method);
            if (const auto* clop = std::get_if<ClopStrategy>(&params.Method))
                diagnostics.MixtureComponentCount = clop->MixtureComponentCount;
            return diagnostics;
        }

        [[nodiscard]] Result InvalidRequest(
            const std::span<const glm::vec3> positions,
            const std::span<const glm::vec3> normals,
            const Params& params)
        {
            Result result{};
            result.Diagnostics = InitialDiagnostics(positions, params);
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
            if (!normals.empty() && normals.size() != positions.size())
            {
                result.State = Status::InvalidNormals;
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
            const bool mayUpsample =
                std::holds_alternative<EarStrategy>(params.Method);
            if (target < 2u || (!mayUpsample && target > positions.size()) ||
                target > params.MaxOutputPointCount ||
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
            if (IsAnisotropic(params.Method))
            {
                const double normalAngle = NormalAngle(params.Method);
                if (!std::isfinite(normalAngle) || !(normalAngle > 0.0) ||
                    !(normalAngle < 3.14159265358979323846))
                {
                    result.State = Status::InvalidNormalAngle;
                    return result;
                }
                const std::uint32_t rounds =
                    NormalRefinementRounds(params.Method);
                if (rounds == 0u || rounds > params.MaxIterations)
                {
                    result.State = Status::InvalidNormalRefinementRounds;
                    return result;
                }
                const NormalSourcePolicy policy = NormalPolicy(params.Method);
                if (policy != NormalSourcePolicy::AuthoredOrEstimate &&
                    policy != NormalSourcePolicy::RequireAuthored)
                {
                    result.State = Status::InvalidNormals;
                    return result;
                }
                if (const auto* wlop =
                        std::get_if<WlopStrategy>(&params.Method))
                {
                    if (wlop->Weighting != WeightingMode::Anisotropic)
                    {
                        result.State = Status::InvalidNormals;
                        return result;
                    }
                }
                if (const auto* ear = std::get_if<EarStrategy>(&params.Method))
                {
                    if (!std::isfinite(ear->EdgeSensitivity) ||
                        ear->EdgeSensitivity < 0.0)
                    {
                        result.State = Status::InvalidEdgeSensitivity;
                        return result;
                    }
                }
            }
            else if (const auto* wlop =
                         std::get_if<WlopStrategy>(&params.Method))
            {
                if (wlop->Weighting != WeightingMode::Isotropic)
                {
                    result.State = Status::InvalidNormals;
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
            std::vector<Geometry::KDTree::ElementIndex>& neighbors,
            Geometry::KDTree::RadiusQueryScratch* const scratch = nullptr)
            noexcept
        {
            if (scratch != nullptr)
            {
                return index.QueryRadius(
                    point, BroadPhaseRadius(supportRadius), neighbors,
                    *scratch).has_value();
            }
            return index.QueryRadius(
                point, BroadPhaseRadius(supportRadius), neighbors).has_value();
        }

        [[nodiscard]] bool BuildNeighborhoodCache(
            const Geometry::KDTree& index,
            const std::span<const glm::vec3> points,
            const double supportRadius,
            OptimizedExecutionScratch& scratch)
        {
            NeighborhoodCache& cache = scratch.ProjectedNeighborhoods;
            cache.Offsets.assign(points.size() + 1u, 0u);
            cache.Indices.clear();
            for (std::size_t i = 0u; i < points.size(); ++i)
            {
                if (!QueryNeighbors(
                        index, points[i], supportRadius, scratch.Neighbors,
                        &scratch.RadiusQuery))
                {
                    cache.Offsets.clear();
                    cache.Indices.clear();
                    return false;
                }
                cache.Indices.insert(
                    cache.Indices.end(),
                    scratch.Neighbors.begin(), scratch.Neighbors.end());
                cache.Offsets[i + 1u] = cache.Indices.size();
            }
            return true;
        }

        [[nodiscard]] bool ComputeCachedDensityWeights(
            const std::span<const glm::vec3> points,
            const NeighborhoodCache& cache,
            const double supportRadius,
            std::vector<float>& weights,
            Diagnostics& diagnostics,
            Status& failure)
        {
            weights.assign(points.size(), 0.0f);
            for (std::size_t i = 0u; i < points.size(); ++i)
            {
                double density = 1.0;
                std::size_t contributionCount = 0u;
                for (const auto neighbor : cache.Neighbors(i))
                {
                    if (neighbor == i || neighbor >= points.size())
                        continue;
                    const auto weight = Kernels::Weight(
                        DistanceSquared(points[i], points[neighbor]),
                        supportRadius,
                        Kernels::KernelType::ThetaLop);
                    if (!weight.has_value())
                    {
                        failure = Status::DensityEstimationFailed;
                        return false;
                    }
                    if (!(*weight > 0.0))
                        continue;
                    density += *weight;
                    ++contributionCount;
                }
                if (contributionCount == 0u)
                {
                    ++diagnostics.EmptyNeighborhoodCount;
                    failure = Status::EmptyNeighborhood;
                    weights.clear();
                    return false;
                }
                if (!std::isfinite(density) ||
                    density > static_cast<double>(
                        std::numeric_limits<float>::max()))
                {
                    failure = Status::DensityEstimationFailed;
                    weights.clear();
                    return false;
                }
                weights[i] = static_cast<float>(density);
                diagnostics.DensityContributionCount += contributionCount;
            }
            return true;
        }

        [[nodiscard]] bool InitializeProjected(
            const std::span<const glm::vec3> positions,
            const std::size_t target,
            const std::uint32_t seed,
            std::vector<glm::vec3>& projected,
            std::vector<std::size_t>& selectedIndices)
        {
            Cloud cloud{};
            cloud.Reserve(positions.size());
            for (const glm::vec3 point : positions)
                static_cast<void>(cloud.AddPoint(point));

            const auto sample = RandomSubsample(
                cloud,
                SubsampleParams{
                    .TargetCount = target,
                    .Seed = seed,
                });
            if (!sample.has_value())
                return false;
            projected.assign(
                sample->Subsampled.Positions().begin(),
                sample->Subsampled.Positions().end());
            selectedIndices = sample->SelectedIndices;
            return projected.size() == target &&
                selectedIndices.size() == target;
        }

        [[nodiscard]] bool NormalizeNormal(
            const glm::vec3 input,
            glm::vec3& output) noexcept
        {
            if (!IsFinite(input))
                return false;
            const glm::dvec3 normal{input};
            const double lengthSquared = glm::dot(normal, normal);
            if (!std::isfinite(lengthSquared) ||
                !(lengthSquared > std::numeric_limits<double>::epsilon()))
            {
                return false;
            }
            return ToFiniteVec3(normal / std::sqrt(lengthSquared), output);
        }

        [[nodiscard]] bool HasLocallyConsistentOrientation(
            const std::span<const glm::vec3> positions,
            const std::span<const glm::vec3> normals,
            const double supportRadius) noexcept
        {
            if (positions.size() != normals.size())
                return false;
            const double supportSquared = supportRadius * supportRadius;
            for (std::size_t i = 0u; i < positions.size(); ++i)
            {
                bool hasNeighbor = false;
                bool hasCompatibleNeighbor = false;
                for (std::size_t j = 0u; j < positions.size(); ++j)
                {
                    if (i == j ||
                        !(DistanceSquared(positions[i], positions[j]) <
                          supportSquared))
                    {
                        continue;
                    }
                    hasNeighbor = true;
                    if (glm::dot(
                            glm::dvec3(normals[i]),
                            glm::dvec3(normals[j])) >= 0.0)
                    {
                        hasCompatibleNeighbor = true;
                        break;
                    }
                }
                // An oriented local surface gives every supported sample at
                // least one normal in the same hemisphere. Isolated points
                // are left to the existing empty-neighborhood contract.
                if (hasNeighbor && !hasCompatibleNeighbor)
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool PrepareSourceNormals(
            const std::span<const glm::vec3> positions,
            const std::span<const glm::vec3> authoredNormals,
            const Params& params,
            std::vector<glm::vec3>& normals,
            Diagnostics& diagnostics,
            Status& failure)
        {
            if (!authoredNormals.empty())
            {
                if (authoredNormals.size() != positions.size())
                {
                    failure = Status::InvalidNormals;
                    return false;
                }
                normals.resize(authoredNormals.size());
                for (std::size_t i = 0u; i < authoredNormals.size(); ++i)
                {
                    if (!NormalizeNormal(authoredNormals[i], normals[i]))
                    {
                        normals.clear();
                        failure = Status::InvalidNormals;
                        return false;
                    }
                }
                if (!HasLocallyConsistentOrientation(
                        positions, normals, params.SupportRadius))
                {
                    normals.clear();
                    failure = Status::InvalidNormals;
                    return false;
                }
                diagnostics.UsedAuthoredNormals = true;
                return true;
            }

            if (NormalPolicy(params.Method) ==
                NormalSourcePolicy::RequireAuthored)
            {
                failure = Status::NormalsRequired;
                return false;
            }

            PointNormals::Params normalParams{};
            normalParams.KNeighbors = std::min<std::size_t>(
                normalParams.KNeighbors, positions.size() - 1u);
            normalParams.MinimumNeighbors = 2u;
            normalParams.Orientation =
                PointNormals::OrientationMode::MinimumSpanningTree;
            const auto estimate = PointNormals::Estimate(
                positions, normalParams);
            if (!estimate.has_value() ||
                estimate->Status != PointNormals::RecomputeStatus::Success ||
                estimate->Normals.size() != positions.size() ||
                estimate->Diagnostics.ValidNormalPointCount == 0u)
            {
                failure = Status::NormalEstimationFailed;
                return false;
            }
            normals.resize(estimate->Normals.size());
            for (std::size_t i = 0u; i < estimate->Normals.size(); ++i)
            {
                if (!NormalizeNormal(estimate->Normals[i], normals[i]))
                {
                    normals.clear();
                    failure = Status::NormalEstimationFailed;
                    return false;
                }
            }
            if (!HasLocallyConsistentOrientation(
                    positions, normals, params.SupportRadius))
            {
                normals.clear();
                failure = Status::NormalEstimationFailed;
                return false;
            }
            diagnostics.EstimatedNormals = true;
            return true;
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

        [[nodiscard]] bool PrepareGaussianProduct(
            const GMM::MultivariateGaussian& gaussian,
            const double mixtureWeight,
            const double supportRadius,
            const ClopGaussianTerm term,
            PreparedGaussianProduct& out) noexcept
        {
            const double scaledSigma = term.Sigma * supportRadius;
            const double variance = scaledSigma * scaledSigma;
            if (!std::isfinite(variance) || !(variance > 0.0) ||
                !std::isfinite(mixtureWeight) || !(mixtureWeight > 0.0))
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

            const double coefficient = mixtureWeight * term.Weight *
                scaledSigma * scaledSigma * scaledSigma /
                std::sqrt(determinant);
            if (!std::isfinite(coefficient) || !(coefficient > 0.0))
                return false;

            double eigenvalueUpperBound = 0.0;
            for (std::size_t row = 0u; row < 3u; ++row)
            {
                double rowSum = 0.0;
                for (std::size_t column = 0u; column < 3u; ++column)
                    rowSum += std::abs(covarianceSum[column][row]);
                eigenvalueUpperBound = std::max(
                    eigenvalueUpperBound, rowSum);
            }
            if (!std::isfinite(eigenvalueUpperBound) ||
                !(eigenvalueUpperBound > 0.0))
            {
                return false;
            }

            constexpr double underflowSafety = 2.0;
            const double logDynamicRange = std::log(coefficient) -
                std::log(std::numeric_limits<double>::denorm_min()) +
                underflowSafety;
            const double radiusSquared = logDynamicRange > 0.0
                ? 2.0 * eigenvalueUpperBound * logDynamicRange
                : 0.0;
            if (!std::isfinite(radiusSquared) || radiusSquared < 0.0)
                return false;

            out.InverseCovarianceSum = inverse;
            out.Variance = variance;
            out.Coefficient = coefficient;
            out.ExactZeroRadiusSquared = radiusSquared;
            return true;
        }

        [[nodiscard]] bool PrepareContinuousAttractionModel(
            ContinuousAttractionModel& model,
            const double supportRadius)
        {
            model.Prepared.clear();
            model.Prepared.resize(model.Mixture.Components.size());
            for (std::size_t component = 0u;
                 component < model.Mixture.Components.size(); ++component)
            {
                PreparedContinuousComponent& prepared =
                    model.Prepared[component];
                const auto& gaussian = model.Mixture.Components[component];
                const double mixtureWeight = model.Mixture.Weights[component];
                if (!PrepareGaussianProduct(
                        gaussian, mixtureWeight, supportRadius,
                        kClopInitializationTerm, prepared.Initialization))
                {
                    model.Prepared.clear();
                    return false;
                }
                prepared.ExactZeroRadiusSquared =
                    prepared.Initialization.ExactZeroRadiusSquared;
                for (std::size_t term = 0u;
                     term < kClopAttractionTerms.size(); ++term)
                {
                    if (!PrepareGaussianProduct(
                            gaussian, mixtureWeight, supportRadius,
                            kClopAttractionTerms[term],
                            prepared.Attraction[term]))
                    {
                        model.Prepared.clear();
                        return false;
                    }
                    prepared.ExactZeroRadiusSquared = std::max(
                        prepared.ExactZeroRadiusSquared,
                        prepared.Attraction[term].ExactZeroRadiusSquared);
                }
            }
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

        [[nodiscard]] bool AccumulatePreparedGaussianProduct(
            const GMM::MultivariateGaussian& gaussian,
            const PreparedGaussianProduct& prepared,
            const glm::dvec3 query,
            glm::dvec3& weightedMeanSum,
            double& weightSum,
            std::size_t& contributionCount) noexcept
        {
            const glm::dvec3 delta = gaussian.Mean - query;
            const double euclideanSquared = glm::dot(delta, delta);
            if (!std::isfinite(euclideanSquared) || euclideanSquared < 0.0)
                return false;
            if (euclideanSquared > prepared.ExactZeroRadiusSquared)
                return true;

            double mahalanobisSquared = glm::dot(
                delta, prepared.InverseCovarianceSum * delta);
            if (!std::isfinite(mahalanobisSquared) ||
                mahalanobisSquared < -1.0e-12)
            {
                return false;
            }
            mahalanobisSquared = std::max(0.0, mahalanobisSquared);
            const double weight = prepared.Coefficient *
                std::exp(-0.5 * mahalanobisSquared);
            if (!std::isfinite(weight) || weight < 0.0)
                return false;
            if (!(weight > 0.0))
                return true;

            const glm::dvec3 productMean = query + prepared.Variance *
                (prepared.InverseCovarianceSum * delta);
            if (!IsFinite(productMean))
                return false;
            weightedMeanSum += weight * productMean;
            weightSum += weight;
            ++contributionCount;
            return IsFinite(weightedMeanSum) && std::isfinite(weightSum);
        }

        [[nodiscard]] bool ContinuousAttractionOptimized(
            const ContinuousAttractionModel& model,
            const glm::vec3 query,
            const bool initialization,
            glm::vec3& out,
            Diagnostics& diagnostics,
            Status& failure) noexcept
        {
            if (model.Prepared.size() != model.Mixture.Components.size())
            {
                failure = Status::NumericalFailure;
                return false;
            }
            glm::dvec3 weightedMeanSum{0.0};
            double weightSum = 0.0;
            std::size_t contributionCount = 0u;
            for (std::size_t component = 0u;
                 component < model.Mixture.Components.size(); ++component)
            {
                const glm::dvec3 delta =
                    model.Mixture.Components[component].Mean -
                    glm::dvec3(query);
                const double euclideanSquared = glm::dot(delta, delta);
                const PreparedContinuousComponent& prepared =
                    model.Prepared[component];
                if (!std::isfinite(euclideanSquared) ||
                    euclideanSquared < 0.0)
                {
                    failure = Status::NumericalFailure;
                    return false;
                }
                if (euclideanSquared > prepared.ExactZeroRadiusSquared)
                    continue;

                if (initialization)
                {
                    if (!AccumulatePreparedGaussianProduct(
                            model.Mixture.Components[component],
                            prepared.Initialization, glm::dvec3(query),
                            weightedMeanSum, weightSum, contributionCount))
                    {
                        failure = Status::NumericalFailure;
                        return false;
                    }
                    continue;
                }
                for (const PreparedGaussianProduct& term :
                     prepared.Attraction)
                {
                    if (!AccumulatePreparedGaussianProduct(
                            model.Mixture.Components[component], term,
                            glm::dvec3(query), weightedMeanSum, weightSum,
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
            Status& failure,
            const bool optimized)
        {
            std::vector<glm::vec3> initialized(projected.size());
            for (std::size_t i = 0u; i < projected.size(); ++i)
            {
                const bool succeeded = optimized
                    ? ContinuousAttractionOptimized(
                        model, projected[i], true, initialized[i],
                        diagnostics, failure)
                    : ContinuousAttraction(
                        model, projected[i], supportRadius,
                        std::span<const ClopGaussianTerm>{
                            &kClopInitializationTerm, 1u},
                        initialized[i], diagnostics, failure);
                if (!succeeded)
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
            Status& failure,
            Geometry::KDTree::RadiusQueryScratch* const scratch = nullptr)
        {
            std::vector<glm::vec3> initialized(projected.size());
            std::vector<Geometry::KDTree::ElementIndex> neighbors{};
            for (std::size_t i = 0u; i < projected.size(); ++i)
            {
                if (!QueryNeighbors(
                        sourceIndex, projected[i], supportRadius, neighbors,
                        scratch))
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

        [[nodiscard]] std::optional<double> EarSpatialWeight(
            const double distanceSquared,
            const double supportRadius) noexcept
        {
            if (!std::isfinite(distanceSquared) || distanceSquared < 0.0 ||
                !std::isfinite(supportRadius) || !(supportRadius > 0.0))
            {
                return std::nullopt;
            }
            const double normalizedSquared =
                distanceSquared / (supportRadius * supportRadius);
            if (!std::isfinite(normalizedSquared))
                return std::nullopt;
            if (!(normalizedSquared < 1.0))
                return 0.0;
            const double weight = std::exp(-normalizedSquared);
            if (!std::isfinite(weight) || weight < 0.0)
                return std::nullopt;
            return weight;
        }

        [[nodiscard]] std::optional<double> NormalSimilarityWeight(
            const glm::vec3 lhs,
            const glm::vec3 rhs,
            const double normalAngle) noexcept
        {
            if (!IsFinite(lhs) || !IsFinite(rhs) ||
                !std::isfinite(normalAngle) || !(normalAngle > 0.0) ||
                !(normalAngle < 3.14159265358979323846))
            {
                return std::nullopt;
            }
            const double denominator = 1.0 - std::cos(normalAngle);
            if (!std::isfinite(denominator) || !(denominator > 0.0))
                return std::nullopt;
            const double dot = std::clamp(
                glm::dot(glm::dvec3(lhs), glm::dvec3(rhs)), -1.0, 1.0);
            const double scaledDifference = (1.0 - dot) / denominator;
            const double weight = std::exp(
                -scaledDifference * scaledDifference);
            if (!std::isfinite(weight) || weight < 0.0)
                return std::nullopt;
            return weight;
        }

        [[nodiscard]] bool RefineNormals(
            const std::span<const glm::vec3> points,
            const double supportRadius,
            const double normalAngle,
            std::vector<glm::vec3>& normals,
            Diagnostics& diagnostics,
            Status& failure,
            Geometry::KDTree::RadiusQueryScratch* const scratch = nullptr)
        {
            if (points.size() != normals.size())
            {
                failure = Status::InvalidNormals;
                return false;
            }
            Geometry::KDTree index{};
            if (!BuildIndex(points, index))
            {
                failure = Status::SpatialIndexBuildFailed;
                return false;
            }

            std::vector<glm::vec3> refined(normals.size());
            std::vector<Geometry::KDTree::ElementIndex> neighbors{};
            for (std::size_t i = 0u; i < points.size(); ++i)
            {
                if (!QueryNeighbors(
                        index, points[i], supportRadius, neighbors, scratch))
                {
                    failure = Status::SpatialQueryFailed;
                    return false;
                }
                glm::dvec3 sum{0.0};
                double weightSum = 0.0;
                for (const auto neighbor : neighbors)
                {
                    if (neighbor >= points.size())
                        continue;
                    const auto spatial = EarSpatialWeight(
                        DistanceSquared(points[i], points[neighbor]),
                        supportRadius);
                    const auto directional = NormalSimilarityWeight(
                        normals[i], normals[neighbor], normalAngle);
                    if (!spatial.has_value() || !directional.has_value())
                    {
                        failure = Status::NumericalFailure;
                        return false;
                    }
                    const double weight = *spatial * *directional;
                    if (!(weight > 0.0))
                        continue;
                    sum += glm::dvec3(normals[neighbor]) * weight;
                    weightSum += weight;
                }
                if (!(weightSum > 0.0) || !std::isfinite(weightSum))
                {
                    ++diagnostics.EmptyNeighborhoodCount;
                    failure = Status::EmptyNeighborhood;
                    return false;
                }
                glm::vec3 averaged{};
                if (!ToFiniteVec3(sum / weightSum, averaged) ||
                    !NormalizeNormal(averaged, refined[i]))
                {
                    failure = Status::InvalidNormals;
                    return false;
                }
            }
            normals = std::move(refined);
            ++diagnostics.NormalRefinementIterations;
            return true;
        }

        [[nodiscard]] bool Iterate(
            const std::span<const glm::vec3> source,
            const Geometry::KDTree& sourceIndex,
            const std::span<const float> sourceWeights,
            const ContinuousAttractionModel* continuousModel,
            const std::span<const glm::vec3> projectedNormals,
            const Params& params,
            std::vector<glm::vec3>& projected,
            Diagnostics& diagnostics,
            Status& failure,
            OptimizedExecutionScratch* const optimizedScratch = nullptr)
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
                if (optimizedScratch != nullptr)
                {
                    if (!BuildNeighborhoodCache(
                            projectedIndex, projected, params.SupportRadius,
                            *optimizedScratch))
                    {
                        failure = Status::SpatialQueryFailed;
                        return false;
                    }
                    if (!ComputeCachedDensityWeights(
                            projected,
                            optimizedScratch->ProjectedNeighborhoods,
                            params.SupportRadius, projectedWeights,
                            diagnostics, failure))
                    {
                        return false;
                    }
                }
                else
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
                    const bool succeeded = optimizedScratch != nullptr
                        ? ContinuousAttractionOptimized(
                            *continuousModel, projected[i], false,
                            continuousAttraction, diagnostics, failure)
                        : ContinuousAttraction(
                            *continuousModel, projected[i],
                            params.SupportRadius,
                            std::span<const ClopGaussianTerm>{
                                kClopAttractionTerms},
                            continuousAttraction, diagnostics, failure);
                    if (!succeeded)
                    {
                        return false;
                    }
                    attraction = glm::dvec3(continuousAttraction);
                    attractionWeight = 1.0;
                }
                else
                {
                    if (!QueryNeighbors(sourceIndex, projected[i],
                                        params.SupportRadius, neighbors,
                                        optimizedScratch != nullptr
                                            ? &optimizedScratch->RadiusQuery
                                            : nullptr))
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
                        const auto radial =
                            diagnostics.UsedAnisotropicWeighting
                            ? Kernels::DirectionalWeight(
                                source[neighbor] - projected[i],
                                projectedNormals[i],
                                params.SupportRadius)
                            : Kernels::Weight(
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

                std::span<const Geometry::KDTree::ElementIndex>
                    repulsionNeighbors{};
                if (optimizedScratch != nullptr &&
                    diagnostics.UsedDensityWeighting)
                {
                    repulsionNeighbors =
                        optimizedScratch->ProjectedNeighborhoods.Neighbors(i);
                }
                else if (!QueryNeighbors(
                             projectedIndex, projected[i],
                             params.SupportRadius, neighbors,
                             optimizedScratch != nullptr
                                 ? &optimizedScratch->RadiusQuery
                                 : nullptr))
                {
                    failure = Status::SpatialQueryFailed;
                    return false;
                }
                else
                {
                    repulsionNeighbors = neighbors;
                }
                glm::dvec3 repulsion{0.0};
                double repulsionWeight = 0.0;
                for (const auto neighbor : repulsionNeighbors)
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

        [[nodiscard]] bool ProjectionDistance(
            const glm::vec3 base,
            const glm::vec3 normal,
            const std::span<const glm::vec3> points,
            const std::span<const glm::vec3> normals,
            const double supportRadius,
            const double normalAngle,
            double& distance,
            Status& failure,
            const Geometry::KDTree* const index = nullptr,
            OptimizedExecutionScratch* const optimizedScratch = nullptr)
        {
            glm::dvec3 unitNormal{};
            glm::vec3 normalized{};
            if (!NormalizeNormal(normal, normalized))
            {
                failure = Status::InvalidNormals;
                return false;
            }
            unitNormal = glm::dvec3(normalized);

            double numerator = 0.0;
            double denominator = 0.0;
            const auto accumulate = [&](const std::size_t i)
            {
                const auto spatial = EarSpatialWeight(
                    DistanceSquared(base, points[i]), supportRadius);
                const auto directional = NormalSimilarityWeight(
                    normalized, normals[i], normalAngle);
                if (!spatial.has_value() || !directional.has_value())
                {
                    failure = Status::NumericalFailure;
                    return false;
                }
                const double weight = *spatial * *directional;
                if (!(weight > 0.0))
                    return true;
                numerator += glm::dot(
                    unitNormal,
                    glm::dvec3(base) - glm::dvec3(points[i])) * weight;
                denominator += weight;
                return true;
            };
            if (index != nullptr && optimizedScratch != nullptr)
            {
                if (!QueryNeighbors(
                        *index, base, supportRadius,
                        optimizedScratch->LocalNeighbors,
                        &optimizedScratch->RadiusQuery))
                {
                    failure = Status::SpatialQueryFailed;
                    return false;
                }
                for (const auto neighbor : optimizedScratch->LocalNeighbors)
                {
                    if (neighbor >= points.size() || !accumulate(neighbor))
                        return false;
                }
            }
            else
            {
                for (std::size_t i = 0u; i < points.size(); ++i)
                {
                    if (!accumulate(i))
                        return false;
                }
            }
            if (!(denominator > 0.0) || !std::isfinite(denominator))
            {
                failure = Status::EmptyNeighborhood;
                return false;
            }
            distance = numerator / denominator;
            if (!std::isfinite(distance))
            {
                failure = Status::NumericalFailure;
                return false;
            }
            return true;
        }

        [[nodiscard]] bool RefineNormalAtBase(
            const glm::vec3 base,
            const glm::vec3 fixedNormal,
            const std::span<const glm::vec3> points,
            const std::span<const glm::vec3> normals,
            const double supportRadius,
            const double normalAngle,
            glm::vec3& refined,
            Status& failure,
            const Geometry::KDTree* const index = nullptr,
            OptimizedExecutionScratch* const optimizedScratch = nullptr)
        {
            glm::vec3 normalized{};
            if (!NormalizeNormal(fixedNormal, normalized))
            {
                failure = Status::InvalidNormals;
                return false;
            }
            glm::dvec3 sum{0.0};
            double denominator = 0.0;
            const auto accumulate = [&](const std::size_t i)
            {
                const auto spatial = EarSpatialWeight(
                    DistanceSquared(base, points[i]), supportRadius);
                const auto directional = NormalSimilarityWeight(
                    normalized, normals[i], normalAngle);
                if (!spatial.has_value() || !directional.has_value())
                {
                    failure = Status::NumericalFailure;
                    return false;
                }
                const double weight = *spatial * *directional;
                if (!(weight > 0.0))
                    return true;
                sum += glm::dvec3(normals[i]) * weight;
                denominator += weight;
                return true;
            };
            if (index != nullptr && optimizedScratch != nullptr)
            {
                if (!QueryNeighbors(
                        *index, base, supportRadius,
                        optimizedScratch->LocalNeighbors,
                        &optimizedScratch->RadiusQuery))
                {
                    failure = Status::SpatialQueryFailed;
                    return false;
                }
                for (const auto neighbor : optimizedScratch->LocalNeighbors)
                {
                    if (neighbor >= points.size() || !accumulate(neighbor))
                        return false;
                }
            }
            else
            {
                for (std::size_t i = 0u; i < points.size(); ++i)
                {
                    if (!accumulate(i))
                        return false;
                }
            }
            glm::vec3 averaged{};
            if (!(denominator > 0.0) || !std::isfinite(denominator) ||
                !ToFiniteVec3(sum / denominator, averaged) ||
                !NormalizeNormal(averaged, refined))
            {
                failure = Status::InvalidNormals;
                return false;
            }
            return true;
        }

        [[nodiscard]] bool Clearance(
            const glm::vec3 base,
            const std::span<const glm::vec3> points,
            const std::span<const glm::vec3> normals,
            const double supportRadius,
            double& clearance,
            Status& failure,
            const Geometry::KDTree* const index = nullptr,
            OptimizedExecutionScratch* const optimizedScratch = nullptr)
        {
            clearance = std::numeric_limits<double>::infinity();
            bool found = false;
            const auto accumulate = [&](const std::size_t i)
            {
                const double distanceSquared =
                    DistanceSquared(base, points[i]);
                if (!(distanceSquared < supportRadius * supportRadius))
                    return true;
                const glm::dvec3 offset =
                    glm::dvec3(base) - glm::dvec3(points[i]);
                const glm::dvec3 normal{normals[i]};
                const glm::dvec3 tangential =
                    offset - glm::dot(normal, offset) * normal;
                const double value = std::sqrt(glm::dot(
                    tangential, tangential));
                if (!std::isfinite(value))
                {
                    failure = Status::NumericalFailure;
                    return false;
                }
                clearance = std::min(clearance, value);
                found = true;
                return true;
            };
            if (index != nullptr && optimizedScratch != nullptr)
            {
                if (!QueryNeighbors(
                        *index, base, supportRadius,
                        optimizedScratch->LocalNeighbors,
                        &optimizedScratch->RadiusQuery))
                {
                    failure = Status::SpatialQueryFailed;
                    return false;
                }
                for (const auto neighbor : optimizedScratch->LocalNeighbors)
                {
                    if (neighbor >= points.size() || !accumulate(neighbor))
                        return false;
                }
            }
            else
            {
                for (std::size_t i = 0u; i < points.size(); ++i)
                {
                    if (!accumulate(i))
                        return false;
                }
            }
            if (!found)
            {
                failure = Status::EmptyNeighborhood;
                return false;
            }
            return true;
        }

        [[nodiscard]] bool PrepareEarPointCache(
            const std::span<const glm::vec3> points,
            const std::span<const glm::vec3> normals,
            OptimizedExecutionScratch& scratch)
        {
            if (points.size() != normals.size())
                return false;
            scratch.EarPoints.assign(points.begin(), points.end());
            scratch.EarNormals.assign(normals.begin(), normals.end());
            return true;
        }

        [[nodiscard]] bool ClearanceOptimized(
            const glm::vec3 base,
            const double supportRadius,
            const OptimizedExecutionScratch& scratch,
            double& clearance,
            Status& failure) noexcept
        {
            const glm::dvec3 query{base};
            const double supportSquared = supportRadius * supportRadius;
            double minimumSquared = std::numeric_limits<double>::infinity();
            bool found = false;
            for (std::size_t i = 0u; i < scratch.EarPoints.size(); ++i)
            {
                const glm::dvec3 offset = query - scratch.EarPoints[i];
                const double distanceSquared = glm::dot(offset, offset);
                if (!(distanceSquared < supportSquared))
                    continue;
                const glm::dvec3 tangential = offset -
                    glm::dot(scratch.EarNormals[i], offset) *
                        scratch.EarNormals[i];
                const double valueSquared = glm::dot(
                    tangential, tangential);
                if (!std::isfinite(valueSquared) || valueSquared < 0.0)
                {
                    failure = Status::NumericalFailure;
                    return false;
                }
                minimumSquared = std::min(minimumSquared, valueSquared);
                found = true;
            }
            if (!found)
            {
                failure = Status::EmptyNeighborhood;
                return false;
            }
            clearance = std::sqrt(minimumSquared);
            if (!std::isfinite(clearance))
            {
                failure = Status::NumericalFailure;
                return false;
            }
            return true;
        }

        [[nodiscard]] bool InsertEarPoint(
            const EarStrategy& strategy,
            const Params& params,
            std::vector<glm::vec3>& points,
            std::vector<glm::vec3>& normals,
            Diagnostics& diagnostics,
            Status& failure,
            OptimizedExecutionScratch* const optimizedScratch = nullptr)
        {
            if (optimizedScratch != nullptr &&
                !PrepareEarPointCache(points, normals, *optimizedScratch))
            {
                failure = Status::InvalidNormals;
                return false;
            }

            double bestPriority = -1.0;
            std::size_t bestFirst = 0u;
            std::size_t bestSecond = 0u;
            glm::vec3 bestBase{};
            const auto considerPair = [&](const std::size_t i,
                                          const std::size_t j)
            {
                const double pairDistanceSquared =
                    DistanceSquared(points[i], points[j]);
                if (!(pairDistanceSquared <
                      params.SupportRadius * params.SupportRadius))
                {
                    return true;
                }
                const glm::vec3 base = 0.5f * (points[i] + points[j]);
                double clearance = 0.0;
                const bool clearanceSucceeded = optimizedScratch != nullptr
                    ? ClearanceOptimized(
                        base, params.SupportRadius, *optimizedScratch,
                        clearance, failure)
                    : Clearance(
                        base, points, normals, params.SupportRadius,
                        clearance, failure);
                if (!clearanceSucceeded)
                {
                    return false;
                }
                const double alignment = std::clamp(
                    glm::dot(
                        glm::dvec3(normals[i]),
                        glm::dvec3(normals[j])),
                    -1.0, 1.0);
                const double priority = std::pow(
                    2.0 - alignment, strategy.EdgeSensitivity) * clearance;
                ++diagnostics.EdgePriorityEvaluations;
                if (!std::isfinite(priority))
                {
                    failure = Status::NumericalFailure;
                    return false;
                }
                if (priority > bestPriority)
                {
                    bestPriority = priority;
                    bestFirst = i;
                    bestSecond = j;
                    bestBase = base;
                }
                return true;
            };

            for (std::size_t i = 0u; i < points.size(); ++i)
            {
                for (std::size_t j = i + 1u; j < points.size(); ++j)
                {
                    if (!considerPair(i, j))
                        return false;
                }
            }

            const double clearanceFloor = std::max(
                params.SupportRadius * 1.0e-7,
                std::numeric_limits<double>::epsilon());
            if (!(bestPriority >= 0.0) ||
                bestFirst == bestSecond ||
                !(bestPriority > clearanceFloor))
            {
                failure = Status::UpsamplingFailed;
                return false;
            }

            double firstDistance = 0.0;
            double secondDistance = 0.0;
            if (!ProjectionDistance(
                    bestBase, normals[bestFirst], points, normals,
                    params.SupportRadius, strategy.NormalAngleRadians,
                    firstDistance, failure) ||
                !ProjectionDistance(
                    bestBase, normals[bestSecond], points, normals,
                    params.SupportRadius, strategy.NormalAngleRadians,
                    secondDistance, failure))
            {
                return false;
            }
            const glm::vec3 candidate =
                std::abs(firstDistance) <= std::abs(secondDistance)
                ? normals[bestFirst]
                : normals[bestSecond];
            glm::vec3 insertedNormal{};
            if (!RefineNormalAtBase(
                    bestBase, candidate, points, normals,
                    params.SupportRadius, strategy.NormalAngleRadians,
                    insertedNormal, failure))
            {
                return false;
            }
            double projectionDistance = 0.0;
            if (!ProjectionDistance(
                    bestBase, insertedNormal, points, normals,
                    params.SupportRadius, strategy.NormalAngleRadians,
                    projectionDistance, failure))
            {
                return false;
            }
            glm::vec3 insertedPoint{};
            if (!ToFiniteVec3(
                    glm::dvec3(bestBase) +
                        projectionDistance * glm::dvec3(insertedNormal),
                    insertedPoint))
            {
                failure = Status::NumericalFailure;
                return false;
            }
            points.push_back(insertedPoint);
            normals.push_back(insertedNormal);
            ++diagnostics.InsertedPointCount;
            return true;
        }

        [[nodiscard]] bool ProgressiveInsert(
            const EarStrategy& strategy,
            const Params& params,
            const std::size_t target,
            std::vector<glm::vec3>& points,
            std::vector<glm::vec3>& normals,
            Diagnostics& diagnostics,
            Status& failure,
            OptimizedExecutionScratch* const optimizedScratch = nullptr)
        {
            while (points.size() < target)
            {
                if (!InsertEarPoint(
                        strategy, params, points, normals,
                        diagnostics, failure, optimizedScratch))
                {
                    return false;
                }
            }
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
        case StrategyKind::Ear: return "ear";
        }
        return "invalid";
    }

    std::string_view DebugName(const WeightingMode weighting) noexcept
    {
        switch (weighting)
        {
        case WeightingMode::Isotropic: return "isotropic";
        case WeightingMode::Anisotropic: return "anisotropic";
        }
        return "invalid";
    }

    std::string_view DebugName(const NormalSourcePolicy policy) noexcept
    {
        switch (policy)
        {
        case NormalSourcePolicy::AuthoredOrEstimate:
            return "authored_or_estimate";
        case NormalSourcePolicy::RequireAuthored:
            return "require_authored";
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
        case Status::InvalidNormalAngle: return "invalid_normal_angle";
        case Status::InvalidEdgeSensitivity:
            return "invalid_edge_sensitivity";
        case Status::InvalidNormalRefinementRounds:
            return "invalid_normal_refinement_rounds";
        case Status::NormalsRequired: return "normals_required";
        case Status::InvalidNormals: return "invalid_normals";
        case Status::NormalEstimationFailed:
            return "normal_estimation_failed";
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
        case Status::UpsamplingFailed: return "upsampling_failed";
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
        if (std::holds_alternative<ClopStrategy>(strategy))
            return StrategyKind::Clop;
        return StrategyKind::Ear;
    }

    Result Consolidate(
        const std::span<const glm::vec3> positions,
        const Params& params)
    {
        return Consolidate(
            positions, std::span<const glm::vec3>{}, params);
    }

    [[nodiscard]] Result ConsolidateImpl(
        const std::span<const glm::vec3> positions,
        const std::span<const glm::vec3> normals,
        const Params& params,
        const bool optimized)
    {
        Result result = InvalidRequest(positions, normals, params);
        result.Diagnostics.Implementation = optimized
            ? Validation::kOptimizedCandidateImplementation
            : kCpuReferenceImplementation;
        if (!result.Succeeded())
            return result;

        OptimizedExecutionScratch optimizedScratchStorage{};
        OptimizedExecutionScratch* const optimizedScratch = optimized
            ? &optimizedScratchStorage
            : nullptr;

        Status failure = Status::NumericalFailure;
        std::vector<glm::vec3> sourceNormals{};
        if (result.Diagnostics.UsedAnisotropicWeighting &&
            !PrepareSourceNormals(
                positions, normals, params, sourceNormals,
                result.Diagnostics, failure))
        {
            result.State = failure;
            return result;
        }

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
            if (optimized && !PrepareContinuousAttractionModel(
                    continuousModel, params.SupportRadius))
            {
                result.State = Status::NumericalFailure;
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
            const auto density = optimizedScratch != nullptr
                ? Kernels::ComputeDensityWeights(
                    positions,
                    sourceIndex,
                    params.SupportRadius,
                    Kernels::KernelType::ThetaLop,
                    Kernels::DensityWeightMode::Reciprocal,
                    optimizedScratch->RadiusQuery)
                : Kernels::ComputeDensityWeights(
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

        const std::size_t target = params.TargetPointCount == 0u
            ? positions.size()
            : params.TargetPointCount;
        const std::size_t initialTarget =
            std::min(target, positions.size());
        std::vector<glm::vec3> projected{};
        std::vector<std::size_t> selectedIndices{};
        if (!InitializeProjected(
                positions, initialTarget, params.Seed,
                projected, selectedIndices))
        {
            result.State = Status::NumericalFailure;
            return result;
        }

        std::vector<glm::vec3> projectedNormals{};
        if (result.Diagnostics.UsedAnisotropicWeighting)
        {
            projectedNormals.reserve(selectedIndices.size());
            for (const std::size_t selected : selectedIndices)
            {
                if (selected >= sourceNormals.size())
                {
                    result.State = Status::InvalidNormals;
                    return result;
                }
                projectedNormals.push_back(sourceNormals[selected]);
            }
        }
        else
        {
            const bool initialized = continuousModelPtr != nullptr
                ? ContinuousL2Initialize(
                    *continuousModelPtr, params.SupportRadius, projected,
                    result.Diagnostics, failure, optimized)
                : L2Initialize(
                    positions, sourceIndex, sourceWeights,
                    params.SupportRadius, projected,
                    result.Diagnostics, failure,
                    optimizedScratch != nullptr
                        ? &optimizedScratch->RadiusQuery
                        : nullptr);
            if (!initialized)
            {
                result.State = failure;
                return result;
            }
        }

        const std::uint32_t refinementRounds =
            NormalRefinementRounds(params.Method);
        const std::uint32_t iterationLimit =
            result.Diagnostics.UsedAnisotropicWeighting
                ? refinementRounds
                : params.MaxIterations;
        for (std::uint32_t iteration = 0u;
             iteration < iterationLimit;
             ++iteration)
        {
            if (result.Diagnostics.UsedAnisotropicWeighting &&
                !RefineNormals(
                    projected, params.SupportRadius,
                    NormalAngle(params.Method), projectedNormals,
                    result.Diagnostics, failure,
                    optimizedScratch != nullptr
                        ? &optimizedScratch->RadiusQuery
                        : nullptr))
            {
                result.State = failure;
                return result;
            }
            if (!Iterate(
                    positions,
                    sourceIndex,
                    sourceWeights,
                    continuousModelPtr,
                    projectedNormals,
                    params,
                    projected,
                    result.Diagnostics,
                    failure,
                    optimizedScratch))
            {
                result.State = failure;
                return result;
            }
            result.Diagnostics.Iterations = iteration + 1u;
            if (result.Diagnostics.UsedAnisotropicWeighting)
            {
                if (iteration + 1u == iterationLimit)
                {
                    // The governing EAR stop rule is a fixed number of
                    // alternating normal/projection rounds (three by
                    // default), rather than a displacement tolerance.
                    result.Diagnostics.Converged = true;
                    break;
                }
            }
            else if (result.Diagnostics.MaxDisplacement <=
                     params.ConvergenceTolerance)
            {
                result.Diagnostics.Converged = true;
                break;
            }
        }

        if (result.Diagnostics.Converged)
        {
            if (const auto* ear = std::get_if<EarStrategy>(&params.Method);
                ear != nullptr && projected.size() < target)
            {
                if (!ProgressiveInsert(
                        *ear, params, target, projected, projectedNormals,
                        result.Diagnostics, failure, optimizedScratch))
                {
                    result.State = failure;
                    return result;
                }
            }
        }

        result.Diagnostics.OutputPointCount = projected.size();
        result.Positions = std::move(projected);
        if (result.Diagnostics.UsedAnisotropicWeighting)
            result.Normals = std::move(projectedNormals);
        result.State = result.Diagnostics.Converged
            ? Status::Success
            : Status::NotConverged;
        return result;
    }

    Result Consolidate(
        const std::span<const glm::vec3> positions,
        const std::span<const glm::vec3> normals,
        const Params& params)
    {
        return ConsolidateImpl(positions, normals, params, false);
    }

    Result Consolidate(const Cloud& cloud, const Params& params)
    {
        if (!cloud.IsValid() || cloud.HasGarbage() || cloud.IsSubmeshView())
        {
            Result result{};
            result.State = Status::InvalidCloud;
            result.Diagnostics = InitialDiagnostics(
                cloud.Positions(), params);
            return result;
        }
        if (cloud.HasNormals())
            return Consolidate(cloud.Positions(), cloud.Normals(), params);
        return Consolidate(cloud.Positions(), params);
    }

    namespace Validation
    {
        Result ConsolidateCpuOptimizedCandidate(
            const std::span<const glm::vec3> positions,
            const Params& params)
        {
            return ConsolidateCpuOptimizedCandidate(
                positions, std::span<const glm::vec3>{}, params);
        }

        Result ConsolidateCpuOptimizedCandidate(
            const std::span<const glm::vec3> positions,
            const std::span<const glm::vec3> normals,
            const Params& params)
        {
            return ConsolidateImpl(positions, normals, params, true);
        }
    }
}
