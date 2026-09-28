// Shared, prefixed configuration block for Geometry.PointSampling (RUNTIME-289): every
// operation that picks points embeds it (e.g. "subsample_method", "landmark_method") so the
// sampling method is selectable wherever points are chosen. Field specs drive validation,
// the generated schema and the panel widgets; ToPointSamplingParams maps the block to the
// geometry parameters (the runtime enums mirror the geometry ones, checked statically).
module;
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
export module Extrinsic.Runtime.PointSamplingConfig;
export import Extrinsic.Runtime.ConfigFieldSpec;
import Geometry.PointSampling;
export namespace Extrinsic::Runtime
{
    // Values match Geometry::PointSampling::Method.
    enum class PointSamplingMethod : std::uint8_t
    {
        Random = 0,
        FarthestPoint,
        ProgressivePoisson,
        CoupledSieve,
        FlatGreedy,
        SampleElimination,
        LazyGreedy,
        Tournament,
    };
    // Values match Geometry::PointSampling::GreedyPriority.
    enum class PointSamplingPriority : std::uint8_t { Random = 0, Clearance, CoverageGain };
    // Values match Geometry::PointSampling::PoissonCellSelection.
    enum class PointSamplingPoissonSelection : std::uint8_t { Bounded = 0, Exhaustive, BestOfCandidates, FeaturePriority };

    struct PointSamplingConfig
    {
        PointSamplingMethod Method{PointSamplingMethod::FarthestPoint};
        std::uint32_t Seed{0u};               // random permutation, greedy priorities
        double Eta{0.95};                     // coupled sieve relaxation, (0, 1]
        std::uint32_t CandidateCap{32u};      // coupled sieve batch cap (1 when eta = 1)
        double Beta{1.1};                     // flat/lazy greedy approximation, >= 1
        PointSamplingPriority BatchPriority{PointSamplingPriority::Random};
        PointSamplingPriority BatchOrdering{PointSamplingPriority::Clearance};
        bool VoidDensity{false};              // lazy greedy: void-density priority
        PointSamplingPoissonSelection PoissonSelection{PointSamplingPoissonSelection::Exhaustive};
        std::uint32_t PoissonRetries{1u};
        std::uint32_t PoissonRepairLevels{4u};
        std::uint32_t PoissonBudget{4u};
        bool PoissonRandomPhases{false};
        bool PoissonBalanced{false};          // spatially balanced within-level order
        std::uint32_t PoissonGridWidth{4u};
        std::uint32_t PoissonMaxLevels{16u};
        double EliminationAlpha{8.0};
        double EliminationRadius{0.0};        // 0 derives it from the bounding box
        std::uint32_t ManifoldDimension{3u};  // sample elimination: 2 for surfaces in 3-D
    };

    // Field specs of a block whose names start with `prefix` (e.g. "subsample_"); the
    // returned storage lives for the program.
    [[nodiscard]] std::span<const ConfigFieldSpec> PointSamplingFieldSpecs(std::string_view prefix);
    // Cross-field rules the per-field ranges cannot express (eta = 1 needs a one-point cap;
    // feature priority needs scores, which no embedded block supplies).
    [[nodiscard]] std::optional<std::string> ValidatePointSamplingConfig(const PointSamplingConfig& config);
    [[nodiscard]] std::string_view DisplayName(PointSamplingMethod method) noexcept;

    [[nodiscard]] Geometry::PointSampling::Params ToPointSamplingParams(const PointSamplingConfig& config,
                                                                        std::span<const double> weights = {},
                                                                        std::span<const float> scores = {});
}
