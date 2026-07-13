module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.PointCloud.Consolidation;

import Geometry.PointCloud;

export namespace Geometry::PointCloud::Consolidation
{
    enum class Variant : std::uint8_t
    {
        // Density-weighted attraction and repulsion (Huang et al. 2009). Default.
        Wlop,
        // Unit density weights (Lipman et al. 2007); same iteration with the
        // density terms disabled.
        Lop,
    };

    enum class ConsolidateStatus : std::uint8_t
    {
        Success,
        EmptyInput,
        InsufficientPoints,
        NonFinitePositions,
        InvalidSupportRadius,
        InvalidRepulsionWeight,
        InvalidIterationCount,
        InvalidTargetCount,
        InvalidInitialIndices,
        SpatialIndexBuildFailed,
        SpatialIndexQueryFailed,
    };

    struct WlopParams
    {
        // Support radius h of the compactly supported weight theta; must be
        // positive and finite. There is deliberately no default: h is
        // data-dependent (typically 6-10x the input's average spacing), so
        // callers derive it from cloud statistics.
        float SupportRadius{0.0f};
        // Repulsion weight mu; valid range [0, 0.5). Zero disables repulsion.
        float RepulsionWeight{0.45f};
        // Fixed-point iteration count; must be at least 1.
        std::uint32_t Iterations{20};
        // Size of the projected set drawn by seeded random subsampling when
        // InitialIndices is empty; must then be in [1, live point count].
        std::size_t TargetCount{0};
        // Optional explicit initial selection (indices into the input cloud's
        // vertex slots); overrides TargetCount when non-empty.
        std::vector<std::size_t> InitialIndices{};
        std::uint32_t Seed{42};
        Variant Method{Variant::Wlop};
    };

    struct IterationMovement
    {
        double MeanMovement{0.0};
        double MaxMovement{0.0};
    };

    struct ConvergenceReport
    {
        std::uint32_t IterationsRun{0};
        // One entry per completed iteration: displacement of the projected set.
        std::vector<IterationMovement> Movement{};
        std::size_t InputPointCount{0};
        std::size_t ProjectedPointCount{0};
        // Projected points whose attraction neighborhood was empty in some
        // iteration (kept in place for that term); summed over iterations.
        std::size_t EmptyAttractionNeighborhoods{0};
        // Projected points with no repulsion neighbors in some iteration;
        // summed over iterations.
        std::size_t EmptyRepulsionNeighborhoods{0};
    };

    struct ConsolidateResult
    {
        ConsolidateStatus Status{ConsolidateStatus::EmptyInput};
        // Projected (consolidated) positions; empty unless Succeeded().
        std::vector<glm::vec3> Positions{};
        // The input indices that seeded the projected set (explicit or drawn).
        std::vector<std::size_t> InitialIndices{};
        ConvergenceReport Report{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == ConsolidateStatus::Success;
        }
    };

    [[nodiscard]] std::string_view DebugName(ConsolidateStatus status) noexcept;
    [[nodiscard]] std::string_view DebugName(Variant variant) noexcept;

    [[nodiscard]] ConsolidateResult Consolidate(std::span<const glm::vec3> points,
                                                const WlopParams& params);
    [[nodiscard]] ConsolidateResult Consolidate(const Cloud& cloud,
                                                const WlopParams& params);
}
