module;
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <nlohmann/json.hpp>
module Extrinsic.Runtime.PointConstructionConfig;
namespace Extrinsic::Runtime
{
    namespace
    {
        using Json = nlohmann::json;
        Json Ref(const GeometryPropertyRef& ref)
        {
            return {
                {"domain", ref.Domain >= GeometryElementDomain::Unknown &&
                                   ref.Domain <= GeometryElementDomain::PointCloudPoint
                               ? ToString(ref.Domain)
                               : "invalid"},
                {"name", ref.Name},
                {"kind", ref.ValueKind == Geometry::PropertyValueKind::Vec3 ? "vec3" : "invalid"}};
        }
        PointConstructionConfig Parse(const Json& data)
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
            const auto read = [&](const Json& value, GeometryPropertyRef& ref)
            {
                ref.Name = value.at("name");
                for (unsigned i = 0; i <= unsigned(GeometryElementDomain::PointCloudPoint); ++i)
                    if (value.at("domain") == ToString(GeometryElementDomain(i)))
                        ref.Domain = GeometryElementDomain(i);
            };
            read(data.at("positions"), c.Positions);
            read(data.at("normals"), c.Normals);
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
        Core::Config::EngineConfigSection Section(const PointConstructionConfig& c)
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
        return Json{{"entity", c.StableEntityId},
                    {"method", ToString(c.Method)},
                    {"backend", ToString(c.Backend)},
                    {"positions", Ref(c.Positions)},
                    {"normals", Ref(c.Normals)},
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
                    {"min_distance_epsilon", c.MinDistanceEpsilon}}
            .dump();
    }
    Core::Config::EngineConfigSectionValidationResult
    ValidatePointConstructionConfigSection(std::string_view payload, std::string_view,
                                           std::string_view subject)
    {
        using namespace Core::Config;
        EngineConfigSectionValidationResult result;
        const auto reject = [&](std::string message)
        {
            result.Diagnostics.push_back({.Code = EngineConfigDiagnosticCode::InvalidValue,
                                          .Subject = std::string(subject),
                                          .Message = std::move(message)});
            return result;
        };
        auto input = Json::parse(payload, nullptr, false),
             data = Json::parse(SerializePointConstructionConfig({}));
        if (!input.is_object())
            return reject("Point construction config must be an object.");
        for (auto it = input.begin(); it != input.end(); ++it)
        {
            if (!data.contains(it.key()))
                return reject("Unknown construction field: " + it.key());
            data[it.key()] = it.value();
        }
        for (auto key : {"entity", "resolution", "k_neighbors", "normal_k_neighbors",
                         "gpu_query_batch_size", "max_grid_vertices"})
            if (!data[key].is_number_unsigned() ||
                data[key].get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
                return reject(std::string(key) + " must be an unsigned 32-bit integer.");
        if (data["resolution"] < 1 || data["resolution"] > 512 || data["k_neighbors"] < 1 ||
            data["k_neighbors"] > 63 || data["normal_k_neighbors"] < 3 ||
            data["normal_k_neighbors"] > 1024 || data["gpu_query_batch_size"] < 1 ||
            data["gpu_query_batch_size"] > 16384 || data["max_grid_vertices"] < 8 ||
            data["max_grid_vertices"] > (1u << 24))
            return reject("Resolution must be 1..512, k 1..63, normal k 3..1024, batch 1..16384 "
                          "and grid budget 8..16777216.");
        if (data["backend"] != "cpu_reference" && data["backend"] != "cpu_lbvh" &&
            data["backend"] != "vulkan_lbvh")
            return reject("Unknown construction backend.");
        if (data["method"] != "hoppe" && data["method"] != "knn_graph")
            return reject("Unknown construction method.");
        if (!data["estimate_normals"].is_boolean() || !data["mutual"].is_boolean())
            return reject("Construction switches must be boolean.");
        if (!data["output_name"].is_string() || data["output_name"].get<std::string>().empty() ||
            data["output_name"].get<std::string>().size() > 256)
            return reject("Output name must contain 1..256 bytes.");
        for (auto key : {"bounding_box_padding", "normal_agreement_power", "kernel_sigma_scale",
                         "min_distance_epsilon"})
            if (!data[key].is_number() || !std::isfinite(data[key].get<double>()) ||
                data[key] < 0 || data[key] > std::numeric_limits<float>::max())
                return reject(std::string(key) + " must be a finite nonnegative float.");
        if (data["kernel_sigma_scale"].get<float>() <= 0)
            return reject("Kernel sigma scale must be positive.");
        const auto validRef = [](const Json& ref)
        {
            if (!ref.is_object() || ref.size() != 3 || !ref.contains("domain") ||
                !ref.contains("name") || !ref.contains("kind") || ref["kind"] != "vec3" ||
                !ref["name"].is_string() || ref["name"].get<std::string>().empty())
                return false;
            for (unsigned i = 0; i <= unsigned(GeometryElementDomain::PointCloudPoint); ++i)
                if (ref["domain"] == ToString(GeometryElementDomain(i)))
                    return true;
            return false;
        };
        if (!validRef(data["positions"]) || !validRef(data["normals"]))
            return reject("Inputs require canonical vec3 property references.");
        if (data["method"] == "hoppe" && data["estimate_normals"] == false &&
            data["positions"]["domain"] != data["normals"]["domain"])
            return reject("Supplied normals must share the position domain.");
        result.State = EngineConfigState::Valid;
        result.CanonicalPayloadJson = SerializePointConstructionConfig(Parse(data));
        result.ParsedFieldCount = input.size();
        return result;
    }
    std::optional<PointConstructionConfig>
    GetPointConstructionConfig(const Core::Config::EngineConfig& c)
    {
        const auto* section = Core::Config::FindEngineConfigSection(
            c.AppSections, kPointConstructionConfigSectionName);
        if (!section || section->SchemaId != kPointConstructionConfigSectionSchemaId ||
            section->SchemaVersion != 1)
            return {};
        const auto validated = ValidatePointConstructionConfigSection(
            section->PayloadJson, {}, kPointConstructionConfigSectionName);
        if (!validated.Usable())
            return {};
        return Parse(Json::parse(validated.CanonicalPayloadJson));
    }
    void SetPointConstructionConfig(Core::Config::EngineConfig& c,
                                    const PointConstructionConfig& value)
    {
        Core::Config::UpsertEngineConfigSection(c.AppSections, Section(value));
    }
    Core::Config::EngineConfigSectionRegistration MakePointConstructionConfigSectionRegistration()
    {
        return {.DefaultSection = Section({}), .Validate = ValidatePointConstructionConfigSection};
    }
} // namespace Extrinsic::Runtime
