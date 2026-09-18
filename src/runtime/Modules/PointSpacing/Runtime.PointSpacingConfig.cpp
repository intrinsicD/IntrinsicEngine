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
        result.State=EngineConfigState::Valid;result.CanonicalPayloadJson=SerializePointSpacingConfig(Parse(data));result.ParsedFieldCount=input.size();return result;
    }
    std::optional<PointSpacingConfig> GetPointSpacingConfig(const Core::Config::EngineConfig& c)
    {
        const auto payload = ConfigDetail::FindValidatedCanonicalPayload(
            c, kPointSpacingConfigSectionName, kPointSpacingConfigSectionSchemaId, 1u,
            nullptr, ValidatePointSpacingConfigSection);
        if (!payload) return {};
        return Parse(ConfigDetail::ParseConfigJson(*payload, true));
    }
    void SetPointSpacingConfig(Core::Config::EngineConfig& c,const PointSpacingConfig& value)
    {Core::Config::UpsertEngineConfigSection(c.AppSections,Section(value));}
    Core::Config::EngineConfigSectionRegistration MakePointSpacingConfigSectionRegistration()
    {return {.DefaultSection=Section({}),.Validate=ValidatePointSpacingConfigSection};}
}
