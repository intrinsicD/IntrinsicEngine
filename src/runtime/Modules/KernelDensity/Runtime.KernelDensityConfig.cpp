module;
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <nlohmann/json.hpp>
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
        return Json{{"entity",c.StableEntityId},{"backend",ToString(c.Backend)},
                    {"positions",ConfigDetail::EncodePointPropertyRef(c.Positions)},{"density",ConfigDetail::EncodePointPropertyRef(c.Density)},
                    {"k_neighbors",c.KNeighbors},{"gpu_query_batch_size",c.GpuQueryBatchSize},
                    {"bandwidth",c.Bandwidth}}.dump();
    }
    Core::Config::EngineConfigSectionValidationResult ValidateKernelDensityConfigSection(
        std::string_view payload,std::string_view,std::string_view subject)
    {
        using namespace Core::Config;
        EngineConfigSectionValidationResult result;
        auto reject=[&](std::string message){result.Diagnostics.push_back({.Code=EngineConfigDiagnosticCode::InvalidValue,
            .Subject=std::string(subject),.Message=std::move(message)});return result;};
        auto input=Json::parse(payload,nullptr,false),data=Json::parse(SerializeKernelDensityConfig({}));
        if(!input.is_object())return reject("Kernel density config must be an object.");
        for(auto it=input.begin();it!=input.end();++it)
        {
            if(!data.contains(it.key()))return reject("Unknown density field: "+it.key());
            data[it.key()]=it.value();
        }
        for(auto key:{"entity","k_neighbors","gpu_query_batch_size"})
            if(!data[key].is_number_unsigned() || data[key].get<std::uint64_t>()>std::numeric_limits<std::uint32_t>::max())
                return reject(std::string(key)+" must be an unsigned 32-bit integer.");
        if(data["gpu_query_batch_size"]==0 || data["gpu_query_batch_size"]>16384)
            return reject("GPU query batch size must be 1..16384.");
        if(data["backend"]!="cpu_octree" && data["backend"]!="cpu_lbvh" && data["backend"]!="vulkan_lbvh")return reject("Unknown density backend.");
        for(auto key:{"bandwidth"})
            if(!data[key].is_number() || !std::isfinite(data[key].get<double>()) || data[key]<0 ||
               data[key].get<double>()>std::numeric_limits<float>::max())return reject(std::string(key)+" must be a finite nonnegative float.");
        if(data["bandwidth"].get<double>()>0 && data["bandwidth"].get<float>()==0)
            return reject("Positive bandwidth must remain positive in float storage.");
        const KernelDensityConfig defaults;
        for(auto [key,kind]:{std::pair{"positions",defaults.Positions.ValueKind},std::pair{"density",defaults.Density.ValueKind}})
        {
            const auto validation = ConfigDetail::ValidatePointPropertyRef(data[key], kind);
            if(validation == ConfigDetail::PointPropertyValidation::InvalidReference)
                return reject(std::string(key)+" needs a canonical typed property reference.");
            if(validation == ConfigDetail::PointPropertyValidation::UnknownDomain)
                return reject("Unknown element domain.");
        }
        if(data["positions"]["domain"]!=data["density"]["domain"] ||
           data["positions"]["name"]==data["density"]["name"])
            return reject("Position and density must be distinct properties on the same domain.");
        result.State=EngineConfigState::Valid;result.CanonicalPayloadJson=SerializeKernelDensityConfig(Parse(data));result.ParsedFieldCount=input.size();return result;
    }
    std::optional<KernelDensityConfig> GetKernelDensityConfig(const Core::Config::EngineConfig& c)
    {
        const auto* section=Core::Config::FindEngineConfigSection(c.AppSections,kKernelDensityConfigSectionName);
        if(!section || section->SchemaId!=kKernelDensityConfigSectionSchemaId || section->SchemaVersion!=1)return {};
        auto validation=ValidateKernelDensityConfigSection(section->PayloadJson,{},kKernelDensityConfigSectionName);
        if(!validation.Usable())return {};
        return Parse(Json::parse(validation.CanonicalPayloadJson));
    }
    void SetKernelDensityConfig(Core::Config::EngineConfig& c,const KernelDensityConfig& value)
    {Core::Config::UpsertEngineConfigSection(c.AppSections,Section(value));}
    Core::Config::EngineConfigSectionRegistration MakeKernelDensityConfigSectionRegistration()
    {return {.DefaultSection=Section({}),.Validate=ValidateKernelDensityConfigSection};}
}
