module;
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <nlohmann/json.hpp>
module Extrinsic.Runtime.BilateralFilterConfig;
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
        BilateralFilterConfig Parse(const Json& data)
        {
            BilateralFilterConfig c;c.StableEntityId=data.at("entity");
            for(unsigned i=0;i<3;++i)if(data.at("backend")==ToString(BilateralFilterBackend(i)))c.Backend=BilateralFilterBackend(i);
            auto read=[&](const char* name,GeometryPropertyRef& ref){
                ref.Name=data.at(name).at("name");
                for(unsigned i=0;i<=unsigned(GeometryElementDomain::PointCloudPoint);++i)
                    if(data.at(name).at("domain")==ToString(GeometryElementDomain(i)))ref.Domain=GeometryElementDomain(i);
            };
            read("positions",c.Positions);read("normals",c.Normals);read("output",c.Output);
            c.KNeighbors=data.at("k_neighbors");
            c.GpuQueryBatchSize=data.at("gpu_query_batch_size");c.SpatialSigma=data.at("spatial_sigma");c.NormalSigma=data.at("normal_sigma");c.Iterations=data.at("iterations");
            return c;
        }
        Core::Config::EngineConfigSection Section(const BilateralFilterConfig& c)
        {
            return {.Name=std::string(kBilateralFilterConfigSectionName),.SchemaId=std::string(kBilateralFilterConfigSectionSchemaId),
                    .SchemaVersion=1,.PayloadJson=SerializeBilateralFilterConfig(c)};
        }
    }
    const char* ToString(BilateralFilterBackend b) noexcept
    {
        switch(b){case BilateralFilterBackend::CpuOctree:return "cpu_octree";case BilateralFilterBackend::CpuLBVH:return "cpu_lbvh";case BilateralFilterBackend::VulkanLBVH:return "vulkan_lbvh";}
        return "invalid";
    }
    std::string SerializeBilateralFilterConfig(const BilateralFilterConfig& c)
    {
        return Json{{"entity",c.StableEntityId},{"backend",ToString(c.Backend)},
                    {"positions",Ref(c.Positions)},{"normals",Ref(c.Normals)},{"output",Ref(c.Output)},
                    {"k_neighbors",c.KNeighbors},{"gpu_query_batch_size",c.GpuQueryBatchSize},
                    {"spatial_sigma",c.SpatialSigma},{"normal_sigma",c.NormalSigma},{"iterations",c.Iterations}}.dump();
    }
    Core::Config::EngineConfigSectionValidationResult ValidateBilateralFilterConfigSection(
        std::string_view payload,std::string_view,std::string_view subject)
    {
        using namespace Core::Config;
        EngineConfigSectionValidationResult result;
        auto reject=[&](std::string message){result.Diagnostics.push_back({.Code=EngineConfigDiagnosticCode::InvalidValue,
            .Subject=std::string(subject),.Message=std::move(message)});return result;};
        auto input=Json::parse(payload,nullptr,false),data=Json::parse(SerializeBilateralFilterConfig({}));
        if(!input.is_object())return reject("Bilateral filter config must be an object.");
        for(auto it=input.begin();it!=input.end();++it)
        {
            if(!data.contains(it.key()))return reject("Unknown output field: "+it.key());
            data[it.key()]=it.value();
        }
        for(auto key:{"entity","k_neighbors","gpu_query_batch_size","iterations"})
            if(!data[key].is_number_unsigned() || data[key].get<std::uint64_t>()>std::numeric_limits<std::uint32_t>::max())
                return reject(std::string(key)+" must be an unsigned 32-bit integer.");
        if(data["gpu_query_batch_size"]==0 || data["gpu_query_batch_size"]>16384)
            return reject("GPU query batch size must be 1..16384.");
        if(data["backend"]!="cpu_octree" && data["backend"]!="cpu_lbvh" && data["backend"]!="vulkan_lbvh")return reject("Unknown output backend.");
        for(auto key:{"spatial_sigma","normal_sigma"})
            if(!data[key].is_number() || !std::isfinite(data[key].get<double>()) || data[key]<0 ||
               data[key].get<double>()>std::numeric_limits<float>::max())return reject(std::string(key)+" must be a finite nonnegative float.");
        if(data["iterations"]>100) return reject("Bilateral iterations must be 0..100.");
        for(auto key:{"spatial_sigma","normal_sigma"})
            if(data[key].get<double>()>0 && data[key].get<float>()==0)
                return reject(std::string(key)+" must remain positive in float storage.");
        const BilateralFilterConfig defaults;
        for(auto [key,kind]:{std::pair{"positions",defaults.Positions.ValueKind},std::pair{"normals",defaults.Normals.ValueKind},std::pair{"output",defaults.Output.ValueKind}})
        {
            const auto& ref=data[key];
            if(!ref.is_object() || ref.size()!=3 || !ref.contains("domain") || !ref.contains("name") || !ref.contains("kind") ||
               ref["kind"]!=Kind(kind) || !ref["name"].is_string() || ref["name"].get<std::string>().empty())
                return reject(std::string(key)+" needs a canonical typed property reference.");
            bool found=false;
            for(unsigned i=0;i<=unsigned(GeometryElementDomain::PointCloudPoint);++i)found |= ref["domain"]==ToString(GeometryElementDomain(i));
            if(!found)return reject("Unknown element domain.");
        }
        if(data["positions"]["domain"]!=data["output"]["domain"] || data["positions"]["domain"]!=data["normals"]["domain"])
            return reject("Positions, normals and output must share an element domain.");
        if(data["output"]["name"]==data["normals"]["name"] && data["positions"]["name"]!=data["normals"]["name"])
            return reject("Output cannot overwrite a distinct normal input.");
        result.State=EngineConfigState::Valid;result.CanonicalPayloadJson=SerializeBilateralFilterConfig(Parse(data));result.ParsedFieldCount=input.size();return result;
    }
    std::optional<BilateralFilterConfig> GetBilateralFilterConfig(const Core::Config::EngineConfig& c)
    {
        const auto* section=Core::Config::FindEngineConfigSection(c.AppSections,kBilateralFilterConfigSectionName);
        if(!section || section->SchemaId!=kBilateralFilterConfigSectionSchemaId || section->SchemaVersion!=1)return {};
        auto validation=ValidateBilateralFilterConfigSection(section->PayloadJson,{},kBilateralFilterConfigSectionName);
        if(!validation.Usable())return {};
        return Parse(Json::parse(validation.CanonicalPayloadJson));
    }
    void SetBilateralFilterConfig(Core::Config::EngineConfig& c,const BilateralFilterConfig& value)
    {Core::Config::UpsertEngineConfigSection(c.AppSections,Section(value));}
    Core::Config::EngineConfigSectionRegistration MakeBilateralFilterConfigSectionRegistration()
    {return {.DefaultSection=Section({}),.Validate=ValidateBilateralFilterConfigSection};}
}
