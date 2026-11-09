// Compiles KeypointAnalysis and PointConstruction config codecs together to share JSON work.
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
import Extrinsic.Runtime.StepRunExecution;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;

import Extrinsic.Runtime.KeypointAnalysisConfig;
import Extrinsic.Runtime.PointConstructionConfig;

#include "Config/internal/Runtime.PointConfigJson.hpp"
#include "Config/internal/Runtime.ConfigFieldJson.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr Geometry::PropertyValueKind kFloatKind[]{Geometry::PropertyValueKind::Float};
        constexpr Geometry::PropertyValueKind kUInt32Kind[]{Geometry::PropertyValueKind::UInt32};
        constexpr Geometry::PropertyValueKind kVec3Kind[]{Geometry::PropertyValueKind::Vec3};
        constexpr std::string_view kKeypointAnalysisBackend[]{"cpu_kdtree", "cpu_lbvh", "vulkan_lbvh", "vulkan_compute"};
        constexpr std::string_view kKeypointAnalysisBackendNames[]{"CPU KD-tree", "CPU LBVH (cached)", "Vulkan LBVH neighborhoods", "Vulkan"};
        // ARCH-023: the shared rows a one-shot honours (auto_accept, start), keypoints' and construction's.
        constexpr auto kOneShotExecutionFields = StepRunExecutionFields(1u);
        constexpr ConfigFieldSpec kKeypointAnalysisFields[]{
            {.Name = "entity", .Type = ConfigFieldType::UInt, .Description = "Entity."},
            {.Name = "minimum_neighbors", .Type = ConfigFieldType::UInt, .Description = "Minimum neighbors."},
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
                .EnumNames = kKeypointAnalysisBackendNames,
                .EnumValues = kKeypointAnalysisBackend},
            {.Name = "salient_radius",
                .Type = ConfigFieldType::Float,
                .Description =
                    "Zero selects an automatic radius; positive values must remain positive in float storage.",
                .Min = 0,
                .Max = double(std::numeric_limits<float>::max())},
            {.Name = "nonmax_radius",
                .Type = ConfigFieldType::Float,
                .Description =
                    "Zero selects an automatic radius; positive values must remain positive in float storage.",
                .Min = 0,
                .Max = double(std::numeric_limits<float>::max())},
            {.Name = "gamma21", .Type = ConfigFieldType::Float, .Description = "Gamma21.", .Min = 0, .Max = 1},
            {.Name = "gamma32", .Type = ConfigFieldType::Float, .Description = "Gamma32.", .Min = 0, .Max = 1},
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
            kOneShotExecutionFields[3],
            kOneShotExecutionFields[4],
        };
        constexpr std::string_view kPointConstructionBackend[]{"cpu_reference", "cpu_lbvh", "vulkan_lbvh"};
        constexpr std::string_view kPointConstructionMethod[]{"hoppe", "knn_graph"};
        constexpr std::string_view kPointConstructionBackendNames[]{"CPU reference", "CPU LBVH (cached)", "Vulkan LBVH"};
        constexpr ConfigFieldSpec kPointConstructionFields[]{
            {.Name = "entity", .Type = ConfigFieldType::UInt, .Description = "Entity."},
            {.Name = "resolution", .Type = ConfigFieldType::UInt, .Description = "Resolution.", .Min = 1, .Max = 512},
            {.Name = "k_neighbors", .Type = ConfigFieldType::UInt, .Description = "K neighbors.", .Min = 1, .Max = 63},
            {.Name = "normal_k_neighbors",
                .Type = ConfigFieldType::UInt,
                .Description = "Normal k neighbors.",
                .Min = 3,
                .Max = 1024},
            {.Name = "gpu_query_batch_size",
                .Type = ConfigFieldType::UInt,
                .Description = "Gpu query batch size.",
                .Min = 1,
                .Max = 16384},
            {.Name = "max_grid_vertices",
                .Type = ConfigFieldType::UInt,
                .Description = "Max grid vertices.",
                .Min = 8,
                .Max = 16777216},
            {.Name = "backend",
                .Type = ConfigFieldType::StringEnum,
                .Description = "Backend.",
                .EnumNames = kPointConstructionBackendNames,
                .EnumValues = kPointConstructionBackend},
            {.Name = "method",
                .Type = ConfigFieldType::StringEnum,
                .Description = "Method.",
                .EnumValues = kPointConstructionMethod},
            {.Name = "estimate_normals", .Type = ConfigFieldType::Bool, .Description = "Estimate normals."},
            {.Name = "mutual", .Type = ConfigFieldType::Bool, .Description = "Mutual."},
            {.Name = "output_name",
                .Type = ConfigFieldType::String,
                .Description = "Output entity name containing 1..256 bytes.",
                .NonEmpty = true},
            {.Name = "bounding_box_padding",
                .Type = ConfigFieldType::Float,
                .Description = "Bounding box padding.",
                .Min = 0,
                .Max = double(std::numeric_limits<float>::max())},
            {.Name = "normal_agreement_power",
                .Type = ConfigFieldType::Float,
                .Description = "Normal agreement power.",
                .Min = 0,
                .Max = double(std::numeric_limits<float>::max())},
            {.Name = "kernel_sigma_scale",
                .Type = ConfigFieldType::Float,
                .Description =
                    "Must be strictly positive after conversion to float; zero and positive values that underflow are rejected.",
                .Min = 0,
                .Max = double(std::numeric_limits<float>::max())},
            {.Name = "min_distance_epsilon",
                .Type = ConfigFieldType::Float,
                .Description = "Min distance epsilon.",
                .Min = 0,
                .Max = double(std::numeric_limits<float>::max())},
            {.Name = "positions",
                .Type = ConfigFieldType::PropertyRef,
                .Description = "Positions.",
                .RefKinds = kVec3Kind},
            {.Name = "normals",
                .Type = ConfigFieldType::PropertyRef,
                .Description =
                    "For Hoppe with estimate_normals disabled, supplied normals must share the position domain.",
                .RefKinds = kVec3Kind},
            kOneShotExecutionFields[3],
            kOneShotExecutionFields[4],
        };
    }
}

namespace Extrinsic::Runtime
{
    namespace
    {
        using Json=nlohmann::json;
        KeypointAnalysisConfig ParseKeypointAnalysisConfig(const Json& data)
        {
            KeypointAnalysisConfig c;c.StableEntityId=data.at("entity");
            for(unsigned i=0;i<4;++i)if(data.at("backend")==ToString(KeypointAnalysisBackend(i)))c.Backend=KeypointAnalysisBackend(i);
            ConfigDetail::DecodePointPropertyRef(data.at("positions"),c.Positions);ConfigDetail::DecodePointPropertyRef(data.at("mask"),c.Mask);ConfigDetail::DecodePointPropertyRef(data.at("score"),c.Score);
            c.MinimumNeighbors=data.at("minimum_neighbors");c.GpuQueryBatchSize=data.at("gpu_query_batch_size");
            c.GpuRadiusCapacity=data.at("gpu_radius_capacity");c.SalientRadius=data.at("salient_radius");
            c.NonMaxRadius=data.at("nonmax_radius");c.Gamma21=data.at("gamma21");c.Gamma32=data.at("gamma32");
            // Validated tokens (the field table checked them).
            c.Execution.AutoAccept =
                ParseStepRunAutoAccept(data.at("auto_accept").get<std::string>()).value_or(StepRunAutoAccept::Never);
            c.Execution.Start = ParseStepRunStart(data.at("start").get<std::string>()).value_or(StepRunStart::Ready);
            return c;
        }
    }

    const char* ToString(KeypointAnalysisBackend b) noexcept
    {
        switch(b){case KeypointAnalysisBackend::CpuKDTree:return "cpu_kdtree";case KeypointAnalysisBackend::CpuLBVH:return "cpu_lbvh";case KeypointAnalysisBackend::VulkanLBVH:return "vulkan_lbvh";case KeypointAnalysisBackend::VulkanCompute:return "vulkan_compute";}
        return "invalid";
    }

    std::string SerializeKeypointAnalysisConfig(const KeypointAnalysisConfig& c)
    {
        return ConfigDetail::SerializeConfigJson(Json{{"entity", c.StableEntityId},
            {"backend", ToString(c.Backend)},
            {"positions", ConfigDetail::EncodePointPropertyRef(c.Positions)},
            {"mask", ConfigDetail::EncodePointPropertyRef(c.Mask)},
            {"score", ConfigDetail::EncodePointPropertyRef(c.Score)},
            {"minimum_neighbors", c.MinimumNeighbors},
            {"gpu_query_batch_size", c.GpuQueryBatchSize},
            {"gpu_radius_capacity", c.GpuRadiusCapacity},
            {"salient_radius", c.SalientRadius},
            {"nonmax_radius", c.NonMaxRadius},
            {"gamma21", c.Gamma21},
            {"gamma32", c.Gamma32},
            {"auto_accept", std::string{ToString(c.Execution.AutoAccept)}},
            {"start", std::string{ToString(c.Execution.Start)}}});
    }
    Core::Config::EngineConfigSectionValidationResult ValidateKeypointAnalysisConfigSection(
        std::string_view payload,std::string_view,std::string_view subject)
    {
        using namespace Core::Config;
        using ConfigDetail::RejectConfigSection;
        auto input=ConfigDetail::ParseConfigJson(payload, false),data=ConfigDetail::ParseConfigJson(SerializeKeypointAnalysisConfig({}), true);
        if (auto error = ConfigDetail::ValidatePointConfigFields(
            input, data, "Keypoint analysis config must be an object.", "Unknown keypoint field: ",
            kKeypointAnalysisFields))
            return RejectConfigSection(subject, std::move(*error));
        if(!ConfigDetail::InConfigFieldRange(kKeypointAnalysisFields, "gpu_query_batch_size", data["gpu_query_batch_size"].get<double>()) ||
           !ConfigDetail::InConfigFieldRange(kKeypointAnalysisFields, "gpu_radius_capacity", data["gpu_radius_capacity"].get<double>()))
            return RejectConfigSection(subject, "GPU query batch must be 1..16384 and complete radius capacity 1..1024.");
        if(!ConfigDetail::IsConfigFieldToken(kKeypointAnalysisFields, "backend", data["backend"]))
            return RejectConfigSection(subject, "Unknown keypoint backend.");
        for (auto key : {"auto_accept", "start"})
            if (!ConfigDetail::IsConfigFieldToken(kKeypointAnalysisFields, key, data[key]))
                return RejectConfigSection(subject, std::string("Unknown keypoint ") + key + " value.");
        for(auto key:{"salient_radius","nonmax_radius"})
            if(!data[key].is_number() || !ConfigDetail::InConfigFieldRange(kKeypointAnalysisFields, key, data[key].get<double>()) ||
               (data[key]>0 && data[key].get<float>()==0))
                return RejectConfigSection(subject, std::string(key)+" must be zero (automatic) or a positive representable float.");
        for(auto key:{"gamma21","gamma32"})
            if(!data[key].is_number() || !ConfigDetail::InConfigFieldRange(kKeypointAnalysisFields, key, data[key].get<double>()))
                return RejectConfigSection(subject, std::string(key)+" must be finite in [0,1].");
        if (auto error = ConfigDetail::ValidatePointConfigPropertyRefs(
            data, kKeypointAnalysisFields))
            return RejectConfigSection(subject, std::move(*error));
        if(data["positions"]["domain"]!=data["mask"]["domain"] || data["mask"]["domain"]!=data["score"]["domain"] ||
           data["positions"]["name"]==data["mask"]["name"] || data["positions"]["name"]==data["score"]["name"] || data["mask"]["name"]==data["score"]["name"])
            return RejectConfigSection(subject, "Position, mask and score must be distinct properties on the same domain.");
        return ConfigDetail::AcceptConfigSection(SerializeKeypointAnalysisConfig(ParseKeypointAnalysisConfig(data)), input.size());
    }
    std::optional<KeypointAnalysisConfig> GetKeypointAnalysisConfig(const Core::Config::EngineConfig& c)
    {
        return ConfigDetail::GetPointConfig(c, kKeypointAnalysisConfigSectionName, kKeypointAnalysisConfigSectionSchemaId, 1u, ValidateKeypointAnalysisConfigSection, ParseKeypointAnalysisConfig);
    }
    void SetKeypointAnalysisConfig(Core::Config::EngineConfig& c,const KeypointAnalysisConfig& value)
    {Core::Config::UpsertEngineConfigSection(c.AppSections,ConfigDetail::MakeConfigSection(
            kKeypointAnalysisConfigSectionName, kKeypointAnalysisConfigSectionSchemaId, 1, SerializeKeypointAnalysisConfig(value)));}
    std::span<const ConfigFieldSpec> KeypointAnalysisConfigFieldSpecs() noexcept
    { return kKeypointAnalysisFields; }
    Core::Config::EngineConfigSectionRegistration MakeKeypointAnalysisConfigSectionRegistration()
    {return ConfigDetail::MakeSectionRegistration(
            kKeypointAnalysisConfigSectionName, kKeypointAnalysisConfigSectionSchemaId, 1, SerializeKeypointAnalysisConfig({}), ValidateKeypointAnalysisConfigSection,
            ConfigDetail::BuildSectionSchemaJson(kKeypointAnalysisConfigSectionSchemaId, "KeypointAnalysis", "Validated KeypointAnalysis configuration.",
                kKeypointAnalysisFields, ConfigDetail::ParseConfigJson(SerializeKeypointAnalysisConfig({}), true)));}
}

namespace Extrinsic::Runtime
{
    namespace
    {
        using Json = nlohmann::json;
        PointConstructionConfig ParsePointConstructionConfig(const Json& data)
        {
            PointConstructionConfig c;
            c.StableEntityId = data.at("entity");
            c.OutputName = data.at("output_name");
            for (unsigned i = 0; i < 2; ++i)
                if (data.at("method") == ToString(PointConstructionMethod(i)))
                    c.Method = PointConstructionMethod(i);
            for (unsigned i = 0; i < 3; ++i)
                if (data.at("backend") == ToString(PointConstructionBackend(i)))
                    c.Backend = PointConstructionBackend(i);
            ConfigDetail::DecodePointPropertyRef(data.at("positions"), c.Positions);
            ConfigDetail::DecodePointPropertyRef(data.at("normals"), c.Normals);
            c.EstimateNormals = data.at("estimate_normals");
            c.Mutual = data.at("mutual");
            c.Resolution = data.at("resolution");
            c.KNeighbors = data.at("k_neighbors");
            c.NormalKNeighbors = data.at("normal_k_neighbors");
            c.GpuQueryBatchSize = data.at("gpu_query_batch_size");
            c.MaxGridVertices = data.at("max_grid_vertices");
            c.BoundingBoxPadding = data.at("bounding_box_padding");
            c.NormalAgreementPower = data.at("normal_agreement_power");
            c.KernelSigmaScale = data.at("kernel_sigma_scale");
            c.MinDistanceEpsilon = data.at("min_distance_epsilon");
            // Validated tokens (the field table checked them).
            c.Execution.AutoAccept =
                ParseStepRunAutoAccept(data.at("auto_accept").get<std::string>()).value_or(StepRunAutoAccept::Never);
            c.Execution.Start = ParseStepRunStart(data.at("start").get<std::string>()).value_or(StepRunStart::Ready);
            return c;
        }
    } // namespace
    const char* ToString(PointConstructionMethod m) noexcept
    {
        switch (m)
        {
        case PointConstructionMethod::Hoppe:
            return "hoppe";
        case PointConstructionMethod::KnnGraph:
            return "knn_graph";
        }
        return "invalid";
    }
    const char* ToString(PointConstructionBackend b) noexcept
    {
        switch (b)
        {
        case PointConstructionBackend::CpuReference:
            return "cpu_reference";
        case PointConstructionBackend::CpuLBVH:
            return "cpu_lbvh";
        case PointConstructionBackend::VulkanLBVH:
            return "vulkan_lbvh";
        }
        return "invalid";
    }
    std::string SerializePointConstructionConfig(const PointConstructionConfig& c)
    {
        return ConfigDetail::SerializeConfigJson(Json{{"entity", c.StableEntityId},
            {"method", ToString(c.Method)},
            {"backend", ToString(c.Backend)},
            {"positions", ConfigDetail::EncodeVec3PointPropertyRef(c.Positions)},
            {"normals", ConfigDetail::EncodeVec3PointPropertyRef(c.Normals)},
            {"output_name", c.OutputName},
            {"estimate_normals", c.EstimateNormals},
            {"mutual", c.Mutual},
            {"resolution", c.Resolution},
            {"k_neighbors", c.KNeighbors},
            {"normal_k_neighbors", c.NormalKNeighbors},
            {"gpu_query_batch_size", c.GpuQueryBatchSize},
            {"max_grid_vertices", c.MaxGridVertices},
            {"bounding_box_padding", c.BoundingBoxPadding},
            {"normal_agreement_power", c.NormalAgreementPower},
            {"kernel_sigma_scale", c.KernelSigmaScale},
            {"min_distance_epsilon", c.MinDistanceEpsilon},
            {"auto_accept", std::string{ToString(c.Execution.AutoAccept)}},
            {"start", std::string{ToString(c.Execution.Start)}}});
    }
    Core::Config::EngineConfigSectionValidationResult
    ValidatePointConstructionConfigSection(std::string_view payload, std::string_view,
                                           std::string_view subject)
    {
        using namespace Core::Config;
        using ConfigDetail::RejectConfigSection;
        auto input = ConfigDetail::ParseConfigJson(payload, false),
             data = ConfigDetail::ParseConfigJson(SerializePointConstructionConfig({}), true);
        if (auto error = ConfigDetail::ValidatePointConfigFields(
            input, data, "Point construction config must be an object.", "Unknown construction field: ",
            kPointConstructionFields))
            return RejectConfigSection(subject, std::move(*error));
        if (!ConfigDetail::InConfigFieldRange(kPointConstructionFields, "resolution", data["resolution"].get<double>()) || !ConfigDetail::InConfigFieldRange(kPointConstructionFields, "k_neighbors", data["k_neighbors"].get<double>()) || !ConfigDetail::InConfigFieldRange(kPointConstructionFields, "normal_k_neighbors", data["normal_k_neighbors"].get<double>()) || !ConfigDetail::InConfigFieldRange(kPointConstructionFields, "gpu_query_batch_size", data["gpu_query_batch_size"].get<double>()) || !ConfigDetail::InConfigFieldRange(kPointConstructionFields, "max_grid_vertices", data["max_grid_vertices"].get<double>()))
            return RejectConfigSection(subject,
                "Resolution must be 1..512, k 1..63, normal k 3..1024, batch 1..16384 "
                "and grid budget 8..16777216.");
        if (!ConfigDetail::IsConfigFieldToken(kPointConstructionFields, "backend", data["backend"]))
            return RejectConfigSection(subject, "Unknown construction backend.");
        if (!ConfigDetail::IsConfigFieldToken(kPointConstructionFields, "method", data["method"]))
            return RejectConfigSection(subject, "Unknown construction method.");
        for (auto key : {"auto_accept", "start"})
            if (!ConfigDetail::IsConfigFieldToken(kPointConstructionFields, key, data[key]))
                return RejectConfigSection(subject, std::string("Unknown construction ") + key + " value.");
        if (!data["estimate_normals"].is_boolean() || !data["mutual"].is_boolean())
            return RejectConfigSection(subject, "Construction switches must be boolean.");
        if (!data["output_name"].is_string() || data["output_name"].get<std::string>().empty() ||
            data["output_name"].get<std::string>().size() > 256)
            return RejectConfigSection(subject, "Output name must contain 1..256 bytes.");
        if (auto error = ConfigDetail::ValidatePointConfigNonnegativeFloats(
            data, kPointConstructionFields))
            return RejectConfigSection(subject, std::move(*error));
        if (data["kernel_sigma_scale"].get<float>() <= 0)
            return RejectConfigSection(subject, "Kernel sigma scale must be positive.");
        using ConfigDetail::PointPropertyValidation;
        if (ConfigDetail::ValidatePointPropertyRef(data["positions"], FindConfigFieldSpec(kPointConstructionFields, "positions")->RefKinds.front(), FindConfigFieldSpec(kPointConstructionFields, "positions")->AnyScalar) != PointPropertyValidation::Valid ||
            ConfigDetail::ValidatePointPropertyRef(data["normals"], FindConfigFieldSpec(kPointConstructionFields, "normals")->RefKinds.front(), FindConfigFieldSpec(kPointConstructionFields, "normals")->AnyScalar) != PointPropertyValidation::Valid)
            return RejectConfigSection(subject, "Inputs require canonical vec3 property references.");
        if (data["method"] == "hoppe" && data["estimate_normals"] == false &&
            data["positions"]["domain"] != data["normals"]["domain"])
            return RejectConfigSection(subject, "Supplied normals must share the position domain.");
        return ConfigDetail::AcceptConfigSection(SerializePointConstructionConfig(ParsePointConstructionConfig(data)), input.size());
    }
    std::optional<PointConstructionConfig>
    GetPointConstructionConfig(const Core::Config::EngineConfig& c)
    {
        return ConfigDetail::GetPointConfig(c, kPointConstructionConfigSectionName, kPointConstructionConfigSectionSchemaId, 1u, ValidatePointConstructionConfigSection, ParsePointConstructionConfig);
    }
    void SetPointConstructionConfig(Core::Config::EngineConfig& c,
                                    const PointConstructionConfig& value)
    {
        Core::Config::UpsertEngineConfigSection(c.AppSections, ConfigDetail::MakeConfigSection(
            kPointConstructionConfigSectionName, kPointConstructionConfigSectionSchemaId, 1, SerializePointConstructionConfig(value)));
    }
    std::span<const ConfigFieldSpec> PointConstructionConfigFieldSpecs() noexcept
    { return kPointConstructionFields; }
    Core::Config::EngineConfigSectionRegistration MakePointConstructionConfigSectionRegistration()
    {
        return ConfigDetail::MakeSectionRegistration(
            kPointConstructionConfigSectionName, kPointConstructionConfigSectionSchemaId, 1, SerializePointConstructionConfig({}), ValidatePointConstructionConfigSection,
            ConfigDetail::BuildSectionSchemaJson(kPointConstructionConfigSectionSchemaId, "PointConstruction", "Validated PointConstruction configuration.",
                kPointConstructionFields, ConfigDetail::ParseConfigJson(SerializePointConstructionConfig({}), true)));
    }
} // namespace Extrinsic::Runtime
