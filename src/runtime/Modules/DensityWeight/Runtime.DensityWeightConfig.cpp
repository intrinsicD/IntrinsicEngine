module;
#include <cmath>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <nlohmann/json.hpp>
module Extrinsic.Runtime.DensityWeightConfig;
namespace Extrinsic::Runtime
{
    namespace
    {
        using Json=nlohmann::json;
        const char* Kind(Geometry::PropertyValueKind k)
        {
            switch(k){case Geometry::PropertyValueKind::Vec3:return "vec3";case Geometry::PropertyValueKind::UInt32:return "uint32";case Geometry::PropertyValueKind::Float:return "float";default:return "invalid";}
        }
        Json Ref(const GeometryPropertyRef& ref)
        {
            return {{"domain",ref.Domain>=GeometryElementDomain::Unknown && ref.Domain<=GeometryElementDomain::PointCloudPoint ? ToString(ref.Domain) : "invalid"},{"name",ref.Name},{"kind",Kind(ref.ValueKind)}};
        }
        DensityWeightConfig Parse(const Json& data)
        {
            DensityWeightConfig c;c.StableEntityId=data.at("entity");
            for(unsigned i=0;i<3;++i)if(data.at("backend")==ToString(DensityWeightBackend(i)))c.Backend=DensityWeightBackend(i);
            auto read=[&](const Json& value,GeometryPropertyRef& ref){
                ref.Name=value.at("name");
                for(unsigned i=0;i<=unsigned(GeometryElementDomain::PointCloudPoint);++i)
                    if(value.at("domain")==ToString(GeometryElementDomain(i)))ref.Domain=GeometryElementDomain(i);
            };
            read(data.at("positions"),c.Positions);read(data.at("weights"),c.Weights);
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
        return Json{{"entity",c.StableEntityId},{"backend",ToString(c.Backend)},
            {"positions",Ref(c.Positions)},{"weights",Ref(c.Weights)},
            {"support_radius",c.SupportRadius},{"kernel",Geometry::PointCloud::Kernels::DebugName(c.Kernel)},
            {"mode",Geometry::PointCloud::Kernels::DebugName(c.Mode)},
            {"gpu_query_batch_size",c.GpuQueryBatchSize},{"gpu_radius_capacity",c.GpuRadiusCapacity}}.dump();
    }
    Core::Config::EngineConfigSectionValidationResult ValidateDensityWeightConfigSection(
        std::string_view payload,std::string_view,std::string_view subject)
    {
        using namespace Core::Config;
        EngineConfigSectionValidationResult result;
        auto reject=[&](std::string message){result.Diagnostics.push_back({.Code=EngineConfigDiagnosticCode::InvalidValue,
            .Subject=std::string(subject),.Message=std::move(message)});return result;};
        auto input=Json::parse(payload,nullptr,false),data=Json::parse(SerializeDensityWeightConfig({}));
        if(!input.is_object())return reject("Density weight config must be an object.");
        for(auto it=input.begin();it!=input.end();++it)
        {
            if(!data.contains(it.key()))return reject("Unknown density weight field: "+it.key());
            data[it.key()]=it.value();
        }
        for(auto key:{"entity","gpu_query_batch_size","gpu_radius_capacity"})
            if(!data[key].is_number_unsigned() || data[key].get<std::uint64_t>()>std::numeric_limits<std::uint32_t>::max())
                return reject(std::string(key)+" must be an unsigned 32-bit integer.");
        if(data["gpu_query_batch_size"]==0 || data["gpu_query_batch_size"]>16384 ||
           data["gpu_radius_capacity"]==0 || data["gpu_radius_capacity"]>1024)
            return reject("GPU query batch must be 1..16384 and complete radius capacity 1..1024.");
        if(data["backend"]!="cpu_kdtree" && data["backend"]!="cpu_lbvh" && data["backend"]!="vulkan_lbvh")
            return reject("Unknown density weight backend.");
        if(!data["support_radius"].is_number() || !std::isfinite(data["support_radius"].get<double>()) ||
           data["support_radius"]<=0 || data["support_radius"].get<double>()>std::numeric_limits<float>::max())
            return reject("Support radius must be positive and finite, at most floatmax.");
        if(data["kernel"]!="gaussian" && data["kernel"]!="theta_lop" && data["kernel"]!="wendland_c2")return reject("Unknown radial kernel.");
        if(data["mode"]!="direct" && data["mode"]!="reciprocal")return reject("Unknown density weight mode.");
        const auto validRef=[&](const Json& ref,Geometry::PropertyValueKind kind)
        {
            if(!ref.is_object() || ref.size()!=3 || !ref.contains("domain") || !ref.contains("name") || !ref.contains("kind") ||
               ref["kind"]!=Kind(kind) || !ref["name"].is_string() || ref["name"].get<std::string>().empty())return false;
            for(unsigned i=0;i<=unsigned(GeometryElementDomain::PointCloudPoint);++i)
                if(ref["domain"]==ToString(GeometryElementDomain(i)))return true;
            return false;
        };
        if(!validRef(data["positions"],Geometry::PropertyValueKind::Vec3) || !validRef(data["weights"],Geometry::PropertyValueKind::Float) ||
           data["positions"]["domain"]!=data["weights"]["domain"] || data["positions"]["name"]==data["weights"]["name"])
            return reject("Position and weight bindings need distinct canonical vec3/float properties on the same domain.");
        result.State=EngineConfigState::Valid;result.CanonicalPayloadJson=SerializeDensityWeightConfig(Parse(data));result.ParsedFieldCount=input.size();return result;
    }
    std::optional<DensityWeightConfig> GetDensityWeightConfig(const Core::Config::EngineConfig& c)
    {
        const auto* section=Core::Config::FindEngineConfigSection(c.AppSections,kDensityWeightConfigSectionName);
        if(!section || section->SchemaId!=kDensityWeightConfigSectionSchemaId || section->SchemaVersion!=1)return {};
        auto validation=ValidateDensityWeightConfigSection(section->PayloadJson,{},kDensityWeightConfigSectionName);
        if(!validation.Usable())return {};
        return Parse(Json::parse(validation.CanonicalPayloadJson));
    }
    void SetDensityWeightConfig(Core::Config::EngineConfig& c,const DensityWeightConfig& value)
    {Core::Config::UpsertEngineConfigSection(c.AppSections,Section(value));}
    Core::Config::EngineConfigSectionRegistration MakeDensityWeightConfigSectionRegistration()
    {return {.DefaultSection=Section({}),.Validate=ValidateDensityWeightConfigSection};}
}
