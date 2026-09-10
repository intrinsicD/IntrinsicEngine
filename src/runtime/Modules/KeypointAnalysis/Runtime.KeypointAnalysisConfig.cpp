module;
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <nlohmann/json.hpp>
module Extrinsic.Runtime.KeypointAnalysisConfig;
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
        KeypointAnalysisConfig Parse(const Json& data)
        {
            KeypointAnalysisConfig c;c.StableEntityId=data.at("entity");
            for(unsigned i=0;i<3;++i)if(data.at("backend")==ToString(KeypointAnalysisBackend(i)))c.Backend=KeypointAnalysisBackend(i);
            auto read=[&](const char* name,GeometryPropertyRef& ref){
                ref.Name=data.at(name).at("name");
                for(unsigned i=0;i<=unsigned(GeometryElementDomain::PointCloudPoint);++i)
                    if(data.at(name).at("domain")==ToString(GeometryElementDomain(i)))ref.Domain=GeometryElementDomain(i);
            };
            read("positions",c.Positions);read("mask",c.Mask);read("score",c.Score);
            c.MinimumNeighbors=data.at("minimum_neighbors");c.GpuQueryBatchSize=data.at("gpu_query_batch_size");
            c.GpuRadiusCapacity=data.at("gpu_radius_capacity");c.SalientRadius=data.at("salient_radius");
            c.NonMaxRadius=data.at("nonmax_radius");c.Gamma21=data.at("gamma21");c.Gamma32=data.at("gamma32");
            return c;
        }
        Core::Config::EngineConfigSection Section(const KeypointAnalysisConfig& c)
        {
            return {.Name=std::string(kKeypointAnalysisConfigSectionName),.SchemaId=std::string(kKeypointAnalysisConfigSectionSchemaId),
                    .SchemaVersion=1,.PayloadJson=SerializeKeypointAnalysisConfig(c)};
        }
    }

    const char* ToString(KeypointAnalysisBackend b) noexcept
    {
        switch(b){case KeypointAnalysisBackend::CpuKDTree:return "cpu_kdtree";case KeypointAnalysisBackend::CpuLBVH:return "cpu_lbvh";case KeypointAnalysisBackend::VulkanLBVH:return "vulkan_lbvh";}
        return "invalid";
    }

    std::string SerializeKeypointAnalysisConfig(const KeypointAnalysisConfig& c)
    {
        return Json{{"entity",c.StableEntityId},{"backend",ToString(c.Backend)},
            {"positions",Ref(c.Positions)},{"mask",Ref(c.Mask)},{"score",Ref(c.Score)},
            {"minimum_neighbors",c.MinimumNeighbors},{"gpu_query_batch_size",c.GpuQueryBatchSize},
            {"gpu_radius_capacity",c.GpuRadiusCapacity},{"salient_radius",c.SalientRadius},
            {"nonmax_radius",c.NonMaxRadius},{"gamma21",c.Gamma21},{"gamma32",c.Gamma32}}.dump();
    }
    Core::Config::EngineConfigSectionValidationResult ValidateKeypointAnalysisConfigSection(
        std::string_view payload,std::string_view,std::string_view subject)
    {
        using namespace Core::Config;
        EngineConfigSectionValidationResult result;
        auto reject=[&](std::string message){result.Diagnostics.push_back({.Code=EngineConfigDiagnosticCode::InvalidValue,
            .Subject=std::string(subject),.Message=std::move(message)});return result;};
        auto input=Json::parse(payload,nullptr,false),data=Json::parse(SerializeKeypointAnalysisConfig({}));
        if(!input.is_object())return reject("Keypoint analysis config must be an object.");
        for(auto it=input.begin();it!=input.end();++it)
        {
            if(!data.contains(it.key()))return reject("Unknown keypoint field: "+it.key());
            data[it.key()]=it.value();
        }
        for(auto key:{"entity","minimum_neighbors","gpu_query_batch_size","gpu_radius_capacity"})
            if(!data[key].is_number_unsigned() || data[key].get<std::uint64_t>()>std::numeric_limits<std::uint32_t>::max())
                return reject(std::string(key)+" must be an unsigned 32-bit integer.");
        if(data["gpu_query_batch_size"]==0 || data["gpu_query_batch_size"]>16384 ||
           data["gpu_radius_capacity"]==0 || data["gpu_radius_capacity"]>1024)
            return reject("GPU query batch must be 1..16384 and complete radius capacity 1..1024.");
        if(data["backend"]!="cpu_kdtree" && data["backend"]!="cpu_lbvh" && data["backend"]!="vulkan_lbvh")
            return reject("Unknown keypoint backend.");
        for(auto key:{"salient_radius","nonmax_radius"})
            if(!data[key].is_number() || !std::isfinite(data[key].get<double>()) || data[key]<0 ||
               data[key].get<double>()>std::numeric_limits<float>::max() ||
               (data[key]>0 && data[key].get<float>()==0))
                return reject(std::string(key)+" must be zero (automatic) or a positive representable float.");
        for(auto key:{"gamma21","gamma32"})
            if(!data[key].is_number() || !std::isfinite(data[key].get<double>()) || data[key]<0 || data[key]>1)
                return reject(std::string(key)+" must be finite in [0,1].");
        const KeypointAnalysisConfig defaults;
        for(auto [key,kind]:{std::pair{"positions",defaults.Positions.ValueKind},std::pair{"mask",defaults.Mask.ValueKind},std::pair{"score",defaults.Score.ValueKind}})
        {
            const auto& ref=data[key];
            if(!ref.is_object() || ref.size()!=3 || !ref.contains("domain") || !ref.contains("name") || !ref.contains("kind") ||
               ref["kind"]!=Kind(kind) || !ref["name"].is_string() || ref["name"].get<std::string>().empty())
                return reject(std::string(key)+" needs a canonical typed property reference.");
            bool found=false;
            for(unsigned i=0;i<=unsigned(GeometryElementDomain::PointCloudPoint);++i)found |= ref["domain"]==ToString(GeometryElementDomain(i));
            if(!found)return reject("Unknown element domain.");
        }
        if(data["positions"]["domain"]!=data["mask"]["domain"] || data["mask"]["domain"]!=data["score"]["domain"] ||
           data["positions"]["name"]==data["mask"]["name"] || data["positions"]["name"]==data["score"]["name"] || data["mask"]["name"]==data["score"]["name"])
            return reject("Position, mask and score must be distinct properties on the same domain.");
        result.State=EngineConfigState::Valid;result.CanonicalPayloadJson=SerializeKeypointAnalysisConfig(Parse(data));result.ParsedFieldCount=input.size();return result;
    }
    std::optional<KeypointAnalysisConfig> GetKeypointAnalysisConfig(const Core::Config::EngineConfig& c)
    {
        const auto* section=Core::Config::FindEngineConfigSection(c.AppSections,kKeypointAnalysisConfigSectionName);
        if(!section || section->SchemaId!=kKeypointAnalysisConfigSectionSchemaId || section->SchemaVersion!=1)return {};
        auto validation=ValidateKeypointAnalysisConfigSection(section->PayloadJson,{},kKeypointAnalysisConfigSectionName);
        if(!validation.Usable())return {};
        return Parse(Json::parse(validation.CanonicalPayloadJson));
    }
    void SetKeypointAnalysisConfig(Core::Config::EngineConfig& c,const KeypointAnalysisConfig& value)
    {Core::Config::UpsertEngineConfigSection(c.AppSections,Section(value));}
    Core::Config::EngineConfigSectionRegistration MakeKeypointAnalysisConfigSectionRegistration()
    {return {.DefaultSection=Section({}),.Validate=ValidateKeypointAnalysisConfigSection};}
}
