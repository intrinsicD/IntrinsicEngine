module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <random>
#include <span>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

module Geometry.PointCloud.Consolidation;

import Geometry.KDTree;

namespace Geometry::PointCloud::Consolidation
{
    namespace
    {
        // Coincident-pair guard for the singular 1/r kernels; pairs closer
        // than this are skipped (the limit of the localized L1 median keeps
        // the point in place, which the fallback below realizes explicitly).
        constexpr double kMinPairDistance = 1.0e-12;

        [[nodiscard]] bool IsFinite(const glm::vec3& v) noexcept
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        // theta(r) = exp(-r^2 / (h/4)^2): the rapidly decreasing weight shared
        // by the attraction, repulsion, and density terms (Lipman et al. 2007
        // Eq. 4; Huang et al. 2009 Sec. 3). At r = h the weight is exp(-16),
        // so neighborhoods are truncated at radius h.
        [[nodiscard]] double Theta(const double r2, const double invQuarterH2) noexcept
        {
            return std::exp(-r2 * invQuarterH2);
        }

        struct ValidatedInput
        {
            ConsolidateStatus Status{ConsolidateStatus::Success};
            std::vector<std::size_t> InitialIndices{};
        };

        [[nodiscard]] ValidatedInput Validate(const std::span<const glm::vec3> points,
                                              const WlopParams& params)
        {
            ValidatedInput out{};
            if (points.empty())
            {
                out.Status = ConsolidateStatus::EmptyInput;
                return out;
            }
            if (points.size() < 3u)
            {
                out.Status = ConsolidateStatus::InsufficientPoints;
                return out;
            }
            for (const glm::vec3& p : points)
            {
                if (!IsFinite(p))
                {
                    out.Status = ConsolidateStatus::NonFinitePositions;
                    return out;
                }
            }
            if (!std::isfinite(params.SupportRadius) || params.SupportRadius <= 0.0f)
            {
                out.Status = ConsolidateStatus::InvalidSupportRadius;
                return out;
            }
            if (!std::isfinite(params.RepulsionWeight) || params.RepulsionWeight < 0.0f ||
                params.RepulsionWeight >= 0.5f)
            {
                out.Status = ConsolidateStatus::InvalidRepulsionWeight;
                return out;
            }
            if (params.Iterations == 0u)
            {
                out.Status = ConsolidateStatus::InvalidIterationCount;
                return out;
            }

            if (!params.InitialIndices.empty())
            {
                std::vector<std::size_t> indices = params.InitialIndices;
                std::sort(indices.begin(), indices.end());
                const bool outOfRange = indices.back() >= points.size();
                const bool duplicated =
                    std::adjacent_find(indices.begin(), indices.end()) != indices.end();
                if (outOfRange || duplicated)
                {
                    out.Status = ConsolidateStatus::InvalidInitialIndices;
                    return out;
                }
                out.InitialIndices = std::move(indices);
                return out;
            }

            if (params.TargetCount == 0u || params.TargetCount > points.size())
            {
                out.Status = ConsolidateStatus::InvalidTargetCount;
                return out;
            }

            // Same draw as Geometry.PointCloud.Utils RandomSubsample (partial
            // Fisher-Yates over std::mt19937, ascending result), restated in
            // the index domain so the span overload needs no Cloud copy.
            std::vector<std::size_t> indices(points.size());
            std::iota(indices.begin(), indices.end(), std::size_t{0});
            std::mt19937 rng(params.Seed);
            for (std::size_t i = 0; i < params.TargetCount; ++i)
            {
                std::uniform_int_distribution<std::size_t> pick(i, indices.size() - 1u);
                std::swap(indices[i], indices[pick(rng)]);
            }
            indices.resize(params.TargetCount);
            std::sort(indices.begin(), indices.end());
            out.InitialIndices = std::move(indices);
            return out;
        }

        // WLOP input-density weights v_j = 1 + sum_{j' != j} theta(|p_j - p_j'|)
        // (Huang et al. 2009 Sec. 3.2); computed once over the input cloud.
        [[nodiscard]] std::vector<double> ComputeInputDensity(
            const std::span<const glm::vec3> points,
            const KDTree& inputTree,
            const float supportRadius,
            const double invQuarterH2,
            bool& queryFailed)
        {
            std::vector<double> density(points.size(), 1.0);
            std::vector<KDTree::ElementIndex> neighbors;
            for (std::size_t j = 0; j < points.size(); ++j)
            {
                const auto query =
                    inputTree.QueryRadius(points[j], supportRadius, neighbors);
                if (!query.has_value())
                {
                    queryFailed = true;
                    return density;
                }
                double sum = 0.0;
                for (const KDTree::ElementIndex neighbor : neighbors)
                {
                    if (static_cast<std::size_t>(neighbor) == j)
                        continue;
                    const glm::dvec3 delta =
                        glm::dvec3(points[j]) - glm::dvec3(points[neighbor]);
                    sum += Theta(glm::dot(delta, delta), invQuarterH2);
                }
                density[j] = 1.0 + sum;
            }
            return density;
        }
    }

    std::string_view DebugName(const ConsolidateStatus status) noexcept
    {
        switch (status)
        {
        case ConsolidateStatus::Success:
            return "Success";
        case ConsolidateStatus::EmptyInput:
            return "EmptyInput";
        case ConsolidateStatus::InsufficientPoints:
            return "InsufficientPoints";
        case ConsolidateStatus::NonFinitePositions:
            return "NonFinitePositions";
        case ConsolidateStatus::InvalidSupportRadius:
            return "InvalidSupportRadius";
        case ConsolidateStatus::InvalidRepulsionWeight:
            return "InvalidRepulsionWeight";
        case ConsolidateStatus::InvalidIterationCount:
            return "InvalidIterationCount";
        case ConsolidateStatus::InvalidTargetCount:
            return "InvalidTargetCount";
        case ConsolidateStatus::InvalidInitialIndices:
            return "InvalidInitialIndices";
        case ConsolidateStatus::SpatialIndexBuildFailed:
            return "SpatialIndexBuildFailed";
        case ConsolidateStatus::SpatialIndexQueryFailed:
            return "SpatialIndexQueryFailed";
        }
        return "Unknown";
    }

    std::string_view DebugName(const Variant variant) noexcept
    {
        switch (variant)
        {
        case Variant::Wlop:
            return "Wlop";
        case Variant::Lop:
            return "Lop";
        }
        return "Unknown";
    }

    ConsolidateResult Consolidate(const std::span<const glm::vec3> points,
                                  const WlopParams& params)
    {
        ConsolidateResult result{};
        ValidatedInput validated = Validate(points, params);
        result.Status = validated.Status;
        if (validated.Status != ConsolidateStatus::Success)
            return result;

        const double h = static_cast<double>(params.SupportRadius);
        const double invQuarterH2 = 16.0 / (h * h);
        const double mu = static_cast<double>(params.RepulsionWeight);
        const bool useDensityWeights = params.Method == Variant::Wlop;

        KDTree inputTree;
        if (!inputTree.BuildFromPoints(points).has_value())
        {
            result.Status = ConsolidateStatus::SpatialIndexBuildFailed;
            return result;
        }

        bool queryFailed = false;
        std::vector<double> inputDensity;
        if (useDensityWeights)
        {
            inputDensity = ComputeInputDensity(points, inputTree, params.SupportRadius,
                                               invQuarterH2, queryFailed);
            if (queryFailed)
            {
                result.Status = ConsolidateStatus::SpatialIndexQueryFailed;
                return result;
            }
        }

        std::vector<glm::vec3> projected;
        projected.reserve(validated.InitialIndices.size());
        for (const std::size_t index : validated.InitialIndices)
            projected.push_back(points[index]);
        const std::size_t projectedCount = projected.size();

        result.Report.InputPointCount = points.size();
        result.Report.ProjectedPointCount = projectedCount;
        result.Report.Movement.reserve(params.Iterations);

        std::vector<glm::vec3> next(projectedCount);
        std::vector<double> projectedDensity(projectedCount, 1.0);
        std::vector<KDTree::ElementIndex> neighbors;

        for (std::uint32_t iteration = 0; iteration < params.Iterations; ++iteration)
        {
            // The first iteration is the papers' L2 initializer: a plain
            // theta-weighted local mean without the 1/r factor and without
            // repulsion (Lipman et al. 2007 Sec. 3; restated by Preiner et
            // al. 2014). The singular Weiszfeld weights only start once the
            // projected set no longer coincides with input points.
            const bool medianIteration = iteration > 0u;
            const bool useRepulsion = medianIteration && mu > 0.0;

            KDTree projectedTree;
            if (useRepulsion &&
                !projectedTree.BuildFromPoints(projected).has_value())
            {
                result.Status = ConsolidateStatus::SpatialIndexBuildFailed;
                return result;
            }

            // WLOP projected-set density weights w_i = 1 + sum theta, refreshed
            // every iteration because the projected set moves; only consumed
            // by the repulsion term.
            if (useDensityWeights && useRepulsion)
            {
                for (std::size_t i = 0; i < projectedCount; ++i)
                {
                    const auto query = projectedTree.QueryRadius(
                        projected[i], params.SupportRadius, neighbors);
                    if (!query.has_value())
                    {
                        result.Status = ConsolidateStatus::SpatialIndexQueryFailed;
                        return result;
                    }
                    double sum = 0.0;
                    for (const KDTree::ElementIndex neighbor : neighbors)
                    {
                        if (static_cast<std::size_t>(neighbor) == i)
                            continue;
                        const glm::dvec3 delta =
                            glm::dvec3(projected[i]) - glm::dvec3(projected[neighbor]);
                        sum += Theta(glm::dot(delta, delta), invQuarterH2);
                    }
                    projectedDensity[i] = 1.0 + sum;
                }
            }

            double meanMovement = 0.0;
            double maxMovement = 0.0;

            for (std::size_t i = 0; i < projectedCount; ++i)
            {
                const glm::dvec3 x(projected[i]);

                // Attraction: the localized L1 median of the input inside the
                // support radius, alpha_ij = theta(|x_i - p_j|) / |x_i - p_j|,
                // divided by v_j under WLOP (Huang et al. 2009 Eq. 6;
                // Lipman et al. 2007 Eq. 6 with v_j = 1).
                glm::dvec3 attraction(0.0);
                double attractionWeight = 0.0;
                {
                    const auto query = inputTree.QueryRadius(
                        projected[i], params.SupportRadius, neighbors);
                    if (!query.has_value())
                    {
                        result.Status = ConsolidateStatus::SpatialIndexQueryFailed;
                        return result;
                    }
                    for (const KDTree::ElementIndex neighbor : neighbors)
                    {
                        const glm::dvec3 p(points[neighbor]);
                        const glm::dvec3 delta = x - p;
                        const double r2 = glm::dot(delta, delta);
                        double alpha = Theta(r2, invQuarterH2);
                        if (medianIteration)
                        {
                            const double r = std::sqrt(r2);
                            if (r < kMinPairDistance)
                                continue;
                            alpha /= r;
                        }
                        if (useDensityWeights)
                            alpha /= inputDensity[neighbor];
                        attraction += p * alpha;
                        attractionWeight += alpha;
                    }
                }

                glm::dvec3 updated = x;
                if (attractionWeight > 0.0)
                    updated = attraction / attractionWeight;
                else
                    ++result.Report.EmptyAttractionNeighborhoods;

                // Repulsion: beta_ii' = theta(|x_i - x_i'|) / |x_i - x_i'|
                // (eta(r) = -r, the WLOP replacement for Lipman's original
                // 1/(3r^3), Huang et al. 2009 Sec. 3.3), scaled by w_i'
                // under WLOP; pushes projected points apart within h.
                if (useRepulsion)
                {
                    const auto query = projectedTree.QueryRadius(
                        projected[i], params.SupportRadius, neighbors);
                    if (!query.has_value())
                    {
                        result.Status = ConsolidateStatus::SpatialIndexQueryFailed;
                        return result;
                    }
                    glm::dvec3 repulsion(0.0);
                    double repulsionWeight = 0.0;
                    for (const KDTree::ElementIndex neighbor : neighbors)
                    {
                        if (static_cast<std::size_t>(neighbor) == i)
                            continue;
                        const glm::dvec3 other(projected[neighbor]);
                        const glm::dvec3 delta = x - other;
                        const double r2 = glm::dot(delta, delta);
                        const double r = std::sqrt(r2);
                        if (r < kMinPairDistance)
                            continue;
                        double beta = Theta(r2, invQuarterH2) / r;
                        if (useDensityWeights)
                            beta *= projectedDensity[neighbor];
                        repulsion += delta * beta;
                        repulsionWeight += beta;
                    }
                    if (repulsionWeight > 0.0)
                        updated += mu * (repulsion / repulsionWeight);
                    else
                        ++result.Report.EmptyRepulsionNeighborhoods;
                }

                next[i] = glm::vec3(updated);
                const double movement = glm::length(updated - x);
                meanMovement += movement;
                maxMovement = std::max(maxMovement, movement);
            }

            projected.swap(next);
            result.Report.Movement.push_back(IterationMovement{
                meanMovement / static_cast<double>(projectedCount), maxMovement});
            ++result.Report.IterationsRun;
        }

        result.Status = ConsolidateStatus::Success;
        result.Positions = std::move(projected);
        result.InitialIndices = std::move(validated.InitialIndices);
        return result;
    }

    ConsolidateResult Consolidate(const Cloud& cloud, const WlopParams& params)
    {
        // Live points gathered in slot order; indices reported in the result
        // refer to this gathered order, not to raw cloud slots.
        std::vector<glm::vec3> livePositions;
        livePositions.reserve(cloud.VertexCount());
        for (const VertexHandle vertex : cloud.LivePoints())
            livePositions.push_back(cloud.Position(vertex));
        return Consolidate(std::span<const glm::vec3>(livePositions), params);
    }
}
