module;
#include <cstdint>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
module Extrinsic.Runtime.KernelDensityConfig;
#include "Config/internal/Runtime.PointConfigJson.hpp"
namespace Extrinsic::Runtime
{
    namespace
    {
        using Json=nlohmann::json;
        KernelDensityConfig Parse(const Json& data)
        {
            KernelDensityConfig c;c.StableEntityId=data.at("entity");
            for(unsigned i=0;i<3;++i)if(data.at("backend")==ToString(KernelDensityBackend(i)))c.Backend=KernelDensityBackend(i);
            ConfigDetail::DecodePointPropertyRef(data.at("positions"),c.Positions);ConfigDetail::DecodePointPropertyRef(data.at("density"),c.Density);
            c.KNeighbors=data.at("k_neighbors");
            c.GpuQueryBatchSize=data.at("gpu_query_batch_size");c.Bandwidth=data.at("bandwidth");
            return c;
        }
        Core::Config::EngineConfigSection Section(const KernelDensityConfig& c)
        {
            return {.Name=std::string(kKernelDensityConfigSectionName),.SchemaId=std::string(kKernelDensityConfigSectionSchemaId),
                    .SchemaVersion=1,.PayloadJson=SerializeKernelDensityConfig(c)};
        }
    }
    const char* ToString(KernelDensityBackend b) noexcept
    {
        switch(b){case KernelDensityBackend::CpuOctree:return "cpu_octree";case KernelDensityBackend::CpuLBVH:return "cpu_lbvh";case KernelDensityBackend::VulkanLBVH:return "vulkan_lbvh";}
        return "invalid";
    }
    std::string SerializeKernelDensityConfig(const KernelDensityConfig& c)
    {
        return ConfigDetail::SerializeConfigJson(Json{{"entity",c.StableEntityId},{"backend",ToString(c.Backend)},
                    {"positions",ConfigDetail::EncodePointPropertyRef(c.Positions)},{"density",ConfigDetail::EncodePointPropertyRef(c.Density)},
                    {"k_neighbors",c.KNeighbors},{"gpu_query_batch_size",c.GpuQueryBatchSize},
                    {"bandwidth",c.Bandwidth}});
    }
    Core::Config::EngineConfigSectionValidationResult ValidateKernelDensityConfigSection(
        std::string_view payload,std::string_view,std::string_view subject)
    {
        using namespace Core::Config;
        using ConfigDetail::RejectConfigSection;
        EngineConfigSectionValidationResult result;
        auto input=ConfigDetail::ParseConfigJson(payload, false),data=ConfigDetail::ParseConfigJson(SerializeKernelDensityConfig({}), true);
        if (auto error = ConfigDetail::ValidatePointConfigFields(
            input, data, "Kernel density config must be an object.", "Unknown density field: ",
            {"entity", "k_neighbors", "gpu_query_batch_size"}))
            return RejectConfigSection(subject, std::move(*error));
        if(data["gpu_query_batch_size"]==0 || data["gpu_query_batch_size"]>16384)
            return RejectConfigSection(subject, "GPU query batch size must be 1..16384.");
        if(data["backend"]!="cpu_octree" && data["backend"]!="cpu_lbvh" && data["backend"]!="vulkan_lbvh")return RejectConfigSection(subject, "Unknown density backend.");
        if (auto error = ConfigDetail::ValidatePointConfigNonnegativeFloats(
            data, {"bandwidth"}))
            return RejectConfigSection(subject, std::move(*error));
        if(data["bandwidth"].get<double>()>0 && data["bandwidth"].get<float>()==0)
            return RejectConfigSection(subject, "Positive bandwidth must remain positive in float storage.");
        const KernelDensityConfig defaults;
        if (auto error = ConfigDetail::ValidatePointConfigPropertyRefs(
            data, {{"positions", defaults.Positions.ValueKind}, {"density", defaults.Density.ValueKind}}))
            return RejectConfigSection(subject, std::move(*error));
        if(data["positions"]["domain"]!=data["density"]["domain"] ||
           data["positions"]["name"]==data["density"]["name"])
            return RejectConfigSection(subject, "Position and density must be distinct properties on the same domain.");
        result.State=EngineConfigState::Valid;result.CanonicalPayloadJson=SerializeKernelDensityConfig(Parse(data));result.ParsedFieldCount=input.size();return result;
    }
    std::optional<KernelDensityConfig> GetKernelDensityConfig(const Core::Config::EngineConfig& c)
    {
        const auto payload = ConfigDetail::FindValidatedCanonicalPayload(
            c, kKernelDensityConfigSectionName, kKernelDensityConfigSectionSchemaId, 1u,
            nullptr, ValidateKernelDensityConfigSection);
        if (!payload) return {};
        return Parse(ConfigDetail::ParseConfigJson(*payload, true));
    }
    void SetKernelDensityConfig(Core::Config::EngineConfig& c,const KernelDensityConfig& value)
    {Core::Config::UpsertEngineConfigSection(c.AppSections,Section(value));}
    Core::Config::EngineConfigSectionRegistration MakeKernelDensityConfigSectionRegistration()
    {return {.DefaultSection=Section({}),.Validate=ValidateKernelDensityConfigSection};}
}
