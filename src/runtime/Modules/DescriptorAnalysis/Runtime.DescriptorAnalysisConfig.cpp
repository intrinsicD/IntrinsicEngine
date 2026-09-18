module;
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
module Extrinsic.Runtime.DescriptorAnalysisConfig;
#include "Config/internal/Runtime.PointConfigJson.hpp"
namespace Extrinsic::Runtime
{
    namespace
    {
        using Json=nlohmann::json;
        DescriptorAnalysisConfig Parse(const Json& data)
        {
            DescriptorAnalysisConfig c;c.StableEntityId=data.at("entity");
            for(unsigned i=0;i<3;++i)if(data.at("backend")==ToString(DescriptorAnalysisBackend(i)))c.Backend=DescriptorAnalysisBackend(i);
            ConfigDetail::DecodePointPropertyRef(data.at("positions"),c.Positions);ConfigDetail::DecodePointPropertyRef(data.at("normals"),c.Normals);
            for(unsigned i=0;i<33;++i)ConfigDetail::DecodePointPropertyRef(data.at("outputs").at(i),c.Outputs[i]);
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
        auto outputs=Json::array();for(const auto& output:c.Outputs)outputs.push_back(ConfigDetail::EncodePointPropertyRef(output));
        return Json{{"entity",c.StableEntityId},{"backend",ToString(c.Backend)},
            {"positions",ConfigDetail::EncodePointPropertyRef(c.Positions)},{"normals",ConfigDetail::EncodePointPropertyRef(c.Normals)},{"outputs",outputs},
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
        auto input=ConfigDetail::ParseConfigJson(payload, false),data=ConfigDetail::ParseConfigJson(SerializeDescriptorAnalysisConfig({}), true);
        if (auto error = ConfigDetail::ValidatePointConfigFields(
            input, data, "Descriptor analysis config must be an object.", "Unknown descriptor field: ",
            {"entity", "max_neighbors", "gpu_query_batch_size", "gpu_radius_capacity"}))
            return reject(std::move(*error));
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
        using ConfigDetail::PointPropertyValidation;
        if(ConfigDetail::ValidatePointPropertyRef(data["positions"],Geometry::PropertyValueKind::Vec3) != PointPropertyValidation::Valid ||
           ConfigDetail::ValidatePointPropertyRef(data["normals"],Geometry::PropertyValueKind::Vec3) != PointPropertyValidation::Valid ||
           data["positions"]["domain"]!=data["normals"]["domain"])
            return reject("Positions and normals need canonical vec3 references on the same domain.");
        if(!data["outputs"].is_array() || data["outputs"].size()!=33)return reject("FPFH requires exactly 33 float output references.");
        for(unsigned i=0;i<33;++i)
        {
            const auto& ref=data["outputs"][i];
            if(ConfigDetail::ValidatePointPropertyRef(ref,Geometry::PropertyValueKind::Float) != PointPropertyValidation::Valid || ref["domain"]!=data["positions"]["domain"] ||
               ref["name"]==data["positions"]["name"] || ref["name"]==data["normals"]["name"])
                return reject("Descriptor outputs must be float properties on the input domain, distinct from inputs.");
            for(unsigned j=0;j<i;++j)if(ref["name"]==data["outputs"][j]["name"])
                return reject("Descriptor output names must be unique.");
        }
        result.State=EngineConfigState::Valid;result.CanonicalPayloadJson=SerializeDescriptorAnalysisConfig(Parse(data));result.ParsedFieldCount=input.size();return result;
    }
    std::optional<DescriptorAnalysisConfig> GetDescriptorAnalysisConfig(const Core::Config::EngineConfig& c)
    {
        const auto payload = ConfigDetail::FindValidatedCanonicalPayload(
            c, kDescriptorAnalysisConfigSectionName, kDescriptorAnalysisConfigSectionSchemaId, 1u,
            nullptr, ValidateDescriptorAnalysisConfigSection);
        if (!payload) return {};
        return Parse(ConfigDetail::ParseConfigJson(*payload, true));
    }
    void SetDescriptorAnalysisConfig(Core::Config::EngineConfig& c,const DescriptorAnalysisConfig& value)
    {Core::Config::UpsertEngineConfigSection(c.AppSections,Section(value));}
    Core::Config::EngineConfigSectionRegistration MakeDescriptorAnalysisConfigSectionRegistration()
    {return {.DefaultSection=Section({}),.Validate=ValidateDescriptorAnalysisConfigSection};}
}
