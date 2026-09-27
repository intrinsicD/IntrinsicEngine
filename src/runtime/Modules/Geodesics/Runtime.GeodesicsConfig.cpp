module;
#include <algorithm>
#include <array>
#include <span>
#include <limits>
#include <initializer_list>
#include <utility>
#include <nlohmann/json.hpp>
module Extrinsic.Runtime.GeodesicsConfig;

#include "Config/internal/Runtime.PointConfigJson.hpp"
#include "Config/internal/Runtime.ConfigFieldJson.hpp"
namespace Extrinsic::Runtime
{
    namespace
    {
        using Json = nlohmann::json;
        using FT = ConfigFieldType;
        using K = Geometry::PropertyValueKind;
        constexpr std::array<K, 1> kVec3{K::Vec3};
        constexpr std::array<K, 1> kBool{K::Bool};
        constexpr std::array<K, 1> kDouble{K::Double};
        constexpr std::array<GeometryElementDomain, 1> kVertex{GeometryElementDomain::MeshVertex};
        constexpr std::array kFields{
            ConfigFieldSpec{.Name = "source_vertices", .Type = FT::UIntArray, .Description = "Source vertex indices."},
            ConfigFieldSpec{.Name = "source_vertex_property", .Type = FT::PropertyRef, .Description = "Scalar vertex property; vertices with a finite nonzero value are added as sources.", .Nullable = true, .RefKinds = kBool, .RefDomains = kVertex, .AnyScalar = true},
            ConfigFieldSpec{.Name = "max_halfedge_expansions", .Type = FT::UInt, .Description = "Work limit of the window propagation.", .Min = 1},
            ConfigFieldSpec{.Name = "position_property", .Type = FT::PropertyRef, .Description = "Vertex positions.", .RefKinds = kVec3, .RefDomains = kVertex},
            ConfigFieldSpec{.Name = "distance_property", .Type = FT::PropertyRef, .Description = "Scalar vertex property receiving the geodesic distance.", .RefKinds = kDouble, .RefDomains = kVertex, .AnyScalar = true},
            ConfigFieldSpec{.Name = "source_mask_property", .Type = FT::PropertyRef, .Description = "Scalar vertex property marking the sources.", .RefKinds = kBool, .RefDomains = kVertex, .AnyScalar = true},
        };
        Core::Config::EngineConfigSection Section(const GeodesicsConfig& value)
        {
            return {.Name = std::string{kGeodesicsConfigSectionName},
                    .SchemaId = std::string{kGeodesicsConfigSectionSchemaId},
                    .SchemaVersion = 2u,
                    .PayloadJson = SerializeGeodesicsConfig(value)};
        }
    }
    std::string SerializeGeodesicsConfig(const GeodesicsConfig& config)
    {
        return Json{{"source_vertices", config.SourceVertices},
                    {"source_vertex_property", config.SourceVertexProperty.Name.empty()
                        ? Json(nullptr) : ConfigDetail::EncodePointPropertyRef(config.SourceVertexProperty)},
                    {"max_halfedge_expansions", config.MaxHalfedgeExpansions},
                    {"position_property", ConfigDetail::EncodePointPropertyRef(config.PositionProperty)},
                    {"distance_property", ConfigDetail::EncodePointPropertyRef(config.DistanceProperty)},
                    {"source_mask_property", ConfigDetail::EncodePointPropertyRef(config.SourceMaskProperty)}}
            .dump();
    }
    Core::Config::EngineConfigSectionValidationResult ValidateGeodesicsConfigSection(
        std::string_view payload, std::string_view, std::string_view subject)
    {
        using namespace Core::Config;
        const auto reject = [&](std::string message) { return ConfigDetail::RejectConfigSection(subject, std::move(message)); };
        const auto doc = Json::parse(payload, nullptr, false);
        auto merged = Json::parse(SerializeGeodesicsConfig({}));
        if (auto error = ConfigDetail::ValidateDeclaredFields(doc, merged, kFields,
                "Geodesics config must be an object.", "Unknown geodesics field: "))
            return reject(*error);
        GeodesicsConfig config;
        config.SourceVertices = merged["source_vertices"].get<std::vector<std::uint32_t>>();
        config.MaxHalfedgeExpansions = merged["max_halfedge_expansions"].get<std::uint32_t>();
        if (!merged["source_vertex_property"].is_null())
        {
            ConfigDetail::DecodePointPropertyRef(merged["source_vertex_property"], config.SourceVertexProperty);
            if (config.SourceVertexProperty.Name.find('\0') != std::string::npos ||
                IsStructuralVertexProperty(config.SourceVertexProperty.Name))
                return reject("source_vertex_property must name a scalar, non-structural vertex property.");
        }
        for (const auto& [key, ref] : {
                 std::pair{"position_property", &config.PositionProperty},
                 std::pair{"distance_property", &config.DistanceProperty},
                 std::pair{"source_mask_property", &config.SourceMaskProperty}})
        {
            ConfigDetail::DecodePointPropertyRef(merged[key], *ref);
            const bool input = ref == &config.PositionProperty;
            if (ref->Name.find('\0') != std::string::npos ||
                (IsStructuralVertexProperty(ref->Name) && (!input || ref->Name != "v:position")))
                return reject("Geodesics bindings must not replace structural vertex storage.");
        }
        if (config.DistanceProperty.Name == config.SourceMaskProperty.Name ||
            config.DistanceProperty.Name == config.PositionProperty.Name ||
            config.SourceMaskProperty.Name == config.PositionProperty.Name ||
            (!config.SourceVertexProperty.Name.empty() &&
             config.SourceVertexProperty.Name == config.DistanceProperty.Name))
            return reject("Geodesics input and output properties must be distinct.");
        EngineConfigSectionValidationResult result;
        result.State = EngineConfigState::Valid;
        result.CanonicalPayloadJson = SerializeGeodesicsConfig(config);
        result.ParsedFieldCount = static_cast<std::uint32_t>(doc.size());
        return result;
    }
    std::optional<GeodesicsConfig> GetGeodesicsConfig(const Core::Config::EngineConfig& config)
    {
        const auto* section =
            Core::Config::FindEngineConfigSection(config.AppSections, kGeodesicsConfigSectionName);
        if (!section || section->SchemaId != kGeodesicsConfigSectionSchemaId ||
            section->SchemaVersion != 2u)
            return std::nullopt;
        const auto validated =
            ValidateGeodesicsConfigSection(section->PayloadJson, {}, kGeodesicsConfigSectionName);
        if (!validated.Usable())
            return std::nullopt;
        const auto doc = Json::parse(validated.CanonicalPayloadJson);
        GeodesicsConfig result;
        result.SourceVertices = doc["source_vertices"].get<std::vector<std::uint32_t>>();
        if (!doc["source_vertex_property"].is_null())
            ConfigDetail::DecodePointPropertyRef(doc["source_vertex_property"], result.SourceVertexProperty);
        result.MaxHalfedgeExpansions = doc["max_halfedge_expansions"].get<std::uint32_t>();
        ConfigDetail::DecodePointPropertyRef(doc["position_property"], result.PositionProperty);
        ConfigDetail::DecodePointPropertyRef(doc["distance_property"], result.DistanceProperty);
        ConfigDetail::DecodePointPropertyRef(doc["source_mask_property"], result.SourceMaskProperty);
        return result;
    }
    void SetGeodesicsConfig(Core::Config::EngineConfig& config, const GeodesicsConfig& value)
    {
        Core::Config::UpsertEngineConfigSection(config.AppSections, Section(value));
    }
    Core::Config::EngineConfigSectionRegistration MakeGeodesicsConfigSectionRegistration()
    {
        return {.DefaultSection = Section({}), .Validate = ValidateGeodesicsConfigSection,
                .SchemaJson = ConfigDetail::BuildSectionSchemaJson(kGeodesicsConfigSectionSchemaId, "Geodesics",
                    "Exact polyhedral geodesic distance from source vertices on a triangle mesh.",
                    kFields, Json::parse(SerializeGeodesicsConfig({})))};
    }
    std::span<const ConfigFieldSpec> GeodesicsConfigFieldSpecs() noexcept { return kFields; }
}
