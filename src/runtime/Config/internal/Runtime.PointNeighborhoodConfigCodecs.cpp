// Compiles OutlierAnalysis, KernelDensity, PointSpacing, DensityWeight config codecs together to share JSON work.
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

import Extrinsic.Runtime.ConfigFieldSpec;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;

import Extrinsic.Runtime.OutlierAnalysisConfig;
import Extrinsic.Runtime.KernelDensityConfig;
import Extrinsic.Runtime.PointSpacingConfig;
import Extrinsic.Runtime.DensityWeightConfig;

#include "Config/internal/Runtime.PointConfigJson.hpp"
#include "Config/internal/Runtime.ConfigFieldJson.hpp"
#include "Config/internal/Runtime.DerivedConfigSection.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr Geometry::PropertyValueKind kFloatKind[]{Geometry::PropertyValueKind::Float};
        constexpr Geometry::PropertyValueKind kUInt32Kind[]{Geometry::PropertyValueKind::UInt32};
        constexpr Geometry::PropertyValueKind kVec3Kind[]{Geometry::PropertyValueKind::Vec3};
        constexpr std::string_view kOutlierAnalysisMethod[]{"statistical", "radius", "local_distance_ratio"};
        constexpr std::string_view kOutlierAnalysisOperation[]{"analyze", "remove_marked"};
        constexpr std::string_view kOutlierAnalysisBackend[]{"cpu_octree", "cpu_lbvh", "vulkan_lbvh"};
        constexpr std::string_view kOutlierAnalysisBackendNames[]{"CPU octree", "CPU LBVH (cached)", "Vulkan LBVH"};
        constexpr ConfigFieldSpec kOutlierAnalysisFields[]{
            {.Name = "entity", .Type = ConfigFieldType::UInt, .Description = "Entity."},
            {.Name = "k_neighbors", .Type = ConfigFieldType::UInt, .Description = "K neighbors.", .Min = 1},
            {.Name = "minimum_neighbors", .Type = ConfigFieldType::UInt, .Description = "Minimum neighbors."},
            {.Name = "gpu_query_batch_size",
                .Type = ConfigFieldType::UInt,
                .Description = "Gpu query batch size.",
                .Min = 1,
                .Max = 16384},
            {.Name = "method",
                .Type = ConfigFieldType::StringEnum,
                .Description = "Method.",
                .EnumValues = kOutlierAnalysisMethod},
            {.Name = "operation",
                .Type = ConfigFieldType::StringEnum,
                .Description = "Operation.",
                .EnumValues = kOutlierAnalysisOperation},
            {.Name = "backend",
                .Type = ConfigFieldType::StringEnum,
                .Description = "Backend.",
                .EnumNames = kOutlierAnalysisBackendNames,
                .EnumValues = kOutlierAnalysisBackend},
            {.Name = "radius",
                .Type = ConfigFieldType::Float,
                .Description =
                    "With method radius, the value must be strictly positive after conversion to float; otherwise zero is allowed.",
                .Min = 0,
                .Max = double(std::numeric_limits<float>::max())},
            {.Name = "stddev_multiplier",
                .Type = ConfigFieldType::Float,
                .Description = "Stddev multiplier.",
                .Min = 0,
                .Max = double(std::numeric_limits<float>::max())},
            {.Name = "score_threshold",
                .Type = ConfigFieldType::Float,
                .Description = "Score threshold.",
                .Min = 0,
                .Max = double(std::numeric_limits<float>::max())},
            {.Name = "positions",
                .Type = ConfigFieldType::PropertyRef,
                .Description = "Positions, mask and score must have distinct names on the same element domain.",
                .RefKinds = kVec3Kind},
            {.Name = "mask",
                .Type = ConfigFieldType::PropertyRef,
                .Description = "Mask.",
                .RefKinds = kUInt32Kind,
                .AnyScalar = true},
            {.Name = "score",
                .Type = ConfigFieldType::PropertyRef,
                .Description = "Score.",
                .RefKinds = kFloatKind,
                .AnyScalar = true},
        };
        constexpr std::string_view kKernelDensityBackend[]{"cpu_octree", "cpu_lbvh", "vulkan_lbvh"};
        constexpr std::string_view kKernelDensityBackendNames[]{"CPU octree", "CPU LBVH (cached)", "Vulkan LBVH"};
        constexpr std::array kKernelDensityRows{
            Bind<&KernelDensityConfig::StableEntityId>(
                {.Name = "entity", .Type = ConfigFieldType::UInt, .Description = "Entity."}),
            Bind<&KernelDensityConfig::KNeighbors>(
                {.Name = "k_neighbors", .Type = ConfigFieldType::UInt, .Description = "K neighbors."}),
            Bind<&KernelDensityConfig::GpuQueryBatchSize>({.Name = "gpu_query_batch_size",
                .Type = ConfigFieldType::UInt,
                .Description = "Gpu query batch size.",
                .Min = 1,
                .Max = 16384,
                .Refusal = "GPU query batch size must be 1..16384."}),
            Bind<&KernelDensityConfig::Backend>({.Name = "backend",
                .Type = ConfigFieldType::StringEnum,
                .Description = "Backend.",
                .EnumNames = kKernelDensityBackendNames,
                .EnumValues = kKernelDensityBackend,
                .Refusal = "Unknown density backend."}),
            Bind<&KernelDensityConfig::Bandwidth>({.Name = "bandwidth",
                .Type = ConfigFieldType::Float,
                .Description =
                    "Zero selects automatic bandwidth; positive values must remain positive in float storage.",
                .Min = 0,
                .Max = double(std::numeric_limits<float>::max()),
                .Refusal = "Positive bandwidth must remain positive in float storage."}),
            Bind<&KernelDensityConfig::Positions>({.Name = "positions",
                .Type = ConfigFieldType::PropertyRef,
                .Description = "Position and density bindings must have distinct names on the same element domain.",
                .RefKinds = kVec3Kind}),
            Bind<&KernelDensityConfig::Density>({.Name = "density",
                .Type = ConfigFieldType::PropertyRef,
                .Description = "Density.",
                .RefKinds = kFloatKind,
                .AnyScalar = true}),
        };
        constexpr auto kKernelDensityFields = SpecsOf(kKernelDensityRows);
        constexpr std::string_view kPointSpacingBackend[]{"cpu_octree", "cpu_lbvh", "vulkan_lbvh"};
        constexpr std::string_view kPointSpacingBackendNames[]{"CPU octree", "CPU LBVH (cached)", "Vulkan LBVH"};
        constexpr std::array kPointSpacingRows{
            Bind<&PointSpacingConfig::StableEntityId>(
                {.Name = "entity", .Type = ConfigFieldType::UInt, .Description = "Entity."}),
            Bind<&PointSpacingConfig::KNeighbors>(
                {.Name = "k_neighbors", .Type = ConfigFieldType::UInt, .Description = "K neighbors."}),
            Bind<&PointSpacingConfig::GpuQueryBatchSize>({.Name = "gpu_query_batch_size",
                .Type = ConfigFieldType::UInt,
                .Description = "Gpu query batch size.",
                .Min = 1,
                .Max = 16384,
                .Refusal = "GPU query batch size must be 1..16384."}),
            Bind<&PointSpacingConfig::Backend>({.Name = "backend",
                .Type = ConfigFieldType::StringEnum,
                .Description = "Backend.",
                .EnumNames = kPointSpacingBackendNames,
                .EnumValues = kPointSpacingBackend,
                .Refusal = "Unknown radii backend."}),
            Bind<&PointSpacingConfig::ScaleFactor>({.Name = "scale_factor",
                .Type = ConfigFieldType::Float,
                .Description =
                    "Nonnegative radius scale factor; positive values must remain positive in float storage.",
                .Min = 0,
                .Max = double(std::numeric_limits<float>::max()),
                .Refusal = "Positive scale factor must remain positive in float storage."}),
            Bind<&PointSpacingConfig::Positions>({.Name = "positions",
                .Type = ConfigFieldType::PropertyRef,
                .Description = "Position and radii bindings must have distinct names on the same element domain.",
                .RefKinds = kVec3Kind}),
            Bind<&PointSpacingConfig::Radii>({.Name = "radii",
                .Type = ConfigFieldType::PropertyRef,
                .Description = "Radii.",
                .RefKinds = kFloatKind,
                .AnyScalar = true}),
        };
        constexpr auto kPointSpacingFields = SpecsOf(kPointSpacingRows);
        constexpr std::string_view kDensityWeightBackend[]{"cpu_kdtree", "cpu_lbvh", "vulkan_lbvh"};
        constexpr std::string_view kDensityWeightKernel[]{"gaussian", "theta_lop", "wendland_c2"};
        constexpr std::string_view kDensityWeightMode[]{"direct", "reciprocal"};
        constexpr std::string_view kDensityWeightBackendNames[]{"CPU KD-tree", "CPU LBVH (cached)", "Vulkan LBVH"};
        constexpr ConfigFieldSpec kDensityWeightFields[]{
            {.Name = "entity", .Type = ConfigFieldType::UInt, .Description = "Entity."},
            {.Name = "gpu_query_batch_size",
                .Type = ConfigFieldType::UInt,
                .Description = "Gpu query batch size.",
                .Min = 1,
                .Max = 16384},
            {.Name = "gpu_radius_capacity",
                .Type = ConfigFieldType::UInt,
                .Description = "Gpu radius capacity.",
                .Min = 1,
                .Max = 1024},
            {.Name = "backend",
                .Type = ConfigFieldType::StringEnum,
                .Description = "Backend.",
                .EnumNames = kDensityWeightBackendNames,
                .EnumValues = kDensityWeightBackend},
            {.Name = "kernel",
                .Type = ConfigFieldType::StringEnum,
                .Description = "Kernel.",
                .EnumValues = kDensityWeightKernel},
            {.Name = "mode",
                .Type = ConfigFieldType::StringEnum,
                .Description = "Mode.",
                .EnumValues = kDensityWeightMode},
            {.Name = "support_radius",
                .Type = ConfigFieldType::Float,
                .Description = "Support radius; 0 derives it from the selected positions (support-radius policy), "
                               "positive values are source units.",
                .Min = 0,
                .Max = double(std::numeric_limits<float>::max())},
            {.Name = "positions",
                .Type = ConfigFieldType::PropertyRef,
                .Description = "Position and weight bindings must have distinct names on the same element domain.",
                .RefKinds = kVec3Kind},
            {.Name = "weights",
                .Type = ConfigFieldType::PropertyRef,
                .Description = "Weights.",
                .RefKinds = kFloatKind,
                .AnyScalar = true},
        };

        using Json = nlohmann::json;
        OutlierAnalysisConfig ParseOutlierAnalysisConfig(const Json& data)
        {
            OutlierAnalysisConfig c;
            c.StableEntityId = data.at("entity");
            for (unsigned i = 0; i < 3; ++i)
                if (data.at("method") == ToString(OutlierAnalysisMethod(i)))
                    c.Method = OutlierAnalysisMethod(i);
            c.Operation = data.at("operation") == "remove_marked" ? OutlierAnalysisOperation::RemoveMarked
                                                                  : OutlierAnalysisOperation::Analyze;
            for (unsigned i = 0; i < 3; ++i)
                if (data.at("backend") == ToString(OutlierAnalysisBackend(i)))
                    c.Backend = OutlierAnalysisBackend(i);
            ConfigDetail::DecodePointPropertyRef(data.at("positions"), c.Positions);
            ConfigDetail::DecodePointPropertyRef(data.at("mask"), c.Mask);
            ConfigDetail::DecodePointPropertyRef(data.at("score"), c.Score);
            c.KNeighbors = data.at("k_neighbors");
            c.MinimumNeighbors = data.at("minimum_neighbors");
            c.GpuQueryBatchSize = data.at("gpu_query_batch_size");
            c.Radius = data.at("radius");
            c.StdDevMultiplier = data.at("stddev_multiplier");
            c.ScoreThreshold = data.at("score_threshold");
            return c;
        }
    }
    const char* ToString(OutlierAnalysisMethod m) noexcept
    {
        switch (m)
        {
        case OutlierAnalysisMethod::Statistical:        return "statistical";
        case OutlierAnalysisMethod::Radius:             return "radius";
        case OutlierAnalysisMethod::LocalDistanceRatio: return "local_distance_ratio";
        }
        return "invalid";
    }
    const char* ToString(OutlierAnalysisBackend b) noexcept
    {
        switch (b)
        {
        case OutlierAnalysisBackend::CpuOctree:  return "cpu_octree";
        case OutlierAnalysisBackend::CpuLBVH:    return "cpu_lbvh";
        case OutlierAnalysisBackend::VulkanLBVH: return "vulkan_lbvh";
        }
        return "invalid";
    }
    const char* ToString(OutlierAnalysisOperation o) noexcept
    {
        switch (o)
        {
        case OutlierAnalysisOperation::Analyze:      return "analyze";
        case OutlierAnalysisOperation::RemoveMarked: return "remove_marked";
        }
        return "invalid";
    }
    std::string SerializeOutlierAnalysisConfig(const OutlierAnalysisConfig& c)
    {
        return ConfigDetail::SerializeConfigJson(Json{{"entity", c.StableEntityId},
            {"method", ToString(c.Method)},
            {"backend", ToString(c.Backend)},
            {"operation", ToString(c.Operation)},
            {"positions", ConfigDetail::EncodePointPropertyRef(c.Positions)},
            {"mask", ConfigDetail::EncodePointPropertyRef(c.Mask)},
            {"score", ConfigDetail::EncodePointPropertyRef(c.Score)},
            {"k_neighbors", c.KNeighbors},
            {"minimum_neighbors", c.MinimumNeighbors},
            {"gpu_query_batch_size", c.GpuQueryBatchSize},
            {"radius", c.Radius},
            {"stddev_multiplier", c.StdDevMultiplier},
            {"score_threshold", c.ScoreThreshold}});
    }
    Core::Config::EngineConfigSectionValidationResult ValidateOutlierAnalysisConfigSection(
        std::string_view payload, std::string_view, std::string_view subject)
    {
        using namespace Core::Config;
        using ConfigDetail::RejectConfigSection;
        auto input = ConfigDetail::ParseConfigJson(payload, false),
             data = ConfigDetail::ParseConfigJson(SerializeOutlierAnalysisConfig({}), true);
        if (auto error = ConfigDetail::ValidatePointConfigFields(input,
                data,
                "Outlier analysis config must be an object.",
                "Unknown outlier field: ",
                kOutlierAnalysisFields))
            return RejectConfigSection(subject, std::move(*error));
        if (!ConfigDetail::InConfigFieldRange(
                kOutlierAnalysisFields, "k_neighbors", data["k_neighbors"].get<double>()) ||
            !ConfigDetail::InConfigFieldRange(
                kOutlierAnalysisFields, "gpu_query_batch_size", data["gpu_query_batch_size"].get<double>()))
            return RejectConfigSection(subject, "k must be positive and GPU query batch size must be 1..16384.");
        if (!ConfigDetail::IsConfigFieldToken(kOutlierAnalysisFields, "method", data["method"]))
            return RejectConfigSection(subject, "Unknown outlier method.");
        if (!ConfigDetail::IsConfigFieldToken(kOutlierAnalysisFields, "operation", data["operation"]))
            return RejectConfigSection(subject, "Unknown outlier operation.");
        if (!ConfigDetail::IsConfigFieldToken(kOutlierAnalysisFields, "backend", data["backend"]))
            return RejectConfigSection(subject, "Unknown outlier backend.");
        if (auto error = ConfigDetail::ValidatePointConfigNonnegativeFloats(data, kOutlierAnalysisFields))
            return RejectConfigSection(subject, std::move(*error));
        if (data["method"] == "radius" && data["radius"].get<float>() <= 0)
            return RejectConfigSection(subject,
                data["radius"].get<double>() <= 0.0 ? "Radius must be positive."
                                                    : "Radius must remain positive in float storage.");
        if (auto error = ConfigDetail::ValidatePointConfigPropertyRefs(data, kOutlierAnalysisFields))
            return RejectConfigSection(subject, std::move(*error));
        if (data["positions"]["domain"] != data["mask"]["domain"] ||
            data["mask"]["domain"] != data["score"]["domain"] || data["positions"]["name"] == data["mask"]["name"] ||
            data["positions"]["name"] == data["score"]["name"] || data["mask"]["name"] == data["score"]["name"])
            return RejectConfigSection(
                subject, "Position, mask and score must be distinct properties on the same domain.");
        return ConfigDetail::AcceptConfigSection(
            SerializeOutlierAnalysisConfig(ParseOutlierAnalysisConfig(data)), input.size());
    }
    std::optional<OutlierAnalysisConfig> GetOutlierAnalysisConfig(const Core::Config::EngineConfig& c)
    {
        return ConfigDetail::GetPointConfig(c,
            kOutlierAnalysisConfigSectionName,
            kOutlierAnalysisConfigSectionSchemaId,
            1u,
            ValidateOutlierAnalysisConfigSection,
            ParseOutlierAnalysisConfig);
    }
    void SetOutlierAnalysisConfig(Core::Config::EngineConfig& c, const OutlierAnalysisConfig& value)
    {
        Core::Config::UpsertEngineConfigSection(c.AppSections,
            ConfigDetail::MakeConfigSection(kOutlierAnalysisConfigSectionName,
                kOutlierAnalysisConfigSectionSchemaId,
                1,
                SerializeOutlierAnalysisConfig(value)));
    }
    std::span<const ConfigFieldSpec> OutlierAnalysisConfigFieldSpecs() noexcept
    {
        return kOutlierAnalysisFields;
    }
    Core::Config::EngineConfigSectionRegistration MakeOutlierAnalysisConfigSectionRegistration()
    {
        return ConfigDetail::MakeSectionRegistration(kOutlierAnalysisConfigSectionName,
            kOutlierAnalysisConfigSectionSchemaId,
            1,
            SerializeOutlierAnalysisConfig({}),
            ValidateOutlierAnalysisConfigSection,
            ConfigDetail::BuildSectionSchemaJson(kOutlierAnalysisConfigSectionSchemaId,
                "OutlierAnalysis",
                "Validated OutlierAnalysis configuration.",
                kOutlierAnalysisFields,
                ConfigDetail::ParseConfigJson(SerializeOutlierAnalysisConfig({}), true)));
    }
}

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr ConfigDetail::DerivedConfigSection<KernelDensityConfig> kKernelDensitySection{
            .Name = kKernelDensityConfigSectionName,
            .SchemaId = kKernelDensityConfigSectionSchemaId,
            .Title = "KernelDensity",
            .Rows = kKernelDensityRows,
            .Fields = kKernelDensityFields,
            .ObjectError = "Kernel density config must be an object.",
            .UnknownFieldPrefix = "Unknown density field: ",
            .Check = [](const KernelDensityConfig& c) -> std::string_view
            {
                return c.Positions.Domain != c.Density.Domain || c.Positions.Name == c.Density.Name
                    ? "Position and density must be distinct properties on the same domain."
                    : "";
            },
            .Validate = &ValidateKernelDensityConfigSection};
        static_assert(ConfigDetail::IsDerivable(kKernelDensitySection));
    }
    // The row's tokens, so the codec and the owner's names cannot drift apart.
    const char* ToString(KernelDensityBackend b) noexcept
    {
        const auto i = std::to_underlying(b);
        return i < std::size(kKernelDensityBackend) ? kKernelDensityBackend[i].data() : "invalid";
    }
    std::string SerializeKernelDensityConfig(const KernelDensityConfig& c)
    {
        return ConfigDetail::SerializeDerivedConfig(kKernelDensitySection, c);
    }
    Core::Config::EngineConfigSectionValidationResult ValidateKernelDensityConfigSection(
        std::string_view payload, std::string_view, std::string_view subject)
    {
        return ConfigDetail::ValidateDerivedConfig(kKernelDensitySection, payload, subject);
    }
    std::optional<KernelDensityConfig> GetKernelDensityConfig(const Core::Config::EngineConfig& c)
    {
        return ConfigDetail::GetDerivedConfig(kKernelDensitySection, c);
    }
    void SetKernelDensityConfig(Core::Config::EngineConfig& c, const KernelDensityConfig& value)
    {
        ConfigDetail::SetDerivedConfig(kKernelDensitySection, c, value);
    }
    std::span<const ConfigFieldSpec> KernelDensityConfigFieldSpecs() noexcept
    {
        return kKernelDensityFields;
    }
    Core::Config::EngineConfigSectionRegistration MakeKernelDensityConfigSectionRegistration()
    {
        return ConfigDetail::MakeDerivedRegistration(kKernelDensitySection);
    }
}

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr ConfigDetail::DerivedConfigSection<PointSpacingConfig> kPointSpacingSection{
            .Name = kPointSpacingConfigSectionName,
            .SchemaId = kPointSpacingConfigSectionSchemaId,
            .Title = "PointSpacing",
            .Rows = kPointSpacingRows,
            .Fields = kPointSpacingFields,
            .ObjectError = "Point spacing config must be an object.",
            .UnknownFieldPrefix = "Unknown radii field: ",
            .Check = [](const PointSpacingConfig& c) -> std::string_view
            {
                return c.Positions.Domain != c.Radii.Domain || c.Positions.Name == c.Radii.Name
                    ? "Position and radii must be distinct properties on the same domain."
                    : "";
            },
            .Validate = &ValidatePointSpacingConfigSection};
        static_assert(ConfigDetail::IsDerivable(kPointSpacingSection));
    }
    // The row's tokens, so the codec and the owner's names cannot drift apart.
    const char* ToString(PointSpacingBackend b) noexcept
    {
        const auto i = std::to_underlying(b);
        return i < std::size(kPointSpacingBackend) ? kPointSpacingBackend[i].data() : "invalid";
    }
    std::string SerializePointSpacingConfig(const PointSpacingConfig& c)
    {
        return ConfigDetail::SerializeDerivedConfig(kPointSpacingSection, c);
    }
    Core::Config::EngineConfigSectionValidationResult ValidatePointSpacingConfigSection(
        std::string_view payload, std::string_view, std::string_view subject)
    {
        return ConfigDetail::ValidateDerivedConfig(kPointSpacingSection, payload, subject);
    }
    std::optional<PointSpacingConfig> GetPointSpacingConfig(const Core::Config::EngineConfig& c)
    {
        return ConfigDetail::GetDerivedConfig(kPointSpacingSection, c);
    }
    void SetPointSpacingConfig(Core::Config::EngineConfig& c, const PointSpacingConfig& value)
    {
        ConfigDetail::SetDerivedConfig(kPointSpacingSection, c, value);
    }
    std::span<const ConfigFieldSpec> PointSpacingConfigFieldSpecs() noexcept
    {
        return kPointSpacingFields;
    }
    Core::Config::EngineConfigSectionRegistration MakePointSpacingConfigSectionRegistration()
    {
        return ConfigDetail::MakeDerivedRegistration(kPointSpacingSection);
    }
}

namespace Extrinsic::Runtime
{
    namespace
    {
        using Json = nlohmann::json;
        DensityWeightConfig ParseDensityWeightConfig(const Json& data)
        {
            DensityWeightConfig c;
            c.StableEntityId = data.at("entity");
            for (unsigned i = 0; i < 3; ++i)
                if (data.at("backend") == ToString(DensityWeightBackend(i)))
                    c.Backend = DensityWeightBackend(i);
            ConfigDetail::DecodePointPropertyRef(data.at("positions"), c.Positions);
            ConfigDetail::DecodePointPropertyRef(data.at("weights"), c.Weights);
            c.SupportRadius = data.at("support_radius");
            c.GpuQueryBatchSize = data.at("gpu_query_batch_size");
            c.GpuRadiusCapacity = data.at("gpu_radius_capacity");
            for (unsigned i = 0; i < 3; ++i)
                if (data.at("kernel") ==
                    Geometry::PointCloud::Kernels::DebugName(Geometry::PointCloud::Kernels::KernelType(i)))
                    c.Kernel = Geometry::PointCloud::Kernels::KernelType(i);
            for (unsigned i = 0; i < 2; ++i)
                if (data.at("mode") ==
                    Geometry::PointCloud::Kernels::DebugName(Geometry::PointCloud::Kernels::DensityWeightMode(i)))
                    c.Mode = Geometry::PointCloud::Kernels::DensityWeightMode(i);
            return c;
        }
    }

    const char* ToString(DensityWeightBackend b) noexcept
    {
        switch (b)
        {
        case DensityWeightBackend::CpuKDTree:  return "cpu_kdtree";
        case DensityWeightBackend::CpuLBVH:    return "cpu_lbvh";
        case DensityWeightBackend::VulkanLBVH: return "vulkan_lbvh";
        }
        return "invalid";
    }

    std::string SerializeDensityWeightConfig(const DensityWeightConfig& c)
    {
        return ConfigDetail::SerializeConfigJson(Json{{"entity", c.StableEntityId},
            {"backend", ToString(c.Backend)},
            {"positions", ConfigDetail::EncodePointPropertyRef(c.Positions)},
            {"weights", ConfigDetail::EncodePointPropertyRef(c.Weights)},
            {"support_radius", c.SupportRadius},
            {"kernel", Geometry::PointCloud::Kernels::DebugName(c.Kernel)},
            {"mode", Geometry::PointCloud::Kernels::DebugName(c.Mode)},
            {"gpu_query_batch_size", c.GpuQueryBatchSize},
            {"gpu_radius_capacity", c.GpuRadiusCapacity}});
    }
    Core::Config::EngineConfigSectionValidationResult ValidateDensityWeightConfigSection(
        std::string_view payload, std::string_view, std::string_view subject)
    {
        using namespace Core::Config;
        using ConfigDetail::RejectConfigSection;
        auto input = ConfigDetail::ParseConfigJson(payload, false),
             data = ConfigDetail::ParseConfigJson(SerializeDensityWeightConfig({}), true);
        if (auto error = ConfigDetail::ValidatePointConfigFields(input,
                data,
                "Density weight config must be an object.",
                "Unknown density weight field: ",
                kDensityWeightFields))
            return RejectConfigSection(subject, std::move(*error));
        if (!ConfigDetail::InConfigFieldRange(
                kDensityWeightFields, "gpu_query_batch_size", data["gpu_query_batch_size"].get<double>()) ||
            !ConfigDetail::InConfigFieldRange(
                kDensityWeightFields, "gpu_radius_capacity", data["gpu_radius_capacity"].get<double>()))
            return RejectConfigSection(
                subject, "GPU query batch must be 1..16384 and complete radius capacity 1..1024.");
        if (!ConfigDetail::IsConfigFieldToken(kDensityWeightFields, "backend", data["backend"]))
            return RejectConfigSection(subject, "Unknown density weight backend.");
        if (!data["support_radius"].is_number() ||
            !ConfigDetail::InConfigFieldRange(
                kDensityWeightFields, "support_radius", data["support_radius"].get<double>()))
            return RejectConfigSection(
                subject, "Support radius must be 0 (automatic) or positive and finite, at most floatmax.");
        if (!ConfigDetail::IsConfigFieldToken(kDensityWeightFields, "kernel", data["kernel"]))
            return RejectConfigSection(subject, "Unknown radial kernel.");
        if (!ConfigDetail::IsConfigFieldToken(kDensityWeightFields, "mode", data["mode"]))
            return RejectConfigSection(subject, "Unknown density weight mode.");
        using ConfigDetail::PointPropertyValidation;
        if (ConfigDetail::ValidatePointPropertyRef(data["positions"],
                FindConfigFieldSpec(kDensityWeightFields, "positions")->RefKinds.front(),
                FindConfigFieldSpec(kDensityWeightFields, "positions")->AnyScalar) != PointPropertyValidation::Valid ||
            ConfigDetail::ValidatePointPropertyRef(data["weights"],
                FindConfigFieldSpec(kDensityWeightFields, "weights")->RefKinds.front(),
                FindConfigFieldSpec(kDensityWeightFields, "weights")->AnyScalar) != PointPropertyValidation::Valid ||
            data["positions"]["domain"] != data["weights"]["domain"] ||
            data["positions"]["name"] == data["weights"]["name"])
            return RejectConfigSection(subject,
                "Position and weight bindings need distinct canonical vec3/float properties on the same domain.");
        return ConfigDetail::AcceptConfigSection(
            SerializeDensityWeightConfig(ParseDensityWeightConfig(data)), input.size());
    }
    std::optional<DensityWeightConfig> GetDensityWeightConfig(const Core::Config::EngineConfig& c)
    {
        return ConfigDetail::GetPointConfig(c,
            kDensityWeightConfigSectionName,
            kDensityWeightConfigSectionSchemaId,
            1u,
            ValidateDensityWeightConfigSection,
            ParseDensityWeightConfig);
    }
    void SetDensityWeightConfig(Core::Config::EngineConfig& c, const DensityWeightConfig& value)
    {
        Core::Config::UpsertEngineConfigSection(c.AppSections,
            ConfigDetail::MakeConfigSection(kDensityWeightConfigSectionName,
                kDensityWeightConfigSectionSchemaId,
                1,
                SerializeDensityWeightConfig(value)));
    }
    std::span<const ConfigFieldSpec> DensityWeightConfigFieldSpecs() noexcept
    {
        return kDensityWeightFields;
    }
    Core::Config::EngineConfigSectionRegistration MakeDensityWeightConfigSectionRegistration()
    {
        return ConfigDetail::MakeSectionRegistration(kDensityWeightConfigSectionName,
            kDensityWeightConfigSectionSchemaId,
            1,
            SerializeDensityWeightConfig({}),
            ValidateDensityWeightConfigSection,
            ConfigDetail::BuildSectionSchemaJson(kDensityWeightConfigSectionSchemaId,
                "DensityWeight",
                "Validated DensityWeight configuration.",
                kDensityWeightFields,
                ConfigDetail::ParseConfigJson(SerializeDensityWeightConfig({}), true)));
    }
}
