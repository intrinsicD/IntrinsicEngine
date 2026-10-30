// The density weight config codec (kernel density, point spacing and outlier analysis derive theirs
// from their declared rows: RUNTIME-349, RUNTIME-354).
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

import Extrinsic.Runtime.DensityWeightConfig;

#include "Config/internal/Runtime.PointConfigJson.hpp"
#include "Config/internal/Runtime.ConfigFieldJson.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr Geometry::PropertyValueKind kFloatKind[]{Geometry::PropertyValueKind::Float};
        constexpr Geometry::PropertyValueKind kVec3Kind[]{Geometry::PropertyValueKind::Vec3};
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
