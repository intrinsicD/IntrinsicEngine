module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

module Geometry.SupportRadius;

import Geometry.KDTree;
import Geometry.Statistics;

namespace Geometry::SupportRadius
{
    namespace
    {
        [[nodiscard]] bool IsFinite(const glm::vec3 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                   std::isfinite(value.z);
        }

        [[nodiscard]] double SquaredDistance(
            const glm::vec3 lhs,
            const glm::vec3 rhs) noexcept
        {
            const double x = static_cast<double>(lhs.x) - rhs.x;
            const double y = static_cast<double>(lhs.y) - rhs.y;
            const double z = static_cast<double>(lhs.z) - rhs.z;
            return x * x + y * y + z * z;
        }

        [[nodiscard]] std::size_t SampleIndex(
            const std::size_t sample,
            const std::size_t sampleCount,
            const std::size_t pointCount) noexcept
        {
            if (sampleCount <= 1u)
                return 0u;
            return sample * (pointCount - 1u) / (sampleCount - 1u);
        }

        [[nodiscard]] std::optional<DistanceQuantiles> Quantiles(
            const std::span<const double> values)
        {
            const auto p10 = Statistics::Quantile(values, 0.10);
            const auto p25 = Statistics::Quantile(values, 0.25);
            const auto p50 = Statistics::Quantile(values, 0.50);
            const auto p75 = Statistics::Quantile(values, 0.75);
            const auto p90 = Statistics::Quantile(values, 0.90);
            const auto p95 = Statistics::Quantile(values, 0.95);
            if (!p10 || !p25 || !p50 || !p75 || !p90 || !p95)
                return std::nullopt;
            return DistanceQuantiles{
                .P10 = *p10,
                .P25 = *p25,
                .P50 = *p50,
                .P75 = *p75,
                .P90 = *p90,
                .P95 = *p95,
            };
        }

        [[nodiscard]] double SelectQuantile(
            const DistanceQuantiles& values,
            const CoverageQuantile quantile) noexcept
        {
            switch (quantile)
            {
            case CoverageQuantile::P50: return values.P50;
            case CoverageQuantile::P75: return values.P75;
            case CoverageQuantile::P90: return values.P90;
            case CoverageQuantile::P95: return values.P95;
            }
            return 0.0;
        }

        [[nodiscard]] std::optional<float> BroadPhaseRadius(
            const double radius) noexcept
        {
            if (!std::isfinite(radius) || !(radius > 0.0) ||
                radius > static_cast<double>(
                    std::numeric_limits<float>::max()))
            {
                return std::nullopt;
            }
            float broad = static_cast<float>(radius);
            if (static_cast<double>(broad) < radius)
            {
                broad = std::nextafter(
                    broad, std::numeric_limits<float>::infinity());
            }
            if (!std::isfinite(broad) || !(broad > 0.0F))
                return std::nullopt;
            return broad;
        }

        [[nodiscard]] std::uint64_t SaturatingMultiply(
            const std::uint64_t lhs,
            const std::uint64_t rhs) noexcept
        {
            if (lhs == 0u || rhs == 0u)
                return 0u;
            if (lhs > std::numeric_limits<std::uint64_t>::max() / rhs)
                return std::numeric_limits<std::uint64_t>::max();
            return lhs * rhs;
        }

        [[nodiscard]] std::uint64_t SaturatingAdd(
            const std::uint64_t lhs,
            const std::uint64_t rhs) noexcept
        {
            if (lhs > std::numeric_limits<std::uint64_t>::max() - rhs)
                return std::numeric_limits<std::uint64_t>::max();
            return lhs + rhs;
        }

        [[nodiscard]] bool ValidProfileParams(
            const ProfileParams& params) noexcept
        {
            return params.MaxSamples > 0u &&
                   params.MaxSamples <= kMaximumProfileSamples &&
                   params.MaxNeighborRank > 0u &&
                   params.MaxNeighborRank <= kMaximumNeighborRank;
        }

        [[nodiscard]] bool ValidPolicy(
            const RecommendationPolicy& policy,
            const ProfileParams& params) noexcept
        {
            const bool validQuantile = [&policy]() noexcept
            {
                switch (policy.Quantile)
                {
                case CoverageQuantile::P50:
                case CoverageQuantile::P75:
                case CoverageQuantile::P90:
                case CoverageQuantile::P95:
                    return true;
                }
                return false;
            }();
            return policy.NeighborRank > 0u &&
                   policy.NeighborRank <= params.MaxNeighborRank &&
                   validQuantile &&
                   std::isfinite(policy.RadiusMultiplier) &&
                   policy.RadiusMultiplier > 0.0;
        }
    }

    Analysis Analyze(
        const std::span<const glm::vec3> points,
        const std::optional<double> manualRadius,
        const RecommendationPolicy& policy,
        const WorkloadBudget& budget,
        const ProfileParams& profileParams)
    {
        Analysis result{
            .Source = manualRadius.has_value()
                ? RadiusSource::Manual
                : RadiusSource::Recommended,
            .InputPointCount = points.size(),
            .SelectedNeighborRank = manualRadius.has_value()
                ? 0u
                : policy.NeighborRank,
            .SelectedQuantile = policy.Quantile,
        };
        if (points.empty())
        {
            result.State = Status::EmptyInput;
            return result;
        }
        if (points.size() < 2u)
        {
            result.State = Status::InsufficientPoints;
            return result;
        }
        if (!ValidProfileParams(profileParams) ||
            (!manualRadius.has_value() &&
             !ValidPolicy(policy, profileParams)))
        {
            result.State = Status::InvalidParameters;
            return result;
        }
        if (std::ranges::any_of(points, [](const glm::vec3 point)
            {
                return !IsFinite(point);
            }))
        {
            result.State = Status::NonFiniteInput;
            return result;
        }

        glm::dvec3 minimum{points.front()};
        glm::dvec3 maximum{points.front()};
        for (const glm::vec3 point : points.subspan(1u))
        {
            minimum = glm::min(minimum, glm::dvec3{point});
            maximum = glm::max(maximum, glm::dvec3{point});
        }
        result.BoundingBoxDiagonal = glm::length(maximum - minimum);
        if (!manualRadius.has_value() &&
            !(result.BoundingBoxDiagonal > 0.0))
        {
            result.State = Status::DegenerateNeighborhood;
            return result;
        }

        KDTree index{};
        if (!index.BuildFromPoints(points).has_value())
        {
            result.State = Status::BuildFailed;
            return result;
        }

        result.ProfileSampleCount =
            std::min(profileParams.MaxSamples, points.size());
        if (!manualRadius.has_value())
        {
            result.SelectedNeighborRank = std::min<std::uint32_t>(
                policy.NeighborRank,
                static_cast<std::uint32_t>(points.size() - 1u));
        }
        std::vector<std::size_t> sampleIndices{};
        sampleIndices.reserve(result.ProfileSampleCount);
        for (std::size_t sample = 0u;
             sample < result.ProfileSampleCount;
             ++sample)
        {
            sampleIndices.push_back(SampleIndex(
                sample, result.ProfileSampleCount, points.size()));
        }

        if (!manualRadius.has_value())
        {
            std::vector<std::vector<double>> rankDistances(
                profileParams.MaxNeighborRank);
            for (auto& distances : rankDistances)
                distances.reserve(result.ProfileSampleCount);

            std::vector<KDTree::ElementIndex> neighbors{};
            const std::uint32_t queryCount = static_cast<std::uint32_t>(
                std::min<std::size_t>(
                    points.size(),
                    static_cast<std::size_t>(
                        profileParams.MaxNeighborRank) + 1u));
            for (const std::size_t sampleIndex : sampleIndices)
            {
                const auto query = index.QueryKNN(
                    points[sampleIndex], queryCount, neighbors);
                if (!query.has_value())
                {
                    result.State = Status::QueryFailed;
                    return result;
                }
                result.KnnDistanceEvaluations +=
                    query->DistanceEvaluations;

                std::uint32_t rank = 0u;
                for (const KDTree::ElementIndex neighbor : neighbors)
                {
                    if (neighbor == sampleIndex)
                        continue;
                    const double distanceSquared = SquaredDistance(
                        points[sampleIndex], points[neighbor]);
                    if (distanceSquared == 0.0)
                        ++result.CoincidentNeighborCount;
                    if (rank < profileParams.MaxNeighborRank)
                    {
                        rankDistances[rank].push_back(
                            std::sqrt(distanceSquared));
                        ++rank;
                    }
                }
            }

            result.Ranks.reserve(profileParams.MaxNeighborRank);
            for (std::uint32_t rank = 1u;
                 rank <= profileParams.MaxNeighborRank;
                 ++rank)
            {
                RankProfile profile{
                    .NeighborRank = rank,
                    .ValidSampleCount = rankDistances[rank - 1u].size(),
                };
                if (const auto quantiles = Quantiles(
                        std::span<const double>{rankDistances[rank - 1u]}))
                {
                    profile.Distance = *quantiles;
                }
                result.Ranks.push_back(profile);
            }

            const RankProfile& selected =
                result.Ranks[result.SelectedNeighborRank - 1u];
            if (selected.ValidSampleCount == 0u)
            {
                result.State = Status::MissingNeighborhoodScale;
                return result;
            }
            result.SelectedNeighborDistance =
                SelectQuantile(selected.Distance, policy.Quantile);
            result.Radius = result.SelectedNeighborDistance *
                policy.RadiusMultiplier;
            if (!std::isfinite(result.Radius) || !(result.Radius > 0.0))
            {
                result.State = Status::DegenerateNeighborhood;
                return result;
            }
        }
        else
        {
            result.Radius = *manualRadius;
        }

        const std::optional<float> broadPhaseRadius =
            BroadPhaseRadius(result.Radius);
        if (!broadPhaseRadius.has_value())
        {
            result.State = Status::InvalidRadius;
            return result;
        }

        std::vector<double> supportCounts{};
        supportCounts.reserve(result.ProfileSampleCount);
        std::vector<KDTree::ElementIndex> neighbors{};
        KDTree::RadiusQueryScratch scratch{};
        const double radiusSquared = result.Radius * result.Radius;
        for (const std::size_t sampleIndex : sampleIndices)
        {
            const auto query = index.QueryRadius(
                points[sampleIndex], *broadPhaseRadius, neighbors, scratch);
            if (!query.has_value())
            {
                result.State = Status::QueryFailed;
                return result;
            }
            result.RadiusDistanceEvaluations +=
                query->DistanceEvaluations;

            std::uint32_t exactCount = 0u;
            for (const KDTree::ElementIndex neighbor : neighbors)
            {
                if (SquaredDistance(
                        points[sampleIndex], points[neighbor]) <
                    radiusSquared)
                {
                    ++exactCount;
                }
            }
            supportCounts.push_back(static_cast<double>(exactCount));
            result.SupportNeighborsMax = std::max(
                result.SupportNeighborsMax, exactCount);
        }

        const auto supportP50 = Statistics::Quantile(
            std::span<const double>{supportCounts}, 0.50);
        const auto supportP95 = Statistics::Quantile(
            std::span<const double>{supportCounts}, 0.95);
        if (!supportP50.has_value() || !supportP95.has_value())
        {
            result.State = Status::QueryFailed;
            return result;
        }
        result.SupportNeighborsP50 = *supportP50;
        result.SupportNeighborsP95 = *supportP95;
        result.PredictedQueryCount = budget.PredictedQueryCount == 0u
            ? static_cast<std::uint64_t>(points.size())
            : budget.PredictedQueryCount;
        const std::uint64_t p95Neighbors = static_cast<std::uint64_t>(
            std::ceil(result.SupportNeighborsP95));
        result.PredictedContributionCount = SaturatingAdd(
            budget.FixedContributionCount,
            SaturatingMultiply(
                result.PredictedQueryCount, p95Neighbors));
        result.NeighborLimitExceeded =
            budget.MaxNeighborsPerSample != 0u &&
            result.SupportNeighborsMax > budget.MaxNeighborsPerSample;
        result.ContributionLimitExceeded =
            budget.MaxPredictedContributions != 0u &&
            result.PredictedContributionCount >
                budget.MaxPredictedContributions;
        result.State = result.NeighborLimitExceeded ||
                result.ContributionLimitExceeded
            ? Status::WorkloadLimitExceeded
            : Status::Success;
        return result;
    }

    std::string_view DebugName(const Status status) noexcept
    {
        switch (status)
        {
        case Status::Success: return "success";
        case Status::EmptyInput: return "empty_input";
        case Status::InsufficientPoints: return "insufficient_points";
        case Status::InvalidParameters: return "invalid_parameters";
        case Status::NonFiniteInput: return "non_finite_input";
        case Status::BuildFailed: return "build_failed";
        case Status::QueryFailed: return "query_failed";
        case Status::MissingNeighborhoodScale:
            return "missing_neighborhood_scale";
        case Status::DegenerateNeighborhood:
            return "degenerate_neighborhood";
        case Status::InvalidRadius: return "invalid_radius";
        case Status::WorkloadLimitExceeded:
            return "workload_limit_exceeded";
        }
        return "unknown";
    }

    std::string_view DebugName(const RadiusSource source) noexcept
    {
        switch (source)
        {
        case RadiusSource::Recommended: return "recommended";
        case RadiusSource::Manual: return "manual";
        }
        return "unknown";
    }

    std::string_view DebugName(const CoverageQuantile quantile) noexcept
    {
        switch (quantile)
        {
        case CoverageQuantile::P50: return "p50";
        case CoverageQuantile::P75: return "p75";
        case CoverageQuantile::P90: return "p90";
        case CoverageQuantile::P95: return "p95";
        }
        return "unknown";
    }
}
