// Persisted statistical/radius/distance-ratio detection and explicit marked-point removal controls.
module;
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
export module Extrinsic.Runtime.OutlierAnalysisConfig;
export import Extrinsic.Runtime.GeometryProperty.Types;
export import Extrinsic.Runtime.DerivedConfigSections;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
export namespace Extrinsic::Runtime
{
    inline constexpr std::string_view kOutlierAnalysisConfigSectionName="sandbox.outlier_analysis";
    inline constexpr std::string_view kOutlierAnalysisConfigSectionSchemaId="intrinsic.runtime.sandbox.outlier_analysis";
    enum class OutlierAnalysisMethod : std::uint8_t { Statistical, Radius, LocalDistanceRatio };
    enum class OutlierAnalysisBackend : std::uint8_t { CpuOctree, CpuLBVH, VulkanLBVH };
    enum class OutlierAnalysisOperation : std::uint8_t { Analyze, RemoveMarked };
    extern "C++" [[nodiscard]] const char* ToString(OutlierAnalysisBackend) noexcept;
    struct OutlierAnalysisConfig
    {
        std::uint32_t StableEntityId{};
        OutlierAnalysisMethod Method{OutlierAnalysisMethod::Statistical};
        OutlierAnalysisBackend Backend{OutlierAnalysisBackend::CpuOctree};
        OutlierAnalysisOperation Operation{OutlierAnalysisOperation::Analyze};
        GeometryPropertyRef Positions{.Domain=GeometryElementDomain::Unknown,.Name="v:position",.ValueKind=Geometry::PropertyValueKind::Vec3};
        GeometryPropertyRef Mask{.Domain=GeometryElementDomain::Unknown,.Name="outlier_mask",.ValueKind=Geometry::PropertyValueKind::UInt32};
        GeometryPropertyRef Score{.Domain=GeometryElementDomain::Unknown,.Name="outlier_score",.ValueKind=Geometry::PropertyValueKind::Float};
        std::uint32_t KNeighbors{16}, MinimumNeighbors{4}, GpuQueryBatchSize{4096};
        float StdDevMultiplier{1}, Radius{1}, ScoreThreshold{2};
        bool Negative{};
    };
    // RUNTIME-354: the section declared once (rows bound to the record, the codec derived from them),
    // keeping the hand-written codec's messages, stage order and conversions (its golden pins them);
    // callers use the generic functions of Extrinsic.Runtime.DerivedConfigSections.
    inline constexpr Geometry::PropertyValueKind kOutlierAnalysisFloatKind[]{Geometry::PropertyValueKind::Float};
    inline constexpr Geometry::PropertyValueKind kOutlierAnalysisUInt32Kind[]{Geometry::PropertyValueKind::UInt32};
    inline constexpr Geometry::PropertyValueKind kOutlierAnalysisVec3Kind[]{Geometry::PropertyValueKind::Vec3};
    inline constexpr std::string_view kOutlierAnalysisMethod[]{"statistical", "radius", "local_distance_ratio"};
    inline constexpr std::string_view kOutlierAnalysisOperation[]{"analyze", "remove_marked"};
    inline constexpr std::string_view kOutlierAnalysisBackend[]{"cpu_octree", "cpu_lbvh", "vulkan_lbvh"};
    inline constexpr std::string_view kOutlierAnalysisBackendNames[]{"CPU octree", "CPU LBVH (cached)", "Vulkan LBVH"};
    inline constexpr std::string_view kOutlierAnalysisRangeRefusal{
        "k must be positive and GPU query batch size must be 1..16384."};
    inline constexpr double kOutlierAnalysisFloatMax = double(std::numeric_limits<float>::max());
    // The two floats other than radius may store as zero (the hand-written codec never refused them).
    constexpr bool OutlierAnalysisMayStoreZero(const OutlierAnalysisConfig&) noexcept
    {
        return false;
    }
    inline constexpr std::array kOutlierAnalysisRows{
        Bind<&OutlierAnalysisConfig::StableEntityId>(
            {.Name = "entity", .Type = ConfigFieldType::UInt, .Description = "Entity."}),
        Bind<&OutlierAnalysisConfig::KNeighbors>({.Name = "k_neighbors",
            .Type = ConfigFieldType::UInt,
            .Description = "K neighbors.",
            .Min = 1,
            .Refusal = kOutlierAnalysisRangeRefusal}),
        Bind<&OutlierAnalysisConfig::MinimumNeighbors>(
            {.Name = "minimum_neighbors", .Type = ConfigFieldType::UInt, .Description = "Minimum neighbors."}),
        Bind<&OutlierAnalysisConfig::GpuQueryBatchSize>({.Name = "gpu_query_batch_size",
            .Type = ConfigFieldType::UInt,
            .Description = "Gpu query batch size.",
            .Min = 1,
            .Max = 16384,
            .Refusal = kOutlierAnalysisRangeRefusal}),
        Bind<&OutlierAnalysisConfig::Method>({.Name = "method",
            .Type = ConfigFieldType::StringEnum,
            .Description = "Method.",
            .EnumValues = kOutlierAnalysisMethod,
            .Refusal = "Unknown outlier method."}),
        Bind<&OutlierAnalysisConfig::Operation>({.Name = "operation",
            .Type = ConfigFieldType::StringEnum,
            .Description = "Operation.",
            .EnumValues = kOutlierAnalysisOperation,
            .Refusal = "Unknown outlier operation."}),
        Bind<&OutlierAnalysisConfig::Backend>({.Name = "backend",
            .Type = ConfigFieldType::StringEnum,
            .Description = "Backend.",
            .EnumNames = kOutlierAnalysisBackendNames,
            .EnumValues = kOutlierAnalysisBackend,
            .Refusal = "Unknown outlier backend."}),
        Bind<&OutlierAnalysisConfig::Radius>(
            {.Name = "radius",
                .Type = ConfigFieldType::Float,
                .Description =
                    "With method radius, the value must be strictly positive after conversion to float; otherwise zero is allowed.",
                .Min = 0,
                .Max = kOutlierAnalysisFloatMax,
                .Refusal = "Radius must remain positive in float storage."},
            [](const OutlierAnalysisConfig& c)
            {
                return c.Method == OutlierAnalysisMethod::Radius;
            },
            "Radius must be positive."),
        Bind<&OutlierAnalysisConfig::StdDevMultiplier>({.Name = "stddev_multiplier",
                                                           .Type = ConfigFieldType::Float,
                                                           .Description = "Stddev multiplier.",
                                                           .Min = 0,
                                                           .Max = kOutlierAnalysisFloatMax},
            OutlierAnalysisMayStoreZero),
        Bind<&OutlierAnalysisConfig::ScoreThreshold>({.Name = "score_threshold",
                                                         .Type = ConfigFieldType::Float,
                                                         .Description = "Score threshold.",
                                                         .Min = 0,
                                                         .Max = kOutlierAnalysisFloatMax},
            OutlierAnalysisMayStoreZero),
        Bind<&OutlierAnalysisConfig::Negative>({.Name = "negative",
            .Type = ConfigFieldType::Bool,
            .Description = "Mark the inliers instead of the outliers (PCL's negative), so a removal keeps only the "
                           "outliers. CPU backends only."}),
        Bind<&OutlierAnalysisConfig::Positions>({.Name = "positions",
            .Type = ConfigFieldType::PropertyRef,
            .Description = "Positions, mask and score must have distinct names on the same element domain.",
            .RefKinds = kOutlierAnalysisVec3Kind}),
        Bind<&OutlierAnalysisConfig::Mask>({.Name = "mask",
            .Type = ConfigFieldType::PropertyRef,
            .Description = "Mask.",
            .RefKinds = kOutlierAnalysisUInt32Kind,
            .AnyScalar = true}),
        Bind<&OutlierAnalysisConfig::Score>({.Name = "score",
            .Type = ConfigFieldType::PropertyRef,
            .Description = "Score.",
            .RefKinds = kOutlierAnalysisFloatKind,
            .AnyScalar = true}),
    };
    inline constexpr auto kOutlierAnalysisFields = SpecsOf(kOutlierAnalysisRows);
    inline constexpr DerivedConfigSection<OutlierAnalysisConfig> kOutlierAnalysisSection{
        .Name = kOutlierAnalysisConfigSectionName,
        .SchemaId = kOutlierAnalysisConfigSectionSchemaId,
        .Title = "OutlierAnalysis",
        .Rows = kOutlierAnalysisRows,
        .Fields = kOutlierAnalysisFields,
        .ObjectError = "Outlier analysis config must be an object.",
        .UnknownFieldPrefix = "Unknown outlier field: ",
        .Check = [](const OutlierAnalysisConfig& c) -> std::string_view
        {
            return c.Positions.Domain != c.Mask.Domain || c.Mask.Domain != c.Score.Domain ||
                    c.Positions.Name == c.Mask.Name || c.Positions.Name == c.Score.Name || c.Mask.Name == c.Score.Name
                ? "Position, mask and score must be distinct properties on the same domain."
                : "";
        }};
    // The section's codec is instantiated once, in the operation's implementation unit.
    extern template std::string SerializeDerivedSection<kOutlierAnalysisSection>(const OutlierAnalysisConfig&);
    extern template Core::Config::EngineConfigSectionValidationResult ValidateDerivedSection<kOutlierAnalysisSection>(
        std::string_view, std::string_view, std::string_view);
    extern template std::optional<OutlierAnalysisConfig> GetDerivedSection<kOutlierAnalysisSection>(
        const Core::Config::EngineConfig&);
    extern template void SetDerivedSection<kOutlierAnalysisSection>(
        Core::Config::EngineConfig&, const OutlierAnalysisConfig&);
    extern template Core::Config::EngineConfigSectionRegistration
    MakeDerivedSectionRegistration<kOutlierAnalysisSection>();
}
