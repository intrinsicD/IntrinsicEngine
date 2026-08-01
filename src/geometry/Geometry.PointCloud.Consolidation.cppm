module;

#include <cstddef>
#include <cstdint>
#include <limits>
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

    struct ClopStrategy
    {
        std::uint32_t MixtureComponentCount{16u};
        std::uint32_t MixtureMaxIterations{100u};
        double MixtureRelativeTolerance{1.0e-6};
        // Positive diagonal covariance floor in squared world units.
        double CovarianceFloor{1.0e-6};
    };

    using Strategy = std::variant<LopStrategy, WlopStrategy, ClopStrategy>;

    enum class StrategyKind : std::uint8_t
    {
        Lop = 0,
        Wlop,
        Clop,
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
        InvalidMixtureComponentCount,
        InvalidMixtureParameters,
        ResourceLimit,
        SpatialIndexBuildFailed,
        SpatialQueryFailed,
        EmptyNeighborhood,
        DensityEstimationFailed,
        MixtureFitFailed,
        MixtureNotConverged,
        EmptyContinuousAttraction,
        NumericalFailure,
        NotConverged,
    };

    struct Params
    {
        Strategy Method{WlopStrategy{}};
        // World-unit kernel scale. LOP/WLOP use it as compact support;
        // CLOP scales the paper's analytic Gaussian approximation by it.
        double SupportRadius{1.0};
        // LOP-family balance parameter, constrained to [0, 0.5).
        double RepulsionWeight{0.45};
        std::uint32_t MaxIterations{20u};
        // Absolute world-unit maximum displacement stopping tolerance.
        double ConvergenceTolerance{1.0e-4};
        // Zero preserves the input count; non-zero values downsample only.
        std::size_t TargetPointCount{0u};
        std::uint32_t Seed{42u};
        // Deterministic caller-controlled guard for the serial reference's
        // input-sized allocations. The default preserves the API's natural
        // platform limit; callers with a fixed memory budget can lower it.
        std::size_t MaxInputPointCount{
            std::numeric_limits<std::size_t>::max()};
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
        bool UsedContinuousAttraction{false};
        std::size_t MixtureComponentCount{0u};
        std::uint32_t MixtureIterations{0u};
        bool MixtureConverged{false};
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
