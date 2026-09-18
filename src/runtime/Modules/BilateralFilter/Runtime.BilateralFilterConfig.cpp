module;
#include <cstdint>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
module Extrinsic.Runtime.BilateralFilterConfig;
#include "Config/internal/Runtime.PointConfigJson.hpp"
namespace Extrinsic::Runtime
{
    namespace
    {
        using Json=nlohmann::json;
        BilateralFilterConfig Parse(const Json& data)
        {
            BilateralFilterConfig c;c.StableEntityId=data.at("entity");
            for(unsigned i=0;i<3;++i)if(data.at("backend")==ToString(BilateralFilterBackend(i)))c.Backend=BilateralFilterBackend(i);
            ConfigDetail::DecodePointPropertyRef(data.at("positions"),c.Positions);ConfigDetail::DecodePointPropertyRef(data.at("normals"),c.Normals);ConfigDetail::DecodePointPropertyRef(data.at("output"),c.Output);
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
        return ConfigDetail::SerializeConfigJson(Json{{"entity",c.StableEntityId},{"backend",ToString(c.Backend)},
                    {"positions",ConfigDetail::EncodePointPropertyRef(c.Positions)},{"normals",ConfigDetail::EncodePointPropertyRef(c.Normals)},{"output",ConfigDetail::EncodePointPropertyRef(c.Output)},
                    {"k_neighbors",c.KNeighbors},{"gpu_query_batch_size",c.GpuQueryBatchSize},
                    {"spatial_sigma",c.SpatialSigma},{"normal_sigma",c.NormalSigma},{"iterations",c.Iterations}});
    }
    Core::Config::EngineConfigSectionValidationResult ValidateBilateralFilterConfigSection(
        std::string_view payload,std::string_view,std::string_view subject)
    {
        using namespace Core::Config;
        using ConfigDetail::RejectConfigSection;
        EngineConfigSectionValidationResult result;
        auto input=ConfigDetail::ParseConfigJson(payload, false),data=ConfigDetail::ParseConfigJson(SerializeBilateralFilterConfig({}), true);
        if (auto error = ConfigDetail::ValidatePointConfigFields(
            input, data, "Bilateral filter config must be an object.", "Unknown output field: ",
            {"entity", "k_neighbors", "gpu_query_batch_size", "iterations"}))
            return RejectConfigSection(subject, std::move(*error));
        if(data["gpu_query_batch_size"]==0 || data["gpu_query_batch_size"]>16384)
            return RejectConfigSection(subject, "GPU query batch size must be 1..16384.");
        if(data["backend"]!="cpu_octree" && data["backend"]!="cpu_lbvh" && data["backend"]!="vulkan_lbvh")return RejectConfigSection(subject, "Unknown output backend.");
        if (auto error = ConfigDetail::ValidatePointConfigNonnegativeFloats(
            data, {"spatial_sigma", "normal_sigma"}))
            return RejectConfigSection(subject, std::move(*error));
        if(data["iterations"]>100) return RejectConfigSection(subject, "Bilateral iterations must be 0..100.");
        for(auto key:{"spatial_sigma","normal_sigma"})
            if(data[key].get<double>()>0 && data[key].get<float>()==0)
                return RejectConfigSection(subject, std::string(key)+" must remain positive in float storage.");
        const BilateralFilterConfig defaults;
        if (auto error = ConfigDetail::ValidatePointConfigPropertyRefs(
            data, {{"positions", defaults.Positions.ValueKind}, {"normals", defaults.Normals.ValueKind}, {"output", defaults.Output.ValueKind}}))
            return RejectConfigSection(subject, std::move(*error));
        if(data["positions"]["domain"]!=data["output"]["domain"] || data["positions"]["domain"]!=data["normals"]["domain"])
            return RejectConfigSection(subject, "Positions, normals and output must share an element domain.");
        if(data["output"]["name"]==data["normals"]["name"] && data["positions"]["name"]!=data["normals"]["name"])
            return RejectConfigSection(subject, "Output cannot overwrite a distinct normal input.");
        result.State=EngineConfigState::Valid;result.CanonicalPayloadJson=SerializeBilateralFilterConfig(Parse(data));result.ParsedFieldCount=input.size();return result;
    }
    std::optional<BilateralFilterConfig> GetBilateralFilterConfig(const Core::Config::EngineConfig& c)
    {
        const auto payload = ConfigDetail::FindValidatedCanonicalPayload(
            c, kBilateralFilterConfigSectionName, kBilateralFilterConfigSectionSchemaId, 1u,
            nullptr, ValidateBilateralFilterConfigSection);
        if (!payload) return {};
        return Parse(ConfigDetail::ParseConfigJson(*payload, true));
    }
    void SetBilateralFilterConfig(Core::Config::EngineConfig& c,const BilateralFilterConfig& value)
    {Core::Config::UpsertEngineConfigSection(c.AppSections,Section(value));}
    Core::Config::EngineConfigSectionRegistration MakeBilateralFilterConfigSectionRegistration()
    {return {.DefaultSection=Section({}),.Validate=ValidateBilateralFilterConfigSection};}
}
