module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.PointCloud.Consolidation;

import Geometry.PointCloud;

export namespace Geometry::PointCloud::Consolidation
{
    inline constexpr std::string_view kCpuReferenceImplementation =
        "cpu_reference";

    struct LopStrategy
    {
    };

    struct WlopStrategy
    {
    };

    using Strategy = std::variant<LopStrategy, WlopStrategy>;

    enum class StrategyKind : std::uint8_t
    {
        Lop = 0,
        Wlop,
    };

    enum class Status : std::uint8_t
    {
        Success = 0,
        EmptyInput,
        TooFewPoints,
        InvalidCloud,
        NonFiniteInput,
        InvalidSupportRadius,
        InvalidRepulsionWeight,
        InvalidIterationLimit,
        InvalidConvergenceTolerance,
        InvalidTargetCount,
        ResourceLimit,
        SpatialIndexBuildFailed,
        SpatialQueryFailed,
        EmptyNeighborhood,
        DensityEstimationFailed,
        NumericalFailure,
        NotConverged,
    };

    struct Params
    {
        Strategy Method{WlopStrategy{}};
        // World-unit compact support shared by attraction, repulsion, and
        // density estimation.
        double SupportRadius{1.0};
        // LOP-family balance parameter, constrained to [0, 0.5).
        double RepulsionWeight{0.45};
        std::uint32_t MaxIterations{20u};
        // Absolute world-unit maximum displacement stopping tolerance.
        double ConvergenceTolerance{1.0e-4};
        // Zero preserves the input count. LOP/WLOP otherwise downsample only.
        std::size_t TargetPointCount{0u};
        std::uint32_t Seed{42u};
    };

    struct Diagnostics
    {
        std::string_view Implementation{kCpuReferenceImplementation};
        StrategyKind Strategy{StrategyKind::Wlop};
        std::size_t InputPointCount{0u};
        std::size_t OutputPointCount{0u};
        std::uint32_t Iterations{0u};
        bool Converged{false};
        bool UsedDensityWeighting{false};
        std::size_t AttractionContributionCount{0u};
        std::size_t RepulsionContributionCount{0u};
        std::size_t DensityContributionCount{0u};
        std::size_t EmptyNeighborhoodCount{0u};
        double AverageDisplacement{0.0};
        double MaxDisplacement{0.0};
    };

    struct Result
    {
        Status State{Status::EmptyInput};
        std::vector<glm::vec3> Positions{};
        Diagnostics Diagnostics{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return State == Status::Success;
        }
    };

    [[nodiscard]] std::string_view DebugName(
        StrategyKind strategy) noexcept;
    [[nodiscard]] std::string_view DebugName(Status status) noexcept;
    [[nodiscard]] StrategyKind Kind(const Strategy& strategy) noexcept;

    // Deterministic serial CPU reference. Initialization reuses the seeded
    // PointCloud::RandomSubsample path, followed by the paper's theta-weighted
    // L2 initializer and fixed-order L1 attraction/repulsion iterations.
    // Inverse-distance terms use a fixed 1% of h floor so coincident samples
    // have a finite, scale-relative interpretation.
    // Failure publishes no positions except NotConverged, which deliberately
    // retains its last finite iterate for diagnostics and caller preview.
    [[nodiscard]] Result Consolidate(
        std::span<const glm::vec3> positions,
        const Params& params = {});

    // Clouds with deleted slots or invalid property storage fail closed. The
    // reference never mutates the input cloud.
    [[nodiscard]] Result Consolidate(
        const Cloud& cloud,
        const Params& params = {});
}
