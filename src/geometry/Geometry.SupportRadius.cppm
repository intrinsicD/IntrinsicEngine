module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.SupportRadius;

export namespace Geometry::SupportRadius
{
    inline constexpr std::uint32_t kEstimatorVersion = 1u;
    inline constexpr std::size_t kMaximumProfileSamples = 2'048u;
    inline constexpr std::uint32_t kMaximumNeighborRank = 64u;

    enum class Status : std::uint8_t
    {
        Success = 0u,
        EmptyInput,
        InsufficientPoints,
        InvalidParameters,
        NonFiniteInput,
        BuildFailed,
        QueryFailed,
        MissingNeighborhoodScale,
        DegenerateNeighborhood,
        InvalidRadius,
        WorkloadLimitExceeded,
    };

    enum class RadiusSource : std::uint8_t
    {
        Recommended = 0u,
        Manual,
    };

    enum class CoverageQuantile : std::uint8_t
    {
        P50 = 0u,
        P75,
        P90,
        P95,
    };

    struct ProfileParams
    {
        std::size_t MaxSamples{kMaximumProfileSamples};
        std::uint32_t MaxNeighborRank{32u};
    };

    struct RecommendationPolicy
    {
        std::uint32_t NeighborRank{16u};
        CoverageQuantile Quantile{CoverageQuantile::P75};
        double RadiusMultiplier{1.0};
    };

    struct WorkloadBudget
    {
        // Zero means use InputPointCount as the estimated number of queries.
        std::uint64_t PredictedQueryCount{0u};
        std::uint64_t FixedContributionCount{0u};
        // Zero disables the corresponding limit.
        std::uint32_t MaxNeighborsPerSample{0u};
        std::uint64_t MaxPredictedContributions{0u};
    };

    struct DistanceQuantiles
    {
        double P10{0.0};
        double P25{0.0};
        double P50{0.0};
        double P75{0.0};
        double P90{0.0};
        double P95{0.0};
    };

    struct RankProfile
    {
        std::uint32_t NeighborRank{0u};
        std::size_t ValidSampleCount{0u};
        DistanceQuantiles Distance{};
    };

    struct Analysis
    {
        Status State{Status::EmptyInput};
        RadiusSource Source{RadiusSource::Recommended};
        std::uint32_t EstimatorVersion{kEstimatorVersion};
        std::size_t InputPointCount{0u};
        std::size_t ProfileSampleCount{0u};
        std::size_t CoincidentNeighborCount{0u};
        std::size_t KnnDistanceEvaluations{0u};
        std::size_t RadiusDistanceEvaluations{0u};
        double BoundingBoxDiagonal{0.0};
        std::vector<RankProfile> Ranks{};
        std::uint32_t SelectedNeighborRank{0u};
        CoverageQuantile SelectedQuantile{CoverageQuantile::P75};
        double SelectedNeighborDistance{0.0};
        double Radius{0.0};
        double SupportNeighborsP50{0.0};
        double SupportNeighborsP95{0.0};
        std::uint32_t SupportNeighborsMax{0u};
        std::uint64_t PredictedQueryCount{0u};
        std::uint64_t PredictedContributionCount{0u};
        bool NeighborLimitExceeded{false};
        bool ContributionLimitExceeded{false};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return State == Status::Success;
        }
    };

    // Builds one KD-tree over the complete finite input. Automatic mode
    // profiles a deterministic bounded sample and maps it through policy;
    // manual mode preserves the supplied radius. Both modes sample exact
    // support occupancy and apply the same workload budget.
    [[nodiscard]] Analysis Analyze(
        std::span<const glm::vec3> points,
        std::optional<double> manualRadius,
        const RecommendationPolicy& policy = {},
        const WorkloadBudget& budget = {},
        const ProfileParams& profileParams = {});

    [[nodiscard]] std::string_view DebugName(Status status) noexcept;
    [[nodiscard]] std::string_view DebugName(RadiusSource source) noexcept;
    [[nodiscard]] std::string_view DebugName(
        CoverageQuantile quantile) noexcept;
}
