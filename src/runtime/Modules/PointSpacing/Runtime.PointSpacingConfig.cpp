module;
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <nlohmann/json.hpp>
module Extrinsic.Runtime.PointSpacingConfig;
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
        PointSpacingConfig Parse(const Json& data)
        {
            PointSpacingConfig c;c.StableEntityId=data.at("entity");
            for(unsigned i=0;i<3;++i)if(data.at("backend")==ToString(PointSpacingBackend(i)))c.Backend=PointSpacingBackend(i);
            auto read=[&](const char* name,GeometryPropertyRef& ref){
                ref.Name=data.at(name).at("name");
                for(unsigned i=0;i<=unsigned(GeometryElementDomain::PointCloudPoint);++i)
                    if(data.at(name).at("domain")==ToString(GeometryElementDomain(i)))ref.Domain=GeometryElementDomain(i);
            };
            read("positions",c.Positions);read("radii",c.Radii);
            c.KNeighbors=data.at("k_neighbors");
            c.GpuQueryBatchSize=data.at("gpu_query_batch_size");c.ScaleFactor=data.at("scale_factor");
            return c;
        }
        Core::Config::EngineConfigSection Section(const PointSpacingConfig& c)
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
        return Json{{"entity",c.StableEntityId},{"backend",ToString(c.Backend)},
                    {"positions",Ref(c.Positions)},{"radii",Ref(c.Radii)},
                    {"k_neighbors",c.KNeighbors},{"gpu_query_batch_size",c.GpuQueryBatchSize},
                    {"scale_factor",c.ScaleFactor}}.dump();
    }
    Core::Config::EngineConfigSectionValidationResult ValidatePointSpacingConfigSection(
        std::string_view payload,std::string_view,std::string_view subject)
    {
        using namespace Core::Config;
        EngineConfigSectionValidationResult result;
        auto reject=[&](std::string message){result.Diagnostics.push_back({.Code=EngineConfigDiagnosticCode::InvalidValue,
            .Subject=std::string(subject),.Message=std::move(message)});return result;};
        auto input=Json::parse(payload,nullptr,false),data=Json::parse(SerializePointSpacingConfig({}));
        if(!input.is_object())return reject("Point spacing config must be an object.");
        for(auto it=input.begin();it!=input.end();++it)
        {
            if(!data.contains(it.key()))return reject("Unknown radii field: "+it.key());
            data[it.key()]=it.value();
        }
        for(auto key:{"entity","k_neighbors","gpu_query_batch_size"})
            if(!data[key].is_number_unsigned() || data[key].get<std::uint64_t>()>std::numeric_limits<std::uint32_t>::max())
                return reject(std::string(key)+" must be an unsigned 32-bit integer.");
        if(data["gpu_query_batch_size"]==0 || data["gpu_query_batch_size"]>16384)
            return reject("GPU query batch size must be 1..16384.");
        if(data["backend"]!="cpu_octree" && data["backend"]!="cpu_lbvh" && data["backend"]!="vulkan_lbvh")return reject("Unknown radii backend.");
        for(auto key:{"scale_factor"})
            if(!data[key].is_number() || !std::isfinite(data[key].get<double>()) || data[key]<0 ||
               data[key].get<double>()>std::numeric_limits<float>::max())return reject(std::string(key)+" must be a finite nonnegative float.");
        if(data["scale_factor"].get<double>()>0 && data["scale_factor"].get<float>()==0)
            return reject("Positive scale factor must remain positive in float storage.");
        const PointSpacingConfig defaults;
        for(auto [key,kind]:{std::pair{"positions",defaults.Positions.ValueKind},std::pair{"radii",defaults.Radii.ValueKind}})
        {
            const auto& ref=data[key];
            if(!ref.is_object() || ref.size()!=3 || !ref.contains("domain") || !ref.contains("name") || !ref.contains("kind") ||
               ref["kind"]!=Kind(kind) || !ref["name"].is_string() || ref["name"].get<std::string>().empty())
                return reject(std::string(key)+" needs a canonical typed property reference.");
            bool found=false;
            for(unsigned i=0;i<=unsigned(GeometryElementDomain::PointCloudPoint);++i)found |= ref["domain"]==ToString(GeometryElementDomain(i));
            if(!found)return reject("Unknown element domain.");
        }
        if(data["positions"]["domain"]!=data["radii"]["domain"] ||
           data["positions"]["name"]==data["radii"]["name"])
            return reject("Position and radii must be distinct properties on the same domain.");
        result.State=EngineConfigState::Valid;result.CanonicalPayloadJson=SerializePointSpacingConfig(Parse(data));result.ParsedFieldCount=input.size();return result;
    }
    std::optional<PointSpacingConfig> GetPointSpacingConfig(const Core::Config::EngineConfig& c)
    {
        const auto* section=Core::Config::FindEngineConfigSection(c.AppSections,kPointSpacingConfigSectionName);
        if(!section || section->SchemaId!=kPointSpacingConfigSectionSchemaId || section->SchemaVersion!=1)return {};
        auto validation=ValidatePointSpacingConfigSection(section->PayloadJson,{},kPointSpacingConfigSectionName);
        if(!validation.Usable())return {};
        return Parse(Json::parse(validation.CanonicalPayloadJson));
    }
    void SetPointSpacingConfig(Core::Config::EngineConfig& c,const PointSpacingConfig& value)
    {Core::Config::UpsertEngineConfigSection(c.AppSections,Section(value));}
    Core::Config::EngineConfigSectionRegistration MakePointSpacingConfigSectionRegistration()
    {return {.DefaultSection=Section({}),.Validate=ValidatePointSpacingConfigSection};}
}
