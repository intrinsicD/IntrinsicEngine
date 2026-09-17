module;
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
module Extrinsic.Runtime.OutlierAnalysisConfig;
#include "Config/internal/Runtime.PointConfigJson.hpp"
namespace Extrinsic::Runtime
{
    namespace
    {
        using Json=nlohmann::json;
        OutlierAnalysisConfig Parse(const Json& data)
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
                    {"operation",ToString(c.Operation)},{"positions",ConfigDetail::EncodePointPropertyRef(c.Positions)},{"mask",ConfigDetail::EncodePointPropertyRef(c.Mask)},{"score",ConfigDetail::EncodePointPropertyRef(c.Score)},
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
        if (auto error = ConfigDetail::ValidatePointConfigFields(
            input, data, "Outlier analysis config must be an object.", "Unknown outlier field: ",
            {"entity", "k_neighbors", "minimum_neighbors", "gpu_query_batch_size"}))
            return reject(std::move(*error));
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
            const auto validation = ConfigDetail::ValidatePointPropertyRef(data[key], kind);
            if(validation == ConfigDetail::PointPropertyValidation::InvalidReference)
                return reject(std::string(key)+" needs a canonical typed property reference.");
            if(validation == ConfigDetail::PointPropertyValidation::UnknownDomain)
                return reject("Unknown element domain.");
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
