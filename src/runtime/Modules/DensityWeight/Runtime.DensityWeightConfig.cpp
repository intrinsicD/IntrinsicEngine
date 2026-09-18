module;
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
module Extrinsic.Runtime.DensityWeightConfig;
#include "Config/internal/Runtime.PointConfigJson.hpp"
namespace Extrinsic::Runtime
{
    namespace
    {
        using Json=nlohmann::json;
        DensityWeightConfig Parse(const Json& data)
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
        Core::Config::EngineConfigSection Section(const DensityWeightConfig& c)
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
           ConfigDetail::ValidatePointPropertyRef(data["weights"],Geometry::PropertyValueKind::Float) != PointPropertyValidation::Valid ||
           data["positions"]["domain"]!=data["weights"]["domain"] || data["positions"]["name"]==data["weights"]["name"])
            return RejectConfigSection(subject, "Position and weight bindings need distinct canonical vec3/float properties on the same domain.");
        result.State=EngineConfigState::Valid;result.CanonicalPayloadJson=SerializeDensityWeightConfig(Parse(data));result.ParsedFieldCount=input.size();return result;
    }
    std::optional<DensityWeightConfig> GetDensityWeightConfig(const Core::Config::EngineConfig& c)
    {
        const auto payload = ConfigDetail::FindValidatedCanonicalPayload(
            c, kDensityWeightConfigSectionName, kDensityWeightConfigSectionSchemaId, 1u,
            nullptr, ValidateDensityWeightConfigSection);
        if (!payload) return {};
        return Parse(ConfigDetail::ParseConfigJson(*payload, true));
    }
    void SetDensityWeightConfig(Core::Config::EngineConfig& c,const DensityWeightConfig& value)
    {Core::Config::UpsertEngineConfigSection(c.AppSections,Section(value));}
    Core::Config::EngineConfigSectionRegistration MakeDensityWeightConfigSectionRegistration()
    {return {.DefaultSection=Section({}),.Validate=ValidateDensityWeightConfigSection};}
}
