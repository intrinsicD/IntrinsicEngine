// Compiles BilateralFilter, KeypointAnalysis, DescriptorAnalysis, PointConstruction config codecs together to share JSON work.
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

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;

import Extrinsic.Runtime.BilateralFilterConfig;
import Extrinsic.Runtime.KeypointAnalysisConfig;
import Extrinsic.Runtime.DescriptorAnalysisConfig;
import Extrinsic.Runtime.PointConstructionConfig;

#include "Config/internal/Runtime.PointConfigJson.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        using Json=nlohmann::json;
        BilateralFilterConfig ParseBilateralFilterConfig(const Json& data)
        {
            BilateralFilterConfig c;c.StableEntityId=data.at("entity");
            for(unsigned i=0;i<3;++i)if(data.at("backend")==ToString(BilateralFilterBackend(i)))c.Backend=BilateralFilterBackend(i);
            ConfigDetail::DecodePointPropertyRef(data.at("positions"),c.Positions);ConfigDetail::DecodePointPropertyRef(data.at("normals"),c.Normals);ConfigDetail::DecodePointPropertyRef(data.at("output"),c.Output);
            c.KNeighbors=data.at("k_neighbors");
            c.GpuQueryBatchSize=data.at("gpu_query_batch_size");c.SpatialSigma=data.at("spatial_sigma");c.NormalSigma=data.at("normal_sigma");c.Iterations=data.at("iterations");
            return c;
        }
        Core::Config::EngineConfigSection MakeBilateralFilterConfigSection(const BilateralFilterConfig& c)
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
        result.State=EngineConfigState::Valid;result.CanonicalPayloadJson=SerializeBilateralFilterConfig(ParseBilateralFilterConfig(data));result.ParsedFieldCount=input.size();return result;
    }
    std::optional<BilateralFilterConfig> GetBilateralFilterConfig(const Core::Config::EngineConfig& c)
    {
        const auto payload = ConfigDetail::FindValidatedCanonicalPayload(
            c, kBilateralFilterConfigSectionName, kBilateralFilterConfigSectionSchemaId, 1u,
            nullptr, ValidateBilateralFilterConfigSection);
        if (!payload) return {};
        return ParseBilateralFilterConfig(ConfigDetail::ParseConfigJson(*payload, true));
    }
    void SetBilateralFilterConfig(Core::Config::EngineConfig& c,const BilateralFilterConfig& value)
    {Core::Config::UpsertEngineConfigSection(c.AppSections,MakeBilateralFilterConfigSection(value));}
    Core::Config::EngineConfigSectionRegistration MakeBilateralFilterConfigSectionRegistration()
    {return {.DefaultSection=MakeBilateralFilterConfigSection({}),.Validate=ValidateBilateralFilterConfigSection};}
}

namespace Extrinsic::Runtime
{
    namespace
    {
        using Json=nlohmann::json;
        KeypointAnalysisConfig ParseKeypointAnalysisConfig(const Json& data)
        {
            KeypointAnalysisConfig c;c.StableEntityId=data.at("entity");
            for(unsigned i=0;i<4;++i)if(data.at("backend")==ToString(KeypointAnalysisBackend(i)))c.Backend=KeypointAnalysisBackend(i);
            ConfigDetail::DecodePointPropertyRef(data.at("positions"),c.Positions);ConfigDetail::DecodePointPropertyRef(data.at("mask"),c.Mask);ConfigDetail::DecodePointPropertyRef(data.at("score"),c.Score);
            c.MinimumNeighbors=data.at("minimum_neighbors");c.GpuQueryBatchSize=data.at("gpu_query_batch_size");
            c.GpuRadiusCapacity=data.at("gpu_radius_capacity");c.SalientRadius=data.at("salient_radius");
            c.NonMaxRadius=data.at("nonmax_radius");c.Gamma21=data.at("gamma21");c.Gamma32=data.at("gamma32");
            return c;
        }
        Core::Config::EngineConfigSection MakeKeypointAnalysisConfigSection(const KeypointAnalysisConfig& c)
        {
            return {.Name=std::string(kKeypointAnalysisConfigSectionName),.SchemaId=std::string(kKeypointAnalysisConfigSectionSchemaId),
                    .SchemaVersion=1,.PayloadJson=SerializeKeypointAnalysisConfig(c)};
        }
    }

    const char* ToString(KeypointAnalysisBackend b) noexcept
    {
        switch(b){case KeypointAnalysisBackend::CpuKDTree:return "cpu_kdtree";case KeypointAnalysisBackend::CpuLBVH:return "cpu_lbvh";case KeypointAnalysisBackend::VulkanLBVH:return "vulkan_lbvh";case KeypointAnalysisBackend::VulkanCompute:return "vulkan_compute";}
        return "invalid";
    }

    std::string SerializeKeypointAnalysisConfig(const KeypointAnalysisConfig& c)
    {
        return ConfigDetail::SerializeConfigJson(Json{{"entity",c.StableEntityId},{"backend",ToString(c.Backend)},
            {"positions",ConfigDetail::EncodePointPropertyRef(c.Positions)},{"mask",ConfigDetail::EncodePointPropertyRef(c.Mask)},{"score",ConfigDetail::EncodePointPropertyRef(c.Score)},
            {"minimum_neighbors",c.MinimumNeighbors},{"gpu_query_batch_size",c.GpuQueryBatchSize},
            {"gpu_radius_capacity",c.GpuRadiusCapacity},{"salient_radius",c.SalientRadius},
            {"nonmax_radius",c.NonMaxRadius},{"gamma21",c.Gamma21},{"gamma32",c.Gamma32}});
    }
    Core::Config::EngineConfigSectionValidationResult ValidateKeypointAnalysisConfigSection(
        std::string_view payload,std::string_view,std::string_view subject)
    {
        using namespace Core::Config;
        using ConfigDetail::RejectConfigSection;
        EngineConfigSectionValidationResult result;
        auto input=ConfigDetail::ParseConfigJson(payload, false),data=ConfigDetail::ParseConfigJson(SerializeKeypointAnalysisConfig({}), true);
        if (auto error = ConfigDetail::ValidatePointConfigFields(
            input, data, "Keypoint analysis config must be an object.", "Unknown keypoint field: ",
            {"entity", "minimum_neighbors", "gpu_query_batch_size", "gpu_radius_capacity"}))
            return RejectConfigSection(subject, std::move(*error));
        if(data["gpu_query_batch_size"]==0 || data["gpu_query_batch_size"]>16384 ||
           data["gpu_radius_capacity"]==0 || data["gpu_radius_capacity"]>1024)
            return RejectConfigSection(subject, "GPU query batch must be 1..16384 and complete radius capacity 1..1024.");
        if(data["backend"]!="cpu_kdtree" && data["backend"]!="cpu_lbvh" && data["backend"]!="vulkan_lbvh" && data["backend"]!="vulkan_compute")
            return RejectConfigSection(subject, "Unknown keypoint backend.");
        for(auto key:{"salient_radius","nonmax_radius"})
            if(!data[key].is_number() || !std::isfinite(data[key].get<double>()) || data[key]<0 ||
               data[key].get<double>()>std::numeric_limits<float>::max() ||
               (data[key]>0 && data[key].get<float>()==0))
                return RejectConfigSection(subject, std::string(key)+" must be zero (automatic) or a positive representable float.");
        for(auto key:{"gamma21","gamma32"})
            if(!data[key].is_number() || !std::isfinite(data[key].get<double>()) || data[key]<0 || data[key]>1)
                return RejectConfigSection(subject, std::string(key)+" must be finite in [0,1].");
        const KeypointAnalysisConfig defaults;
        if (auto error = ConfigDetail::ValidatePointConfigPropertyRefs(
            data, {{"positions", defaults.Positions.ValueKind}, {"mask", defaults.Mask.ValueKind}, {"score", defaults.Score.ValueKind}}))
            return RejectConfigSection(subject, std::move(*error));
        if(data["positions"]["domain"]!=data["mask"]["domain"] || data["mask"]["domain"]!=data["score"]["domain"] ||
           data["positions"]["name"]==data["mask"]["name"] || data["positions"]["name"]==data["score"]["name"] || data["mask"]["name"]==data["score"]["name"])
            return RejectConfigSection(subject, "Position, mask and score must be distinct properties on the same domain.");
        result.State=EngineConfigState::Valid;result.CanonicalPayloadJson=SerializeKeypointAnalysisConfig(ParseKeypointAnalysisConfig(data));result.ParsedFieldCount=input.size();return result;
    }
    std::optional<KeypointAnalysisConfig> GetKeypointAnalysisConfig(const Core::Config::EngineConfig& c)
    {
        const auto payload = ConfigDetail::FindValidatedCanonicalPayload(
            c, kKeypointAnalysisConfigSectionName, kKeypointAnalysisConfigSectionSchemaId, 1u,
            nullptr, ValidateKeypointAnalysisConfigSection);
        if (!payload) return {};
        return ParseKeypointAnalysisConfig(ConfigDetail::ParseConfigJson(*payload, true));
    }
    void SetKeypointAnalysisConfig(Core::Config::EngineConfig& c,const KeypointAnalysisConfig& value)
    {Core::Config::UpsertEngineConfigSection(c.AppSections,MakeKeypointAnalysisConfigSection(value));}
    Core::Config::EngineConfigSectionRegistration MakeKeypointAnalysisConfigSectionRegistration()
    {return {.DefaultSection=MakeKeypointAnalysisConfigSection({}),.Validate=ValidateKeypointAnalysisConfigSection};}
}

namespace Extrinsic::Runtime
{
    namespace
    {
        using Json=nlohmann::json;
        DescriptorAnalysisConfig ParseDescriptorAnalysisConfig(const Json& data)
        {
            DescriptorAnalysisConfig c;c.StableEntityId=data.at("entity");
            for(unsigned i=0;i<3;++i)if(data.at("backend")==ToString(DescriptorAnalysisBackend(i)))c.Backend=DescriptorAnalysisBackend(i);
            ConfigDetail::DecodePointPropertyRef(data.at("positions"),c.Positions);ConfigDetail::DecodePointPropertyRef(data.at("normals"),c.Normals);
            for(unsigned i=0;i<33;++i)ConfigDetail::DecodePointPropertyRef(data.at("outputs").at(i),c.Outputs[i]);
            c.MaxNeighbors=data.at("max_neighbors");c.GpuQueryBatchSize=data.at("gpu_query_batch_size");
            c.GpuRadiusCapacity=data.at("gpu_radius_capacity");c.FeatureRadius=data.at("feature_radius");
            return c;
        }
        Core::Config::EngineConfigSection MakeDescriptorAnalysisConfigSection(const DescriptorAnalysisConfig& c)
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
        return ConfigDetail::SerializeConfigJson(Json{{"entity",c.StableEntityId},{"backend",ToString(c.Backend)},
            {"positions",ConfigDetail::EncodePointPropertyRef(c.Positions)},{"normals",ConfigDetail::EncodePointPropertyRef(c.Normals)},{"outputs",outputs},
            {"max_neighbors",c.MaxNeighbors},{"gpu_query_batch_size",c.GpuQueryBatchSize},
            {"gpu_radius_capacity",c.GpuRadiusCapacity},{"feature_radius",c.FeatureRadius}});
    }
    Core::Config::EngineConfigSectionValidationResult ValidateDescriptorAnalysisConfigSection(
        std::string_view payload,std::string_view,std::string_view subject)
    {
        using namespace Core::Config;
        using ConfigDetail::RejectConfigSection;
        EngineConfigSectionValidationResult result;
        auto input=ConfigDetail::ParseConfigJson(payload, false),data=ConfigDetail::ParseConfigJson(SerializeDescriptorAnalysisConfig({}), true);
        if (auto error = ConfigDetail::ValidatePointConfigFields(
            input, data, "Descriptor analysis config must be an object.", "Unknown descriptor field: ",
            {"entity", "max_neighbors", "gpu_query_batch_size", "gpu_radius_capacity"}))
            return RejectConfigSection(subject, std::move(*error));
        if(data["gpu_query_batch_size"]==0 || data["gpu_query_batch_size"]>16384 ||
           data["gpu_radius_capacity"]==0 || data["gpu_radius_capacity"]>1024)
            return RejectConfigSection(subject, "GPU query batch must be 1..16384 and complete radius capacity 1..1024.");
        if(data["backend"]!="cpu_kdtree" && data["backend"]!="cpu_lbvh" && data["backend"]!="vulkan_lbvh")
            return RejectConfigSection(subject, "Unknown descriptor backend.");
        for(auto key:{"feature_radius"})
            if(!data[key].is_number() || !std::isfinite(data[key].get<double>()) || data[key]<0 ||
               data[key].get<double>()>std::numeric_limits<float>::max() ||
               (data[key]>0 && data[key].get<float>()==0))
                return RejectConfigSection(subject, std::string(key)+" must be zero (automatic) or a positive representable float.");
        using ConfigDetail::PointPropertyValidation;
        if(ConfigDetail::ValidatePointPropertyRef(data["positions"],Geometry::PropertyValueKind::Vec3) != PointPropertyValidation::Valid ||
           ConfigDetail::ValidatePointPropertyRef(data["normals"],Geometry::PropertyValueKind::Vec3) != PointPropertyValidation::Valid ||
           data["positions"]["domain"]!=data["normals"]["domain"])
            return RejectConfigSection(subject, "Positions and normals need canonical vec3 references on the same domain.");
        if(!data["outputs"].is_array() || data["outputs"].size()!=33)return RejectConfigSection(subject, "FPFH requires exactly 33 float output references.");
        for(unsigned i=0;i<33;++i)
        {
            const auto& ref=data["outputs"][i];
            if(ConfigDetail::ValidatePointPropertyRef(ref,Geometry::PropertyValueKind::Float, true) != PointPropertyValidation::Valid || ref["domain"]!=data["positions"]["domain"] ||
               ref["name"]==data["positions"]["name"] || ref["name"]==data["normals"]["name"])
                return RejectConfigSection(subject, "Descriptor outputs must be float properties on the input domain, distinct from inputs.");
            for(unsigned j=0;j<i;++j)if(ref["name"]==data["outputs"][j]["name"])
                return RejectConfigSection(subject, "Descriptor output names must be unique.");
        }
        result.State=EngineConfigState::Valid;result.CanonicalPayloadJson=SerializeDescriptorAnalysisConfig(ParseDescriptorAnalysisConfig(data));result.ParsedFieldCount=input.size();return result;
    }
    std::optional<DescriptorAnalysisConfig> GetDescriptorAnalysisConfig(const Core::Config::EngineConfig& c)
    {
        const auto payload = ConfigDetail::FindValidatedCanonicalPayload(
            c, kDescriptorAnalysisConfigSectionName, kDescriptorAnalysisConfigSectionSchemaId, 1u,
            nullptr, ValidateDescriptorAnalysisConfigSection);
        if (!payload) return {};
        return ParseDescriptorAnalysisConfig(ConfigDetail::ParseConfigJson(*payload, true));
    }
    void SetDescriptorAnalysisConfig(Core::Config::EngineConfig& c,const DescriptorAnalysisConfig& value)
    {Core::Config::UpsertEngineConfigSection(c.AppSections,MakeDescriptorAnalysisConfigSection(value));}
    Core::Config::EngineConfigSectionRegistration MakeDescriptorAnalysisConfigSectionRegistration()
    {return {.DefaultSection=MakeDescriptorAnalysisConfigSection({}),.Validate=ValidateDescriptorAnalysisConfigSection};}
}

namespace Extrinsic::Runtime
{
    namespace
    {
        using Json = nlohmann::json;
        PointConstructionConfig ParsePointConstructionConfig(const Json& data)
        {
            PointConstructionConfig c;
            c.StableEntityId = data.at("entity");
            c.OutputName = data.at("output_name");
            for (unsigned i = 0; i < 2; ++i)
                if (data.at("method") == ToString(PointConstructionMethod(i)))
                    c.Method = PointConstructionMethod(i);
            for (unsigned i = 0; i < 3; ++i)
                if (data.at("backend") == ToString(PointConstructionBackend(i)))
                    c.Backend = PointConstructionBackend(i);
            ConfigDetail::DecodePointPropertyRef(data.at("positions"), c.Positions);
            ConfigDetail::DecodePointPropertyRef(data.at("normals"), c.Normals);
            c.EstimateNormals = data.at("estimate_normals");
            c.Mutual = data.at("mutual");
            c.Resolution = data.at("resolution");
            c.KNeighbors = data.at("k_neighbors");
            c.NormalKNeighbors = data.at("normal_k_neighbors");
            c.GpuQueryBatchSize = data.at("gpu_query_batch_size");
            c.MaxGridVertices = data.at("max_grid_vertices");
            c.BoundingBoxPadding = data.at("bounding_box_padding");
            c.NormalAgreementPower = data.at("normal_agreement_power");
            c.KernelSigmaScale = data.at("kernel_sigma_scale");
            c.MinDistanceEpsilon = data.at("min_distance_epsilon");
            return c;
        }
        Core::Config::EngineConfigSection MakePointConstructionConfigSection(const PointConstructionConfig& c)
        {
            return {.Name = std::string(kPointConstructionConfigSectionName),
                    .SchemaId = std::string(kPointConstructionConfigSectionSchemaId),
                    .SchemaVersion = 1,
                    .PayloadJson = SerializePointConstructionConfig(c)};
        }
    } // namespace
    const char* ToString(PointConstructionMethod m) noexcept
    {
        switch (m)
        {
        case PointConstructionMethod::Hoppe:
            return "hoppe";
        case PointConstructionMethod::KnnGraph:
            return "knn_graph";
        }
        return "invalid";
    }
    const char* ToString(PointConstructionBackend b) noexcept
    {
        switch (b)
        {
        case PointConstructionBackend::CpuReference:
            return "cpu_reference";
        case PointConstructionBackend::CpuLBVH:
            return "cpu_lbvh";
        case PointConstructionBackend::VulkanLBVH:
            return "vulkan_lbvh";
        }
        return "invalid";
    }
    std::string SerializePointConstructionConfig(const PointConstructionConfig& c)
    {
        return ConfigDetail::SerializeConfigJson(Json{{"entity", c.StableEntityId},
                    {"method", ToString(c.Method)},
                    {"backend", ToString(c.Backend)},
                    {"positions", ConfigDetail::EncodeVec3PointPropertyRef(c.Positions)},
                    {"normals", ConfigDetail::EncodeVec3PointPropertyRef(c.Normals)},
                    {"output_name", c.OutputName},
                    {"estimate_normals", c.EstimateNormals},
                    {"mutual", c.Mutual},
                    {"resolution", c.Resolution},
                    {"k_neighbors", c.KNeighbors},
                    {"normal_k_neighbors", c.NormalKNeighbors},
                    {"gpu_query_batch_size", c.GpuQueryBatchSize},
                    {"max_grid_vertices", c.MaxGridVertices},
                    {"bounding_box_padding", c.BoundingBoxPadding},
                    {"normal_agreement_power", c.NormalAgreementPower},
                    {"kernel_sigma_scale", c.KernelSigmaScale},
                    {"min_distance_epsilon", c.MinDistanceEpsilon}});
    }
    Core::Config::EngineConfigSectionValidationResult
    ValidatePointConstructionConfigSection(std::string_view payload, std::string_view,
                                           std::string_view subject)
    {
        using namespace Core::Config;
        using ConfigDetail::RejectConfigSection;
        EngineConfigSectionValidationResult result;
        auto input = ConfigDetail::ParseConfigJson(payload, false),
             data = ConfigDetail::ParseConfigJson(SerializePointConstructionConfig({}), true);
        if (auto error = ConfigDetail::ValidatePointConfigFields(
            input, data, "Point construction config must be an object.", "Unknown construction field: ",
            {"entity", "resolution", "k_neighbors",
             "normal_k_neighbors", "gpu_query_batch_size", "max_grid_vertices"}))
            return RejectConfigSection(subject, std::move(*error));
        if (data["resolution"] < 1 || data["resolution"] > 512 || data["k_neighbors"] < 1 ||
            data["k_neighbors"] > 63 || data["normal_k_neighbors"] < 3 ||
            data["normal_k_neighbors"] > 1024 || data["gpu_query_batch_size"] < 1 ||
            data["gpu_query_batch_size"] > 16384 || data["max_grid_vertices"] < 8 ||
            data["max_grid_vertices"] > (1u << 24))
            return RejectConfigSection(subject,
                "Resolution must be 1..512, k 1..63, normal k 3..1024, batch 1..16384 "
                "and grid budget 8..16777216.");
        if (data["backend"] != "cpu_reference" && data["backend"] != "cpu_lbvh" &&
            data["backend"] != "vulkan_lbvh")
            return RejectConfigSection(subject, "Unknown construction backend.");
        if (data["method"] != "hoppe" && data["method"] != "knn_graph")
            return RejectConfigSection(subject, "Unknown construction method.");
        if (!data["estimate_normals"].is_boolean() || !data["mutual"].is_boolean())
            return RejectConfigSection(subject, "Construction switches must be boolean.");
        if (!data["output_name"].is_string() || data["output_name"].get<std::string>().empty() ||
            data["output_name"].get<std::string>().size() > 256)
            return RejectConfigSection(subject, "Output name must contain 1..256 bytes.");
        if (auto error = ConfigDetail::ValidatePointConfigNonnegativeFloats(
            data, {"bounding_box_padding", "normal_agreement_power", "kernel_sigma_scale", "min_distance_epsilon"}))
            return RejectConfigSection(subject, std::move(*error));
        if (data["kernel_sigma_scale"].get<float>() <= 0)
            return RejectConfigSection(subject, "Kernel sigma scale must be positive.");
        using ConfigDetail::PointPropertyValidation;
        if (ConfigDetail::ValidatePointPropertyRef(data["positions"], Geometry::PropertyValueKind::Vec3) != PointPropertyValidation::Valid ||
            ConfigDetail::ValidatePointPropertyRef(data["normals"], Geometry::PropertyValueKind::Vec3) != PointPropertyValidation::Valid)
            return RejectConfigSection(subject, "Inputs require canonical vec3 property references.");
        if (data["method"] == "hoppe" && data["estimate_normals"] == false &&
            data["positions"]["domain"] != data["normals"]["domain"])
            return RejectConfigSection(subject, "Supplied normals must share the position domain.");
        result.State = EngineConfigState::Valid;
        result.CanonicalPayloadJson = SerializePointConstructionConfig(ParsePointConstructionConfig(data));
        result.ParsedFieldCount = input.size();
        return result;
    }
    std::optional<PointConstructionConfig>
    GetPointConstructionConfig(const Core::Config::EngineConfig& c)
    {
        const auto payload = ConfigDetail::FindValidatedCanonicalPayload(
            c, kPointConstructionConfigSectionName, kPointConstructionConfigSectionSchemaId, 1u,
            nullptr, ValidatePointConstructionConfigSection);
        if (!payload) return {};
        return ParsePointConstructionConfig(ConfigDetail::ParseConfigJson(*payload, true));
    }
    void SetPointConstructionConfig(Core::Config::EngineConfig& c,
                                    const PointConstructionConfig& value)
    {
        Core::Config::UpsertEngineConfigSection(c.AppSections, MakePointConstructionConfigSection(value));
    }
    Core::Config::EngineConfigSectionRegistration MakePointConstructionConfigSectionRegistration()
    {
        return {.DefaultSection = MakePointConstructionConfigSection({}), .Validate = ValidatePointConstructionConfigSection};
    }
} // namespace Extrinsic::Runtime
