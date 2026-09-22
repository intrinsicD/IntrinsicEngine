// Compiles OutlierAnalysis, KernelDensity, PointSpacing, DensityWeight config codecs together to share JSON work.
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;

import Extrinsic.Runtime.OutlierAnalysisConfig;
import Extrinsic.Runtime.KernelDensityConfig;
import Extrinsic.Runtime.PointSpacingConfig;
import Extrinsic.Runtime.DensityWeightConfig;

#include "Config/internal/Runtime.PointConfigJson.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        using Json=nlohmann::json;
        OutlierAnalysisConfig ParseOutlierAnalysisConfig(const Json& data)
        {
            OutlierAnalysisConfig c;c.StableEntityId=data.at("entity");
            for(unsigned i=0;i<3;++i)if(data.at("method")==ToString(OutlierAnalysisMethod(i)))c.Method=OutlierAnalysisMethod(i);
            c.Operation=data.at("operation")=="remove_marked"?OutlierAnalysisOperation::RemoveMarked:OutlierAnalysisOperation::Analyze;
            for(unsigned i=0;i<3;++i)if(data.at("backend")==ToString(OutlierAnalysisBackend(i)))c.Backend=OutlierAnalysisBackend(i);
            ConfigDetail::DecodePointPropertyRef(data.at("positions"),c.Positions);ConfigDetail::DecodePointPropertyRef(data.at("mask"),c.Mask);ConfigDetail::DecodePointPropertyRef(data.at("score"),c.Score);
            c.KNeighbors=data.at("k_neighbors");c.MinimumNeighbors=data.at("minimum_neighbors");
            c.GpuQueryBatchSize=data.at("gpu_query_batch_size");c.Radius=data.at("radius");c.StdDevMultiplier=data.at("stddev_multiplier");c.ScoreThreshold=data.at("score_threshold");
            return c;
        }
        Core::Config::EngineConfigSection MakeOutlierAnalysisConfigSection(const OutlierAnalysisConfig& c)
        {
            return {.Name=std::string(kOutlierAnalysisConfigSectionName),.SchemaId=std::string(kOutlierAnalysisConfigSectionSchemaId),
                    .SchemaVersion=1,.PayloadJson=SerializeOutlierAnalysisConfig(c)};
        }
    }
    const char* ToString(OutlierAnalysisMethod m) noexcept
    {
        switch(m){case OutlierAnalysisMethod::Statistical:return "statistical";case OutlierAnalysisMethod::Radius:return "radius";case OutlierAnalysisMethod::LocalDistanceRatio:return "local_distance_ratio";}
        return "invalid";
    }
    const char* ToString(OutlierAnalysisBackend b) noexcept
    {
        switch(b){case OutlierAnalysisBackend::CpuOctree:return "cpu_octree";case OutlierAnalysisBackend::CpuLBVH:return "cpu_lbvh";case OutlierAnalysisBackend::VulkanLBVH:return "vulkan_lbvh";}
        return "invalid";
    }
    const char* ToString(OutlierAnalysisOperation o) noexcept
    {
        switch(o){case OutlierAnalysisOperation::Analyze:return "analyze";case OutlierAnalysisOperation::RemoveMarked:return "remove_marked";}
        return "invalid";
    }
    std::string SerializeOutlierAnalysisConfig(const OutlierAnalysisConfig& c)
    {
        return ConfigDetail::SerializeConfigJson(Json{{"entity",c.StableEntityId},{"method",ToString(c.Method)},{"backend",ToString(c.Backend)},
                    {"operation",ToString(c.Operation)},{"positions",ConfigDetail::EncodePointPropertyRef(c.Positions)},{"mask",ConfigDetail::EncodePointPropertyRef(c.Mask)},{"score",ConfigDetail::EncodePointPropertyRef(c.Score)},
                    {"k_neighbors",c.KNeighbors},{"minimum_neighbors",c.MinimumNeighbors},{"gpu_query_batch_size",c.GpuQueryBatchSize},
                    {"radius",c.Radius},{"stddev_multiplier",c.StdDevMultiplier},{"score_threshold",c.ScoreThreshold}});
    }
    Core::Config::EngineConfigSectionValidationResult ValidateOutlierAnalysisConfigSection(
        std::string_view payload,std::string_view,std::string_view subject)
    {
        using namespace Core::Config;
        using ConfigDetail::RejectConfigSection;
        EngineConfigSectionValidationResult result;
        auto input=ConfigDetail::ParseConfigJson(payload, false),data=ConfigDetail::ParseConfigJson(SerializeOutlierAnalysisConfig({}), true);
        if (auto error = ConfigDetail::ValidatePointConfigFields(
            input, data, "Outlier analysis config must be an object.", "Unknown outlier field: ",
            {"entity", "k_neighbors", "minimum_neighbors", "gpu_query_batch_size"}))
            return RejectConfigSection(subject, std::move(*error));
        if(data["k_neighbors"]==0 || data["gpu_query_batch_size"]==0 || data["gpu_query_batch_size"]>16384)
            return RejectConfigSection(subject, "k must be positive and GPU query batch size must be 1..16384.");
        if(data["method"]!="statistical" && data["method"]!="radius" && data["method"]!="local_distance_ratio")return RejectConfigSection(subject, "Unknown outlier method.");
        if(data["operation"]!="analyze" && data["operation"]!="remove_marked")return RejectConfigSection(subject, "Unknown outlier operation.");
        if(data["backend"]!="cpu_octree" && data["backend"]!="cpu_lbvh" && data["backend"]!="vulkan_lbvh")return RejectConfigSection(subject, "Unknown outlier backend.");
        if (auto error = ConfigDetail::ValidatePointConfigNonnegativeFloats(
            data, {"radius", "stddev_multiplier", "score_threshold"}))
            return RejectConfigSection(subject, std::move(*error));
        if(data["method"]=="radius" && data["radius"].get<double>()<=0)return RejectConfigSection(subject, "Radius must be positive.");
        const OutlierAnalysisConfig defaults;
        if (auto error = ConfigDetail::ValidatePointConfigPropertyRefs(
            data, {{"positions", defaults.Positions.ValueKind}, {"mask", defaults.Mask.ValueKind}, {"score", defaults.Score.ValueKind}}))
            return RejectConfigSection(subject, std::move(*error));
        if(data["positions"]["domain"]!=data["mask"]["domain"] || data["mask"]["domain"]!=data["score"]["domain"] ||
           data["positions"]["name"]==data["mask"]["name"] || data["positions"]["name"]==data["score"]["name"] || data["mask"]["name"]==data["score"]["name"])
            return RejectConfigSection(subject, "Position, mask and score must be distinct properties on the same domain.");
        result.State=EngineConfigState::Valid;result.CanonicalPayloadJson=SerializeOutlierAnalysisConfig(ParseOutlierAnalysisConfig(data));result.ParsedFieldCount=input.size();return result;
    }
    std::optional<OutlierAnalysisConfig> GetOutlierAnalysisConfig(const Core::Config::EngineConfig& c)
    {
        const auto payload = ConfigDetail::FindValidatedCanonicalPayload(
            c, kOutlierAnalysisConfigSectionName, kOutlierAnalysisConfigSectionSchemaId, 1u,
            nullptr, ValidateOutlierAnalysisConfigSection);
        if (!payload) return {};
        return ParseOutlierAnalysisConfig(ConfigDetail::ParseConfigJson(*payload, true));
    }
    void SetOutlierAnalysisConfig(Core::Config::EngineConfig& c,const OutlierAnalysisConfig& value)
    {Core::Config::UpsertEngineConfigSection(c.AppSections,MakeOutlierAnalysisConfigSection(value));}
    Core::Config::EngineConfigSectionRegistration MakeOutlierAnalysisConfigSectionRegistration()
    {return {.DefaultSection=MakeOutlierAnalysisConfigSection({}),.Validate=ValidateOutlierAnalysisConfigSection};}
}

namespace Extrinsic::Runtime
{
    namespace
    {
        using Json=nlohmann::json;
        KernelDensityConfig ParseKernelDensityConfig(const Json& data)
        {
            KernelDensityConfig c;c.StableEntityId=data.at("entity");
            for(unsigned i=0;i<3;++i)if(data.at("backend")==ToString(KernelDensityBackend(i)))c.Backend=KernelDensityBackend(i);
            ConfigDetail::DecodePointPropertyRef(data.at("positions"),c.Positions);ConfigDetail::DecodePointPropertyRef(data.at("density"),c.Density);
            c.KNeighbors=data.at("k_neighbors");
            c.GpuQueryBatchSize=data.at("gpu_query_batch_size");c.Bandwidth=data.at("bandwidth");
            return c;
        }
        Core::Config::EngineConfigSection MakeKernelDensityConfigSection(const KernelDensityConfig& c)
        {
            return {.Name=std::string(kKernelDensityConfigSectionName),.SchemaId=std::string(kKernelDensityConfigSectionSchemaId),
                    .SchemaVersion=1,.PayloadJson=SerializeKernelDensityConfig(c)};
        }
    }
    const char* ToString(KernelDensityBackend b) noexcept
    {
        switch(b){case KernelDensityBackend::CpuOctree:return "cpu_octree";case KernelDensityBackend::CpuLBVH:return "cpu_lbvh";case KernelDensityBackend::VulkanLBVH:return "vulkan_lbvh";}
        return "invalid";
    }
    std::string SerializeKernelDensityConfig(const KernelDensityConfig& c)
    {
        return ConfigDetail::SerializeConfigJson(Json{{"entity",c.StableEntityId},{"backend",ToString(c.Backend)},
                    {"positions",ConfigDetail::EncodePointPropertyRef(c.Positions)},{"density",ConfigDetail::EncodePointPropertyRef(c.Density)},
                    {"k_neighbors",c.KNeighbors},{"gpu_query_batch_size",c.GpuQueryBatchSize},
                    {"bandwidth",c.Bandwidth}});
    }
    Core::Config::EngineConfigSectionValidationResult ValidateKernelDensityConfigSection(
        std::string_view payload,std::string_view,std::string_view subject)
    {
        using namespace Core::Config;
        using ConfigDetail::RejectConfigSection;
        EngineConfigSectionValidationResult result;
        auto input=ConfigDetail::ParseConfigJson(payload, false),data=ConfigDetail::ParseConfigJson(SerializeKernelDensityConfig({}), true);
        if (auto error = ConfigDetail::ValidatePointConfigFields(
            input, data, "Kernel density config must be an object.", "Unknown density field: ",
            {"entity", "k_neighbors", "gpu_query_batch_size"}))
            return RejectConfigSection(subject, std::move(*error));
        if(data["gpu_query_batch_size"]==0 || data["gpu_query_batch_size"]>16384)
            return RejectConfigSection(subject, "GPU query batch size must be 1..16384.");
        if(data["backend"]!="cpu_octree" && data["backend"]!="cpu_lbvh" && data["backend"]!="vulkan_lbvh")return RejectConfigSection(subject, "Unknown density backend.");
        if (auto error = ConfigDetail::ValidatePointConfigNonnegativeFloats(
            data, {"bandwidth"}))
            return RejectConfigSection(subject, std::move(*error));
        if(data["bandwidth"].get<double>()>0 && data["bandwidth"].get<float>()==0)
            return RejectConfigSection(subject, "Positive bandwidth must remain positive in float storage.");
        const KernelDensityConfig defaults;
        if (auto error = ConfigDetail::ValidatePointConfigPropertyRefs(
            data, {{"positions", defaults.Positions.ValueKind}, {"density", defaults.Density.ValueKind}}))
            return RejectConfigSection(subject, std::move(*error));
        if(data["positions"]["domain"]!=data["density"]["domain"] ||
           data["positions"]["name"]==data["density"]["name"])
            return RejectConfigSection(subject, "Position and density must be distinct properties on the same domain.");
        result.State=EngineConfigState::Valid;result.CanonicalPayloadJson=SerializeKernelDensityConfig(ParseKernelDensityConfig(data));result.ParsedFieldCount=input.size();return result;
    }
    std::optional<KernelDensityConfig> GetKernelDensityConfig(const Core::Config::EngineConfig& c)
    {
        const auto payload = ConfigDetail::FindValidatedCanonicalPayload(
            c, kKernelDensityConfigSectionName, kKernelDensityConfigSectionSchemaId, 1u,
            nullptr, ValidateKernelDensityConfigSection);
        if (!payload) return {};
        return ParseKernelDensityConfig(ConfigDetail::ParseConfigJson(*payload, true));
    }
    void SetKernelDensityConfig(Core::Config::EngineConfig& c,const KernelDensityConfig& value)
    {Core::Config::UpsertEngineConfigSection(c.AppSections,MakeKernelDensityConfigSection(value));}
    Core::Config::EngineConfigSectionRegistration MakeKernelDensityConfigSectionRegistration()
    {return {.DefaultSection=MakeKernelDensityConfigSection({}),.Validate=ValidateKernelDensityConfigSection};}
}

namespace Extrinsic::Runtime
{
    namespace
    {
        using Json=nlohmann::json;
        PointSpacingConfig ParsePointSpacingConfig(const Json& data)
        {
            PointSpacingConfig c;c.StableEntityId=data.at("entity");
            for(unsigned i=0;i<3;++i)if(data.at("backend")==ToString(PointSpacingBackend(i)))c.Backend=PointSpacingBackend(i);
            ConfigDetail::DecodePointPropertyRef(data.at("positions"),c.Positions);ConfigDetail::DecodePointPropertyRef(data.at("radii"),c.Radii);
            c.KNeighbors=data.at("k_neighbors");
            c.GpuQueryBatchSize=data.at("gpu_query_batch_size");c.ScaleFactor=data.at("scale_factor");
            return c;
        }
        Core::Config::EngineConfigSection MakePointSpacingConfigSection(const PointSpacingConfig& c)
        {
            return {.Name=std::string(kPointSpacingConfigSectionName),.SchemaId=std::string(kPointSpacingConfigSectionSchemaId),
                    .SchemaVersion=1,.PayloadJson=SerializePointSpacingConfig(c)};
        }
    }
    const char* ToString(PointSpacingBackend b) noexcept
    {
        switch(b){case PointSpacingBackend::CpuOctree:return "cpu_octree";case PointSpacingBackend::CpuLBVH:return "cpu_lbvh";case PointSpacingBackend::VulkanLBVH:return "vulkan_lbvh";}
        return "invalid";
    }
    std::string SerializePointSpacingConfig(const PointSpacingConfig& c)
    {
        return ConfigDetail::SerializeConfigJson(Json{{"entity",c.StableEntityId},{"backend",ToString(c.Backend)},
                    {"positions",ConfigDetail::EncodePointPropertyRef(c.Positions)},{"radii",ConfigDetail::EncodePointPropertyRef(c.Radii)},
                    {"k_neighbors",c.KNeighbors},{"gpu_query_batch_size",c.GpuQueryBatchSize},
                    {"scale_factor",c.ScaleFactor}});
    }
    Core::Config::EngineConfigSectionValidationResult ValidatePointSpacingConfigSection(
        std::string_view payload,std::string_view,std::string_view subject)
    {
        using namespace Core::Config;
        using ConfigDetail::RejectConfigSection;
        EngineConfigSectionValidationResult result;
        auto input=ConfigDetail::ParseConfigJson(payload, false),data=ConfigDetail::ParseConfigJson(SerializePointSpacingConfig({}), true);
        if (auto error = ConfigDetail::ValidatePointConfigFields(
            input, data, "Point spacing config must be an object.", "Unknown radii field: ",
            {"entity", "k_neighbors", "gpu_query_batch_size"}))
            return RejectConfigSection(subject, std::move(*error));
        if(data["gpu_query_batch_size"]==0 || data["gpu_query_batch_size"]>16384)
            return RejectConfigSection(subject, "GPU query batch size must be 1..16384.");
        if(data["backend"]!="cpu_octree" && data["backend"]!="cpu_lbvh" && data["backend"]!="vulkan_lbvh")return RejectConfigSection(subject, "Unknown radii backend.");
        if (auto error = ConfigDetail::ValidatePointConfigNonnegativeFloats(
            data, {"scale_factor"}))
            return RejectConfigSection(subject, std::move(*error));
        if(data["scale_factor"].get<double>()>0 && data["scale_factor"].get<float>()==0)
            return RejectConfigSection(subject, "Positive scale factor must remain positive in float storage.");
        const PointSpacingConfig defaults;
        if (auto error = ConfigDetail::ValidatePointConfigPropertyRefs(
            data, {{"positions", defaults.Positions.ValueKind}, {"radii", defaults.Radii.ValueKind}}))
            return RejectConfigSection(subject, std::move(*error));
        if(data["positions"]["domain"]!=data["radii"]["domain"] ||
           data["positions"]["name"]==data["radii"]["name"])
            return RejectConfigSection(subject, "Position and radii must be distinct properties on the same domain.");
        result.State=EngineConfigState::Valid;result.CanonicalPayloadJson=SerializePointSpacingConfig(ParsePointSpacingConfig(data));result.ParsedFieldCount=input.size();return result;
    }
    std::optional<PointSpacingConfig> GetPointSpacingConfig(const Core::Config::EngineConfig& c)
    {
        const auto payload = ConfigDetail::FindValidatedCanonicalPayload(
            c, kPointSpacingConfigSectionName, kPointSpacingConfigSectionSchemaId, 1u,
            nullptr, ValidatePointSpacingConfigSection);
        if (!payload) return {};
        return ParsePointSpacingConfig(ConfigDetail::ParseConfigJson(*payload, true));
    }
    void SetPointSpacingConfig(Core::Config::EngineConfig& c,const PointSpacingConfig& value)
    {Core::Config::UpsertEngineConfigSection(c.AppSections,MakePointSpacingConfigSection(value));}
    Core::Config::EngineConfigSectionRegistration MakePointSpacingConfigSectionRegistration()
    {return {.DefaultSection=MakePointSpacingConfigSection({}),.Validate=ValidatePointSpacingConfigSection};}
}

namespace Extrinsic::Runtime
{
    namespace
    {
        using Json=nlohmann::json;
        DensityWeightConfig ParseDensityWeightConfig(const Json& data)
        {
            DensityWeightConfig c;c.StableEntityId=data.at("entity");
            for(unsigned i=0;i<3;++i)if(data.at("backend")==ToString(DensityWeightBackend(i)))c.Backend=DensityWeightBackend(i);
            ConfigDetail::DecodePointPropertyRef(data.at("positions"),c.Positions);ConfigDetail::DecodePointPropertyRef(data.at("weights"),c.Weights);
            c.SupportRadius=data.at("support_radius");c.GpuQueryBatchSize=data.at("gpu_query_batch_size");
            c.GpuRadiusCapacity=data.at("gpu_radius_capacity");
            for(unsigned i=0;i<3;++i)if(data.at("kernel")==Geometry::PointCloud::Kernels::DebugName(Geometry::PointCloud::Kernels::KernelType(i)))c.Kernel=Geometry::PointCloud::Kernels::KernelType(i);
            for(unsigned i=0;i<2;++i)if(data.at("mode")==Geometry::PointCloud::Kernels::DebugName(Geometry::PointCloud::Kernels::DensityWeightMode(i)))c.Mode=Geometry::PointCloud::Kernels::DensityWeightMode(i);
            return c;
        }
        Core::Config::EngineConfigSection MakeDensityWeightConfigSection(const DensityWeightConfig& c)
        {
            return {.Name=std::string(kDensityWeightConfigSectionName),.SchemaId=std::string(kDensityWeightConfigSectionSchemaId),
                    .SchemaVersion=1,.PayloadJson=SerializeDensityWeightConfig(c)};
        }
    }

    const char* ToString(DensityWeightBackend b) noexcept
    {
        switch(b){case DensityWeightBackend::CpuKDTree:return "cpu_kdtree";case DensityWeightBackend::CpuLBVH:return "cpu_lbvh";case DensityWeightBackend::VulkanLBVH:return "vulkan_lbvh";}
        return "invalid";
    }

    std::string SerializeDensityWeightConfig(const DensityWeightConfig& c)
    {
        return ConfigDetail::SerializeConfigJson(Json{{"entity",c.StableEntityId},{"backend",ToString(c.Backend)},
            {"positions",ConfigDetail::EncodePointPropertyRef(c.Positions)},{"weights",ConfigDetail::EncodePointPropertyRef(c.Weights)},
            {"support_radius",c.SupportRadius},{"kernel",Geometry::PointCloud::Kernels::DebugName(c.Kernel)},
            {"mode",Geometry::PointCloud::Kernels::DebugName(c.Mode)},
            {"gpu_query_batch_size",c.GpuQueryBatchSize},{"gpu_radius_capacity",c.GpuRadiusCapacity}});
    }
    Core::Config::EngineConfigSectionValidationResult ValidateDensityWeightConfigSection(
        std::string_view payload,std::string_view,std::string_view subject)
    {
        using namespace Core::Config;
        using ConfigDetail::RejectConfigSection;
        EngineConfigSectionValidationResult result;
        auto input=ConfigDetail::ParseConfigJson(payload, false),data=ConfigDetail::ParseConfigJson(SerializeDensityWeightConfig({}), true);
        if (auto error = ConfigDetail::ValidatePointConfigFields(
            input, data, "Density weight config must be an object.", "Unknown density weight field: ",
            {"entity", "gpu_query_batch_size", "gpu_radius_capacity"}))
            return RejectConfigSection(subject, std::move(*error));
        if(data["gpu_query_batch_size"]==0 || data["gpu_query_batch_size"]>16384 ||
           data["gpu_radius_capacity"]==0 || data["gpu_radius_capacity"]>1024)
            return RejectConfigSection(subject, "GPU query batch must be 1..16384 and complete radius capacity 1..1024.");
        if(data["backend"]!="cpu_kdtree" && data["backend"]!="cpu_lbvh" && data["backend"]!="vulkan_lbvh")
            return RejectConfigSection(subject, "Unknown density weight backend.");
        if(!data["support_radius"].is_number() || !std::isfinite(data["support_radius"].get<double>()) ||
           data["support_radius"]<=0 || data["support_radius"].get<double>()>std::numeric_limits<float>::max())
            return RejectConfigSection(subject, "Support radius must be positive and finite, at most floatmax.");
        if(data["kernel"]!="gaussian" && data["kernel"]!="theta_lop" && data["kernel"]!="wendland_c2")return RejectConfigSection(subject, "Unknown radial kernel.");
        if(data["mode"]!="direct" && data["mode"]!="reciprocal")return RejectConfigSection(subject, "Unknown density weight mode.");
        using ConfigDetail::PointPropertyValidation;
        if(ConfigDetail::ValidatePointPropertyRef(data["positions"],Geometry::PropertyValueKind::Vec3) != PointPropertyValidation::Valid ||
           ConfigDetail::ValidatePointPropertyRef(data["weights"],Geometry::PropertyValueKind::Float, true) != PointPropertyValidation::Valid ||
           data["positions"]["domain"]!=data["weights"]["domain"] || data["positions"]["name"]==data["weights"]["name"])
            return RejectConfigSection(subject, "Position and weight bindings need distinct canonical vec3/float properties on the same domain.");
        result.State=EngineConfigState::Valid;result.CanonicalPayloadJson=SerializeDensityWeightConfig(ParseDensityWeightConfig(data));result.ParsedFieldCount=input.size();return result;
    }
    std::optional<DensityWeightConfig> GetDensityWeightConfig(const Core::Config::EngineConfig& c)
    {
        const auto payload = ConfigDetail::FindValidatedCanonicalPayload(
            c, kDensityWeightConfigSectionName, kDensityWeightConfigSectionSchemaId, 1u,
            nullptr, ValidateDensityWeightConfigSection);
        if (!payload) return {};
        return ParseDensityWeightConfig(ConfigDetail::ParseConfigJson(*payload, true));
    }
    void SetDensityWeightConfig(Core::Config::EngineConfig& c,const DensityWeightConfig& value)
    {Core::Config::UpsertEngineConfigSection(c.AppSections,MakeDensityWeightConfigSection(value));}
    Core::Config::EngineConfigSectionRegistration MakeDensityWeightConfigSectionRegistration()
    {return {.DefaultSection=MakeDensityWeightConfigSection({}),.Validate=ValidateDensityWeightConfigSection};}
}
