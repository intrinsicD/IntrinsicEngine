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
export import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
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

    // The standalone point-sampling operation (RUNTIME-274): orders the points of one entity's
    // point domain with the chosen method and publishes either a rank and selection property
    // pair on that domain or a new point-cloud entity with the first `Count` samples.
    inline constexpr std::string_view kPointSamplingConfigSectionName = "sandbox.point_sampling";
    inline constexpr std::string_view kPointSamplingConfigSectionSchemaId = "intrinsic.runtime.sandbox.point_sampling";
    enum class PointSamplingOutput : std::uint8_t { Properties = 0, PointCloud };

    struct PointSamplingOperationConfig
    {
        std::uint32_t SourceStableEntityId{0u};
        // Unknown domain resolves to the entity's primary point domain.
        GeometryPropertyRef Positions{GeometryElementDomain::Unknown, "v:position", Geometry::PropertyValueKind::Vec3};
        std::uint32_t Count{1024u}; // 0: order every point
        PointSamplingConfig Sampling{};
        // Optional float property on the positions' domain: importance weights (farthest point,
        // coupled sieve; positive) or priority scores (Poisson feature priority). Empty: none.
        std::string WeightsName{};
        PointSamplingOutput Output{PointSamplingOutput::Properties};
        std::string RankName{"v:sample_rank"};         // float rank in the order, -1 when unranked
        std::string SelectedName{"v:sample_selected"}; // true for the first Count samples
    };

    [[nodiscard]] std::string SerializePointSamplingOperationConfig(const PointSamplingOperationConfig& config);
    [[nodiscard]] Core::Config::EngineConfigSectionValidationResult ValidatePointSamplingOperationConfigSection(
        std::string_view payload, std::string_view reference, std::string_view subject);
    [[nodiscard]] std::optional<PointSamplingOperationConfig> GetPointSamplingOperationConfig(const Core::Config::EngineConfig& config);
    void SetPointSamplingOperationConfig(Core::Config::EngineConfig& config, const PointSamplingOperationConfig& value);
    [[nodiscard]] Core::Config::EngineConfigSectionRegistration MakePointSamplingConfigSectionRegistration();
    [[nodiscard]] std::span<const ConfigFieldSpec> PointSamplingOperationFieldSpecs() noexcept;
    [[nodiscard]] std::optional<PointSamplingOperationConfig> DecodePointSamplingOperationConfig(std::string_view payload);
}
