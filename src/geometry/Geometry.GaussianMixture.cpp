module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <random>
#include <span>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

module Geometry.GaussianMixture;

import Geometry.KMeans;

namespace Geometry::GaussianMixture
{
    namespace
    {
        constexpr double kLogTwoPi =
            1.837877066409345483560659472811;
        constexpr double kMinimumResponsibility = 1.0e-15;

        struct Cholesky3
        {
            double L00{0.0};
            double L10{0.0};
            double L11{0.0};
            double L20{0.0};
            double L21{0.0};
            double L22{0.0};
        };

        struct PreparedModel
        {
            double WeightSum{0.0};
            std::vector<Cholesky3> Factors{};
        };

        [[nodiscard]] bool IsFinite(
            const glm::dvec3& value) noexcept
        {
            return std::isfinite(value.x) &&
                   std::isfinite(value.y) &&
                   std::isfinite(value.z);
        }

        [[nodiscard]] bool IsFinite(
            const glm::vec3& value) noexcept
        {
            return std::isfinite(value.x) &&
                   std::isfinite(value.y) &&
                   std::isfinite(value.z);
        }

        [[nodiscard]] bool IsFinite(
            const glm::dmat3& matrix) noexcept
        {
            for (std::size_t column = 0u; column < 3u; ++column)
            {
                if (!IsFinite(matrix[column]))
                    return false;
            }
            return true;
        }

        [[nodiscard]] std::optional<Cholesky3> Factor(
            const glm::dmat3& covariance) noexcept
        {
            if (!IsFinite(covariance))
                return std::nullopt;
            const auto symmetricPair = [&covariance](
                                           const std::size_t lhs,
                                           const std::size_t rhs) noexcept
            {
                const double forward = covariance[lhs][rhs];
                const double reverse = covariance[rhs][lhs];
                const double lhsDiagonal =
                    std::abs(covariance[lhs][lhs]);
                const double rhsDiagonal =
                    std::abs(covariance[rhs][rhs]);
                const double geometricScale =
                    std::sqrt(lhsDiagonal) *
                    std::sqrt(rhsDiagonal);
                const double scale = std::max({
                    std::abs(forward),
                    std::abs(reverse),
                    geometricScale,
                });
                constexpr double kRelativeTolerance =
                    64.0 * std::numeric_limits<double>::epsilon();
                return std::abs(forward - reverse) <=
                    kRelativeTolerance * scale;
            };
            if (!symmetricPair(0u, 1u) ||
                !symmetricPair(0u, 2u) ||
                !symmetricPair(1u, 2u))
            {
                return std::nullopt;
            }

            const double a00 = covariance[0][0];
            if (!(a00 > 0.0))
                return std::nullopt;
            Cholesky3 result{};
            result.L00 = std::sqrt(a00);
            const double a01 = std::midpoint(
                covariance[0][1], covariance[1][0]);
            const double a02 = std::midpoint(
                covariance[0][2], covariance[2][0]);
            const double a12 = std::midpoint(
                covariance[1][2], covariance[2][1]);
            result.L10 = a01 / result.L00;
            result.L20 = a02 / result.L00;

            const double diagonal11 =
                covariance[1][1] - result.L10 * result.L10;
            if (!(diagonal11 > 0.0))
                return std::nullopt;
            result.L11 = std::sqrt(diagonal11);
            result.L21 =
                (a12 -
                 result.L20 * result.L10) /
                result.L11;

            const double diagonal22 =
                covariance[2][2] -
                result.L20 * result.L20 -
                result.L21 * result.L21;
            if (!(diagonal22 > 0.0))
                return std::nullopt;
            result.L22 = std::sqrt(diagonal22);
            if (!std::isfinite(result.L00) ||
                !std::isfinite(result.L10) ||
                !std::isfinite(result.L11) ||
                !std::isfinite(result.L20) ||
                !std::isfinite(result.L21) ||
                !std::isfinite(result.L22))
            {
                return std::nullopt;
            }
            return result;
        }

        [[nodiscard]] std::optional<double> LogPdfWithFactor(
            const MultivariateGaussian& gaussian,
            const glm::dvec3& point,
            const Cholesky3& factor) noexcept
        {
            if (!IsFinite(gaussian.Mean) || !IsFinite(point))
                return std::nullopt;
            const glm::dvec3 delta = point - gaussian.Mean;
            const double y0 = delta.x / factor.L00;
            const double y1 =
                (delta.y - factor.L10 * y0) /
                factor.L11;
            const double y2 =
                (delta.z - factor.L20 * y0 -
                 factor.L21 * y1) /
                factor.L22;
            const double quadratic =
                y0 * y0 + y1 * y1 + y2 * y2;
            const double logDeterminant =
                2.0 * (std::log(factor.L00) +
                       std::log(factor.L11) +
                       std::log(factor.L22));
            const double value =
                -0.5 * (3.0 * kLogTwoPi +
                        logDeterminant + quadratic);
            return std::isfinite(value)
                ? std::optional<double>{value}
                : std::nullopt;
        }

        [[nodiscard]] std::optional<PreparedModel> PrepareModel(
            const Model& model)
        {
            if (model.Components.empty() ||
                model.Weights.size() !=
                    model.Components.size())
            {
                return std::nullopt;
            }
            PreparedModel prepared{};
            prepared.Factors.reserve(model.Components.size());
            for (std::size_t i = 0u;
                 i < model.Components.size();
                 ++i)
            {
                const auto factor = Factor(
                    model.Components[i].Covariance);
                if (!std::isfinite(model.Weights[i]) ||
                    !(model.Weights[i] > 0.0) ||
                    !IsFinite(model.Components[i].Mean) ||
                    !factor.has_value())
                {
                    return std::nullopt;
                }
                prepared.WeightSum += model.Weights[i];
                prepared.Factors.push_back(*factor);
            }
            if (!std::isfinite(prepared.WeightSum) ||
                !(prepared.WeightSum > 0.0))
            {
                return std::nullopt;
            }
            return prepared;
        }

        [[nodiscard]] bool ValidateModelShape(
            const Model& model)
        {
            return PrepareModel(model).has_value();
        }

        [[nodiscard]] std::optional<std::vector<double>>
        LogWeightedDensitiesPrepared(
            const Model& model,
            const glm::dvec3& point,
            const PreparedModel& prepared)
        {
            if (prepared.Factors.size() != model.Components.size() ||
                !IsFinite(point))
            {
                return std::nullopt;
            }
            std::vector<double> values(
                model.Components.size(), 0.0);
            for (std::size_t i = 0u;
                 i < model.Components.size();
                 ++i)
            {
                const auto logPdf = LogPdfWithFactor(
                    model.Components[i], point, prepared.Factors[i]);
                if (!logPdf)
                    return std::nullopt;
                values[i] =
                    std::log(model.Weights[i] / prepared.WeightSum) +
                    *logPdf;
            }
            return values;
        }

        [[nodiscard]] double LogSumExp(
            const std::span<const double> values) noexcept
        {
            const double maximum = *std::max_element(
                values.begin(), values.end());
            double sum = 0.0;
            for (const double value : values)
                sum += std::exp(value - maximum);
            return maximum + std::log(sum);
        }

        [[nodiscard]] std::optional<std::vector<double>>
        ResponsibilitiesPrepared(
            const Model& model,
            const glm::dvec3& point,
            const PreparedModel& prepared)
        {
            auto logDensities = LogWeightedDensitiesPrepared(
                model, point, prepared);
            if (!logDensities)
                return std::nullopt;
            const double normalization = LogSumExp(*logDensities);
            if (!std::isfinite(normalization))
                return std::nullopt;
            std::vector<double> responsibilities(
                logDensities->size(), 0.0);
            double sum = 0.0;
            for (std::size_t i = 0u; i < logDensities->size(); ++i)
            {
                responsibilities[i] =
                    std::exp((*logDensities)[i] - normalization);
                sum += responsibilities[i];
            }
            if (!(sum > 0.0) || !std::isfinite(sum))
                return std::nullopt;
            for (double& value : responsibilities)
                value /= sum;
            return responsibilities;
        }

        void AddOuterProduct(
            glm::dmat3& covariance,
            const glm::dvec3& delta,
            const double weight) noexcept
        {
            for (std::size_t column = 0u;
                 column < 3u;
                 ++column)
            {
                for (std::size_t row = 0u; row < 3u; ++row)
                {
                    covariance[column][row] +=
                        weight * delta[column] * delta[row];
                }
            }
        }

        void Regularize(
            glm::dmat3& covariance,
            const double floor) noexcept
        {
            covariance[0][0] += floor;
            covariance[1][1] += floor;
            covariance[2][2] += floor;
        }

        [[nodiscard]] std::optional<Model> InitializeModel(
            const std::span<const glm::vec3> points,
            const std::uint32_t componentCount,
            const FitParams& params,
            std::uint32_t& regularizedCount)
        {
            KMeans::KMeansParams kmeansParams{};
            kmeansParams.ClusterCount = componentCount;
            kmeansParams.MaxIterations =
                std::max(8u, std::min(params.MaxIterations, 32u));
            kmeansParams.ConvergenceTolerance = 1.0e-5f;
            kmeansParams.Seed = params.Seed;
            kmeansParams.Init =
                KMeans::Initialization::KMeansPlusPlus;
            const auto clusters =
                KMeans::Cluster(points, kmeansParams);
            if (!clusters ||
                clusters->Centroids.size() != componentCount ||
                clusters->Labels.size() != points.size())
            {
                return std::nullopt;
            }

            Model model{};
            model.Weights.assign(componentCount, 0.0);
            model.Components.resize(componentCount);
            std::vector<std::size_t> counts(
                componentCount, 0u);
            for (std::uint32_t component = 0u;
                 component < componentCount;
                 ++component)
            {
                model.Components[component].Mean =
                    glm::dvec3(clusters->Centroids[component]);
                model.Components[component].Covariance =
                    glm::dmat3{0.0};
            }
            for (std::size_t pointIndex = 0u;
                 pointIndex < points.size();
                 ++pointIndex)
            {
                const std::uint32_t component =
                    clusters->Labels[pointIndex];
                if (component >= componentCount)
                    return std::nullopt;
                const glm::dvec3 delta =
                    glm::dvec3(points[pointIndex]) -
                    model.Components[component].Mean;
                AddOuterProduct(
                    model.Components[component].Covariance,
                    delta, 1.0);
                ++counts[component];
            }

            for (std::uint32_t component = 0u;
                 component < componentCount;
                 ++component)
            {
                if (counts[component] > 0u)
                {
                    model.Components[component].Covariance /=
                        static_cast<double>(counts[component]);
                }
                Regularize(
                    model.Components[component].Covariance,
                    params.CovarianceFloor);
                ++regularizedCount;
                model.Weights[component] =
                    std::max(
                        kMinimumResponsibility,
                        static_cast<double>(counts[component]) /
                            static_cast<double>(points.size()));
            }
            const double weightSum = std::accumulate(
                model.Weights.begin(),
                model.Weights.end(), 0.0);
            for (double& weight : model.Weights)
                weight /= weightSum;
            return ValidateModelShape(model)
                ? std::optional<Model>{std::move(model)}
                : std::nullopt;
        }

        [[nodiscard]] std::optional<Model> EmMap(
            const Model& current,
            const std::span<const glm::vec3> points,
            const double covarianceFloor,
            std::uint32_t& regularizedCount)
        {
            const std::size_t componentCount =
                current.Components.size();
            const auto prepared = PrepareModel(current);
            if (!prepared)
                return std::nullopt;
            std::vector<double> memberships(
                points.size() * componentCount, 0.0);
            std::vector<double> totals(componentCount, 0.0);
            std::vector<glm::dvec3> means(
                componentCount, glm::dvec3{0.0});

            for (std::size_t pointIndex = 0u;
                 pointIndex < points.size();
                 ++pointIndex)
            {
                const auto responsibilities = ResponsibilitiesPrepared(
                    current, glm::dvec3(points[pointIndex]), *prepared);
                if (!responsibilities)
                    return std::nullopt;
                for (std::size_t component = 0u;
                     component < componentCount;
                     ++component)
                {
                    const double value =
                        (*responsibilities)[component];
                    memberships[
                        pointIndex * componentCount + component] =
                        value;
                    totals[component] += value;
                    means[component] +=
                        value * glm::dvec3(points[pointIndex]);
                }
            }

            Model next{};
            next.Weights.resize(componentCount, 0.0);
            next.Components.resize(componentCount);
            for (std::size_t component = 0u;
                 component < componentCount;
                 ++component)
            {
                if (!(totals[component] >
                      kMinimumResponsibility) ||
                    !std::isfinite(totals[component]))
                {
                    return std::nullopt;
                }
                next.Components[component].Mean =
                    means[component] / totals[component];
                next.Components[component].Covariance =
                    glm::dmat3{0.0};
                for (std::size_t pointIndex = 0u;
                     pointIndex < points.size();
                     ++pointIndex)
                {
                    const glm::dvec3 delta =
                        glm::dvec3(points[pointIndex]) -
                        next.Components[component].Mean;
                    AddOuterProduct(
                        next.Components[component].Covariance,
                        delta,
                        memberships[
                            pointIndex * componentCount + component]);
                }
                next.Components[component].Covariance /=
                    totals[component];
                Regularize(
                    next.Components[component].Covariance,
                    covarianceFloor);
                ++regularizedCount;
                next.Weights[component] =
                    totals[component] /
                    static_cast<double>(points.size());
            }
            return ValidateModelShape(next)
                ? std::optional<Model>{std::move(next)}
                : std::nullopt;
        }

        [[nodiscard]] std::vector<double> Flatten(
            const Model& model)
        {
            std::vector<double> values{};
            values.reserve(model.Components.size() * 10u);
            for (std::size_t component = 0u;
                 component < model.Components.size();
                 ++component)
            {
                const auto& gaussian = model.Components[component];
                values.push_back(model.Weights[component]);
                values.push_back(gaussian.Mean.x);
                values.push_back(gaussian.Mean.y);
                values.push_back(gaussian.Mean.z);
                values.push_back(gaussian.Covariance[0][0]);
                values.push_back(gaussian.Covariance[0][1]);
                values.push_back(gaussian.Covariance[0][2]);
                values.push_back(gaussian.Covariance[1][1]);
                values.push_back(gaussian.Covariance[1][2]);
                values.push_back(gaussian.Covariance[2][2]);
            }
            return values;
        }

        [[nodiscard]] std::optional<Model> Decode(
            const std::span<const double> values,
            const std::size_t componentCount,
            const double covarianceFloor,
            std::uint32_t& regularizedCount)
        {
            if (values.size() != componentCount * 10u)
                return std::nullopt;
            Model model{};
            model.Weights.resize(componentCount);
            model.Components.resize(componentCount);
            double weightSum = 0.0;
            for (std::size_t component = 0u;
                 component < componentCount;
                 ++component)
            {
                const std::size_t offset = component * 10u;
                const double weight = values[offset];
                if (!std::isfinite(weight) || !(weight > 0.0))
                    return std::nullopt;
                model.Weights[component] = weight;
                weightSum += weight;
                auto& gaussian = model.Components[component];
                gaussian.Mean = glm::dvec3{
                    values[offset + 1u],
                    values[offset + 2u],
                    values[offset + 3u],
                };
                gaussian.Covariance = glm::dmat3{0.0};
                gaussian.Covariance[0][0] = values[offset + 4u];
                gaussian.Covariance[0][1] = values[offset + 5u];
                gaussian.Covariance[1][0] = values[offset + 5u];
                gaussian.Covariance[0][2] = values[offset + 6u];
                gaussian.Covariance[2][0] = values[offset + 6u];
                gaussian.Covariance[1][1] = values[offset + 7u];
                gaussian.Covariance[1][2] = values[offset + 8u];
                gaussian.Covariance[2][1] = values[offset + 8u];
                gaussian.Covariance[2][2] = values[offset + 9u];
                if (!Factor(gaussian.Covariance))
                {
                    Regularize(
                        gaussian.Covariance, covarianceFloor);
                    ++regularizedCount;
                }
            }
            if (!(weightSum > 0.0) || !std::isfinite(weightSum))
                return std::nullopt;
            for (double& weight : model.Weights)
                weight /= weightSum;
            return ValidateModelShape(model)
                ? std::optional<Model>{std::move(model)}
                : std::nullopt;
        }
    }

    std::optional<double> LogPdf(
        const MultivariateGaussian& gaussian,
        const glm::dvec3& point) noexcept
    {
        const auto factor = Factor(gaussian.Covariance);
        return factor
            ? LogPdfWithFactor(gaussian, point, *factor)
            : std::nullopt;
    }

    std::optional<glm::dvec3> Sample(
        const MultivariateGaussian& gaussian,
        const std::uint64_t seed) noexcept
    {
        const auto factor = Factor(gaussian.Covariance);
        if (!factor || !IsFinite(gaussian.Mean))
            return std::nullopt;
        std::mt19937_64 rng(seed);
        std::normal_distribution<double> standardNormal(0.0, 1.0);
        const double z0 = standardNormal(rng);
        const double z1 = standardNormal(rng);
        const double z2 = standardNormal(rng);
        const glm::dvec3 sample{
            gaussian.Mean.x + factor->L00 * z0,
            gaussian.Mean.y + factor->L10 * z0 +
                factor->L11 * z1,
            gaussian.Mean.z + factor->L20 * z0 +
                factor->L21 * z1 + factor->L22 * z2,
        };
        return IsFinite(sample)
            ? std::optional<glm::dvec3>{sample}
            : std::nullopt;
    }

    std::optional<std::vector<double>> Responsibilities(
        const Model& mixture,
        const glm::dvec3& point)
    {
        const auto prepared = PrepareModel(mixture);
        return prepared
            ? ResponsibilitiesPrepared(mixture, point, *prepared)
            : std::nullopt;
    }

    std::optional<double> LogLikelihood(
        const Model& mixture,
        const std::span<const glm::vec3> points)
    {
        if (points.empty())
            return std::nullopt;
        const auto prepared = PrepareModel(mixture);
        if (!prepared)
            return std::nullopt;
        double likelihood = 0.0;
        for (const glm::vec3& point : points)
        {
            if (!IsFinite(point))
                return std::nullopt;
            const auto logDensities = LogWeightedDensitiesPrepared(
                mixture, glm::dvec3(point), *prepared);
            if (!logDensities)
                return std::nullopt;
            likelihood += LogSumExp(*logDensities);
        }
        return std::isfinite(likelihood)
            ? std::optional<double>{likelihood}
            : std::nullopt;
    }

    FitResult FitEM(
        const std::span<const glm::vec3> points,
        const std::uint32_t componentCount,
        const FitParams& params)
    {
        FitResult result{};
        if (points.empty())
        {
            result.Diagnostics.Status = FitStatus::EmptyInput;
            return result;
        }
        if (componentCount == 0u ||
            componentCount > points.size())
        {
            result.Diagnostics.Status =
                FitStatus::InvalidComponentCount;
            return result;
        }
        if (params.MaxIterations == 0u ||
            !std::isfinite(params.RelativeTolerance) ||
            params.RelativeTolerance < 0.0 ||
            !std::isfinite(params.CovarianceFloor) ||
            !(params.CovarianceFloor > 0.0))
        {
            result.Diagnostics.Status =
                FitStatus::InvalidParameters;
            return result;
        }
        if (params.Acceleration != AccelerationPolicy::None &&
            params.Acceleration != AccelerationPolicy::Anderson)
        {
            result.Diagnostics.Status =
                FitStatus::InvalidParameters;
            return result;
        }
        if (params.Acceleration == AccelerationPolicy::Anderson &&
            (params.Anderson.Window == 0u ||
             !std::isfinite(params.Anderson.Damping) ||
             params.Anderson.Damping < 0.0 ||
             params.Anderson.Damping > 1.0 ||
             !std::isfinite(params.Anderson.Regularization) ||
             params.Anderson.Regularization < 0.0 ||
             !std::isfinite(params.Anderson.SafeguardFactor) ||
             params.Anderson.SafeguardFactor < 1.0))
        {
            result.Diagnostics.Status =
                FitStatus::InvalidParameters;
            return result;
        }
        if (!std::all_of(
                points.begin(), points.end(),
                [](const glm::vec3& point)
                { return IsFinite(point); }))
        {
            result.Diagnostics.Status =
                FitStatus::NonFiniteInput;
            return result;
        }

        auto model = InitializeModel(
            points,
            componentCount,
            params,
            result.Diagnostics.RegularizedCovariances);
        if (!model)
        {
            result.Diagnostics.Status =
                FitStatus::NumericalFailure;
            return result;
        }
        auto currentLikelihood = LogLikelihood(*model, points);
        if (!currentLikelihood)
        {
            result.Diagnostics.Status =
                FitStatus::NumericalFailure;
            return result;
        }
        result.Diagnostics.InitialLogLikelihood =
            *currentLikelihood;

        FixedPoint::AndersonState anderson{};
        anderson.Params = params.Anderson;
        for (std::uint32_t iteration = 0u;
             iteration < params.MaxIterations;
             ++iteration)
        {
            auto plain = EmMap(
                *model,
                points,
                params.CovarianceFloor,
                result.Diagnostics.RegularizedCovariances);
            if (!plain)
            {
                result.Diagnostics.Status =
                    FitStatus::NumericalFailure;
                return result;
            }
            auto plainLikelihood = LogLikelihood(*plain, points);
            if (!plainLikelihood)
            {
                result.Diagnostics.Status =
                    FitStatus::NumericalFailure;
                return result;
            }

            Model acceptedModel = std::move(*plain);
            double acceptedLikelihood = *plainLikelihood;
            if (params.Acceleration ==
                AccelerationPolicy::Anderson)
            {
                const std::vector<double> currentValues =
                    Flatten(*model);
                const std::vector<double> plainValues =
                    Flatten(acceptedModel);
                const auto proposal = FixedPoint::AndersonStep(
                    anderson, currentValues, plainValues);
                bool acceleratedAccepted = false;
                if (proposal.UsedAcceleration())
                {
                    auto accelerated = Decode(
                        proposal.Value,
                        componentCount,
                        params.CovarianceFloor,
                        result.Diagnostics.RegularizedCovariances);
                    if (accelerated)
                    {
                        const auto acceleratedLikelihood =
                            LogLikelihood(*accelerated, points);
                        if (acceleratedLikelihood &&
                            *acceleratedLikelihood >=
                                acceptedLikelihood +
                                    params.RelativeTolerance)
                        {
                            acceptedModel =
                                std::move(*accelerated);
                            acceptedLikelihood =
                                *acceleratedLikelihood;
                            acceleratedAccepted = true;
                            ++result.Diagnostics.AcceleratedSteps;
                        }
                    }
                }
                if (!acceleratedAccepted &&
                    proposal.Status !=
                        FixedPoint::AndersonStepStatus::Plain)
                {
                    ++result.Diagnostics.SafeguardedSteps;
                }
            }

            const double monotonicTolerance =
                1.0e-10 *
                (1.0 + std::abs(*currentLikelihood));
            if (acceptedLikelihood + monotonicTolerance <
                *currentLikelihood)
            {
                result.Diagnostics.Status =
                    FitStatus::NumericalFailure;
                return result;
            }
            if (acceptedLikelihood < *currentLikelihood)
            {
                acceptedLikelihood = *currentLikelihood;
                acceptedModel = *model;
            }

            const double improvement =
                acceptedLikelihood - *currentLikelihood;
            *model = std::move(acceptedModel);
            *currentLikelihood = acceptedLikelihood;
            result.Diagnostics.Iterations = iteration + 1u;
            result.Diagnostics.LogLikelihoodHistory.push_back(
                acceptedLikelihood);
            const double convergenceThreshold =
                params.RelativeTolerance *
                (1.0 + std::abs(acceptedLikelihood));
            if (improvement <= convergenceThreshold)
            {
                result.Diagnostics.Converged = true;
                break;
            }
        }

        result.Mixture = std::move(*model);
        result.Diagnostics.FinalLogLikelihood =
            *currentLikelihood;
        result.Diagnostics.Status = FitStatus::Success;
        return result;
    }
}
