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
module Extrinsic.Runtime.DescriptorAnalysisConfig;
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
        DescriptorAnalysisConfig Parse(const Json& data)
        {
            DescriptorAnalysisConfig c;c.StableEntityId=data.at("entity");
            for(unsigned i=0;i<3;++i)if(data.at("backend")==ToString(DescriptorAnalysisBackend(i)))c.Backend=DescriptorAnalysisBackend(i);
            auto read=[&](const Json& value,GeometryPropertyRef& ref){
                ref.Name=value.at("name");
                for(unsigned i=0;i<=unsigned(GeometryElementDomain::PointCloudPoint);++i)
                    if(value.at("domain")==ToString(GeometryElementDomain(i)))ref.Domain=GeometryElementDomain(i);
            };
            read(data.at("positions"),c.Positions);read(data.at("normals"),c.Normals);
            for(unsigned i=0;i<33;++i)read(data.at("outputs").at(i),c.Outputs[i]);
            c.MaxNeighbors=data.at("max_neighbors");c.GpuQueryBatchSize=data.at("gpu_query_batch_size");
            c.GpuRadiusCapacity=data.at("gpu_radius_capacity");c.FeatureRadius=data.at("feature_radius");
            return c;
        }
        Core::Config::EngineConfigSection Section(const DescriptorAnalysisConfig& c)
        {
            return {.Name=std::string(kDescriptorAnalysisConfigSectionName),.SchemaId=std::string(kDescriptorAnalysisConfigSectionSchemaId),
                    .SchemaVersion=1,.PayloadJson=SerializeDescriptorAnalysisConfig(c)};
        }
    }

    std::array<GeometryPropertyRef,33> MakeDescriptorOutputProperties(GeometryElementDomain domain,std::string_view prefix)
    {
        std::array<GeometryPropertyRef,33> outputs;
        constexpr std::array names{"alpha","phi","theta"};
        for(unsigned i=0;i<33;++i)outputs[i]={domain,std::string(prefix)+"."+names[i/11]+std::to_string(i%11),Geometry::PropertyValueKind::Float};
        return outputs;
    }

    const char* ToString(DescriptorAnalysisBackend b) noexcept
    {
        switch(b){case DescriptorAnalysisBackend::CpuKDTree:return "cpu_kdtree";case DescriptorAnalysisBackend::CpuLBVH:return "cpu_lbvh";case DescriptorAnalysisBackend::VulkanLBVH:return "vulkan_lbvh";}
        return "invalid";
    }

    std::string SerializeDescriptorAnalysisConfig(const DescriptorAnalysisConfig& c)
    {
        auto outputs=Json::array();for(const auto& output:c.Outputs)outputs.push_back(Ref(output));
        return Json{{"entity",c.StableEntityId},{"backend",ToString(c.Backend)},
            {"positions",Ref(c.Positions)},{"normals",Ref(c.Normals)},{"outputs",outputs},
            {"max_neighbors",c.MaxNeighbors},{"gpu_query_batch_size",c.GpuQueryBatchSize},
            {"gpu_radius_capacity",c.GpuRadiusCapacity},{"feature_radius",c.FeatureRadius}}.dump();
    }
    Core::Config::EngineConfigSectionValidationResult ValidateDescriptorAnalysisConfigSection(
        std::string_view payload,std::string_view,std::string_view subject)
    {
        using namespace Core::Config;
        EngineConfigSectionValidationResult result;
        auto reject=[&](std::string message){result.Diagnostics.push_back({.Code=EngineConfigDiagnosticCode::InvalidValue,
            .Subject=std::string(subject),.Message=std::move(message)});return result;};
        auto input=Json::parse(payload,nullptr,false),data=Json::parse(SerializeDescriptorAnalysisConfig({}));
        if(!input.is_object())return reject("Descriptor analysis config must be an object.");
        for(auto it=input.begin();it!=input.end();++it)
        {
            if(!data.contains(it.key()))return reject("Unknown descriptor field: "+it.key());
            data[it.key()]=it.value();
        }
        for(auto key:{"entity","max_neighbors","gpu_query_batch_size","gpu_radius_capacity"})
            if(!data[key].is_number_unsigned() || data[key].get<std::uint64_t>()>std::numeric_limits<std::uint32_t>::max())
                return reject(std::string(key)+" must be an unsigned 32-bit integer.");
        if(data["gpu_query_batch_size"]==0 || data["gpu_query_batch_size"]>16384 ||
           data["gpu_radius_capacity"]==0 || data["gpu_radius_capacity"]>1024)
            return reject("GPU query batch must be 1..16384 and complete radius capacity 1..1024.");
        if(data["backend"]!="cpu_kdtree" && data["backend"]!="cpu_lbvh" && data["backend"]!="vulkan_lbvh")
            return reject("Unknown descriptor backend.");
        for(auto key:{"feature_radius"})
            if(!data[key].is_number() || !std::isfinite(data[key].get<double>()) || data[key]<0 ||
               data[key].get<double>()>std::numeric_limits<float>::max() ||
               (data[key]>0 && data[key].get<float>()==0))
                return reject(std::string(key)+" must be zero (automatic) or a positive representable float.");
        const auto validRef=[&](const Json& ref,Geometry::PropertyValueKind kind)
        {
            if(!ref.is_object() || ref.size()!=3 || !ref.contains("domain") || !ref.contains("name") || !ref.contains("kind") ||
               ref["kind"]!=Kind(kind) || !ref["name"].is_string() || ref["name"].get<std::string>().empty())return false;
            for(unsigned i=0;i<=unsigned(GeometryElementDomain::PointCloudPoint);++i)
                if(ref["domain"]==ToString(GeometryElementDomain(i)))return true;
            return false;
        };
        if(!validRef(data["positions"],Geometry::PropertyValueKind::Vec3) || !validRef(data["normals"],Geometry::PropertyValueKind::Vec3) ||
           data["positions"]["domain"]!=data["normals"]["domain"])
            return reject("Positions and normals need canonical vec3 references on the same domain.");
        if(!data["outputs"].is_array() || data["outputs"].size()!=33)return reject("FPFH requires exactly 33 float output references.");
        for(unsigned i=0;i<33;++i)
        {
            const auto& ref=data["outputs"][i];
            if(!validRef(ref,Geometry::PropertyValueKind::Float) || ref["domain"]!=data["positions"]["domain"] ||
               ref["name"]==data["positions"]["name"] || ref["name"]==data["normals"]["name"])
                return reject("Descriptor outputs must be float properties on the input domain, distinct from inputs.");
            for(unsigned j=0;j<i;++j)if(ref["name"]==data["outputs"][j]["name"])
                return reject("Descriptor output names must be unique.");
        }
        result.State=EngineConfigState::Valid;result.CanonicalPayloadJson=SerializeDescriptorAnalysisConfig(Parse(data));result.ParsedFieldCount=input.size();return result;
    }
    std::optional<DescriptorAnalysisConfig> GetDescriptorAnalysisConfig(const Core::Config::EngineConfig& c)
    {
        const auto* section=Core::Config::FindEngineConfigSection(c.AppSections,kDescriptorAnalysisConfigSectionName);
        if(!section || section->SchemaId!=kDescriptorAnalysisConfigSectionSchemaId || section->SchemaVersion!=1)return {};
        auto validation=ValidateDescriptorAnalysisConfigSection(section->PayloadJson,{},kDescriptorAnalysisConfigSectionName);
        if(!validation.Usable())return {};
        return Parse(Json::parse(validation.CanonicalPayloadJson));
    }
    void SetDescriptorAnalysisConfig(Core::Config::EngineConfig& c,const DescriptorAnalysisConfig& value)
    {Core::Config::UpsertEngineConfigSection(c.AppSections,Section(value));}
    Core::Config::EngineConfigSectionRegistration MakeDescriptorAnalysisConfigSectionRegistration()
    {return {.DefaultSection=Section({}),.Validate=ValidateDescriptorAnalysisConfigSection};}
}
