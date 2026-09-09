module;
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <nlohmann/json.hpp>
module Extrinsic.Runtime.OutlierAnalysisConfig;
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
        OutlierAnalysisConfig Parse(const Json& data)
        {
            OutlierAnalysisConfig c;c.StableEntityId=data.at("entity");
            for(unsigned i=0;i<3;++i)if(data.at("method")==ToString(OutlierAnalysisMethod(i)))c.Method=OutlierAnalysisMethod(i);
            c.Operation=data.at("operation")=="remove_marked"?OutlierAnalysisOperation::RemoveMarked:OutlierAnalysisOperation::Analyze;
            for(unsigned i=0;i<3;++i)if(data.at("backend")==ToString(OutlierAnalysisBackend(i)))c.Backend=OutlierAnalysisBackend(i);
            auto read=[&](const char* name,GeometryPropertyRef& ref){
                ref.Name=data.at(name).at("name");
                for(unsigned i=0;i<=unsigned(GeometryElementDomain::PointCloudPoint);++i)
                    if(data.at(name).at("domain")==ToString(GeometryElementDomain(i)))ref.Domain=GeometryElementDomain(i);
            };
            read("positions",c.Positions);read("mask",c.Mask);read("score",c.Score);
            c.KNeighbors=data.at("k_neighbors");c.MinimumNeighbors=data.at("minimum_neighbors");
            c.GpuQueryBatchSize=data.at("gpu_query_batch_size");c.Radius=data.at("radius");c.StdDevMultiplier=data.at("stddev_multiplier");c.ScoreThreshold=data.at("score_threshold");
            return c;
        }
        Core::Config::EngineConfigSection Section(const OutlierAnalysisConfig& c)
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
        return Json{{"entity",c.StableEntityId},{"method",ToString(c.Method)},{"backend",ToString(c.Backend)},
                    {"operation",ToString(c.Operation)},{"positions",Ref(c.Positions)},{"mask",Ref(c.Mask)},{"score",Ref(c.Score)},
                    {"k_neighbors",c.KNeighbors},{"minimum_neighbors",c.MinimumNeighbors},{"gpu_query_batch_size",c.GpuQueryBatchSize},
                    {"radius",c.Radius},{"stddev_multiplier",c.StdDevMultiplier},{"score_threshold",c.ScoreThreshold}}.dump();
    }
    Core::Config::EngineConfigSectionValidationResult ValidateOutlierAnalysisConfigSection(
        std::string_view payload,std::string_view,std::string_view subject)
    {
        using namespace Core::Config;
        EngineConfigSectionValidationResult result;
        auto reject=[&](std::string message){result.Diagnostics.push_back({.Code=EngineConfigDiagnosticCode::InvalidValue,
            .Subject=std::string(subject),.Message=std::move(message)});return result;};
        auto input=Json::parse(payload,nullptr,false),data=Json::parse(SerializeOutlierAnalysisConfig({}));
        if(!input.is_object())return reject("Outlier analysis config must be an object.");
        for(auto it=input.begin();it!=input.end();++it)
        {
            if(!data.contains(it.key()))return reject("Unknown outlier field: "+it.key());
            data[it.key()]=it.value();
        }
        for(auto key:{"entity","k_neighbors","minimum_neighbors","gpu_query_batch_size"})
            if(!data[key].is_number_unsigned() || data[key].get<std::uint64_t>()>std::numeric_limits<std::uint32_t>::max())
                return reject(std::string(key)+" must be an unsigned 32-bit integer.");
        if(data["k_neighbors"]==0 || data["gpu_query_batch_size"]==0 || data["gpu_query_batch_size"]>16384)
            return reject("k must be positive and GPU query batch size must be 1..16384.");
        if(data["method"]!="statistical" && data["method"]!="radius" && data["method"]!="local_distance_ratio")return reject("Unknown outlier method.");
        if(data["operation"]!="analyze" && data["operation"]!="remove_marked")return reject("Unknown outlier operation.");
        if(data["backend"]!="cpu_octree" && data["backend"]!="cpu_lbvh" && data["backend"]!="vulkan_lbvh")return reject("Unknown outlier backend.");
        for(auto key:{"radius","stddev_multiplier","score_threshold"})
            if(!data[key].is_number() || !std::isfinite(data[key].get<double>()) || data[key]<0 ||
               data[key].get<double>()>std::numeric_limits<float>::max())return reject(std::string(key)+" must be a finite nonnegative float.");
        if(data["method"]=="radius" && data["radius"]<=0)return reject("Radius must be positive.");
        const OutlierAnalysisConfig defaults;
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
        result.State=EngineConfigState::Valid;result.CanonicalPayloadJson=SerializeOutlierAnalysisConfig(Parse(data));result.ParsedFieldCount=input.size();return result;
    }
    std::optional<OutlierAnalysisConfig> GetOutlierAnalysisConfig(const Core::Config::EngineConfig& c)
    {
        const auto* section=Core::Config::FindEngineConfigSection(c.AppSections,kOutlierAnalysisConfigSectionName);
        if(!section || section->SchemaId!=kOutlierAnalysisConfigSectionSchemaId || section->SchemaVersion!=1)return {};
        auto validation=ValidateOutlierAnalysisConfigSection(section->PayloadJson,{},kOutlierAnalysisConfigSectionName);
        if(!validation.Usable())return {};
        return Parse(Json::parse(validation.CanonicalPayloadJson));
    }
    void SetOutlierAnalysisConfig(Core::Config::EngineConfig& c,const OutlierAnalysisConfig& value)
    {Core::Config::UpsertEngineConfigSection(c.AppSections,Section(value));}
    Core::Config::EngineConfigSectionRegistration MakeOutlierAnalysisConfigSectionRegistration()
    {return {.DefaultSection=Section({}),.Validate=ValidateOutlierAnalysisConfigSection};}
}
