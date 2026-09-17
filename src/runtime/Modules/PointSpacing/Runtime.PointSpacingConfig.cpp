module;
#include <cstdint>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
module Extrinsic.Runtime.PointSpacingConfig;
#include "Config/internal/Runtime.PointConfigJson.hpp"
namespace Extrinsic::Runtime
{
    namespace
    {
        using Json=nlohmann::json;
        PointSpacingConfig Parse(const Json& data)
        {
            PointSpacingConfig c;c.StableEntityId=data.at("entity");
            for(unsigned i=0;i<3;++i)if(data.at("backend")==ToString(PointSpacingBackend(i)))c.Backend=PointSpacingBackend(i);
            ConfigDetail::DecodePointPropertyRef(data.at("positions"),c.Positions);ConfigDetail::DecodePointPropertyRef(data.at("radii"),c.Radii);
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
                    {"positions",ConfigDetail::EncodePointPropertyRef(c.Positions)},{"radii",ConfigDetail::EncodePointPropertyRef(c.Radii)},
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
        if (auto error = ConfigDetail::ValidatePointConfigFields(
            input, data, "Point spacing config must be an object.", "Unknown radii field: ",
            {"entity", "k_neighbors", "gpu_query_batch_size"}))
            return reject(std::move(*error));
        if(data["gpu_query_batch_size"]==0 || data["gpu_query_batch_size"]>16384)
            return reject("GPU query batch size must be 1..16384.");
        if(data["backend"]!="cpu_octree" && data["backend"]!="cpu_lbvh" && data["backend"]!="vulkan_lbvh")return reject("Unknown radii backend.");
        if (auto error = ConfigDetail::ValidatePointConfigNonnegativeFloats(
            data, {"scale_factor"}))
            return reject(std::move(*error));
        if(data["scale_factor"].get<double>()>0 && data["scale_factor"].get<float>()==0)
            return reject("Positive scale factor must remain positive in float storage.");
        const PointSpacingConfig defaults;
        for(auto [key,kind]:{std::pair{"positions",defaults.Positions.ValueKind},std::pair{"radii",defaults.Radii.ValueKind}})
        {
            const auto validation = ConfigDetail::ValidatePointPropertyRef(data[key], kind);
            if(validation == ConfigDetail::PointPropertyValidation::InvalidReference)
                return reject(std::string(key)+" needs a canonical typed property reference.");
            if(validation == ConfigDetail::PointPropertyValidation::UnknownDomain)
                return reject("Unknown element domain.");
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
