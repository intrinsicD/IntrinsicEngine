module;
#include <algorithm>
#include <limits>
#include <initializer_list>
#include <utility>
#include <nlohmann/json.hpp>
module Extrinsic.Runtime.GeodesicsConfig;

#include "Config/internal/Runtime.PointConfigJson.hpp"
namespace Extrinsic::Runtime
{
    namespace
    {
        using Json = nlohmann::json;
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
        EngineConfigSectionValidationResult result;
        auto doc = Json::parse(payload, nullptr, false);
        auto reject = [&](std::string message) {
            result.Diagnostics.push_back({.Code = EngineConfigDiagnosticCode::InvalidValue,
                                          .Subject = std::string{subject},
                                          .Message = std::move(message)});
            return result;
        };
        if (!doc.is_object())
            return reject("Geodesics config must be an object.");
        for (auto it = doc.begin(); it != doc.end(); ++it)
            if (it.key() != "source_vertices" && it.key() != "max_halfedge_expansions" &&
                it.key() != "position_property" && it.key() != "distance_property" &&
                it.key() != "source_mask_property")
                return reject("Unknown geodesics field: " + it.key());
        GeodesicsConfig config;
        if (doc.contains("source_vertices"))
        {
            const auto& sources = doc["source_vertices"];
            if (!sources.is_array())
                return reject("source_vertices must be an array of vertex indices.");
            for (const auto& source : sources)
            {
                if (!source.is_number_unsigned() ||
                    source.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
                    return reject("source_vertices requires unsigned 32-bit indices.");
                config.SourceVertices.push_back(source.get<std::uint32_t>());
            }
        }
        if (doc.contains("max_halfedge_expansions"))
        {
            const auto& limit = doc["max_halfedge_expansions"];
            if (!limit.is_number_unsigned() || limit.get<std::uint64_t>() == 0 ||
                limit.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
                return reject(
                    "max_halfedge_expansions must be a positive unsigned 32-bit integer.");
            config.MaxHalfedgeExpansions = limit.get<std::uint32_t>();
        }
        for (const auto& [key, ref] : {
                 std::pair{"position_property", &config.PositionProperty},
                 std::pair{"distance_property", &config.DistanceProperty},
                 std::pair{"source_mask_property", &config.SourceMaskProperty}})
        {
            if (doc.contains(key))
            {
                if (ConfigDetail::ValidatePointPropertyRef(doc[key], ref->ValueKind, ref != &config.PositionProperty) !=
                        ConfigDetail::PointPropertyValidation::Valid ||
                    doc[key]["domain"] != ToString(GeometryElementDomain::MeshVertex))
                    return reject(std::string{key} + " requires a typed mesh vertex property reference.");
                ConfigDetail::DecodePointPropertyRef(doc[key], *ref);
            }
            const bool input = ref == &config.PositionProperty;
            if (ref->Name.empty() || ref->Name.find('\0') != std::string::npos ||
                (IsStructuralVertexProperty(ref->Name) && (!input || ref->Name != "v:position")))
                return reject("Geodesics bindings must not replace structural vertex storage.");
        }
        if (config.DistanceProperty.Name == config.SourceMaskProperty.Name ||
            config.DistanceProperty.Name == config.PositionProperty.Name ||
            config.SourceMaskProperty.Name == config.PositionProperty.Name)
            return reject("Geodesics input and output properties must be distinct.");
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
        return {.DefaultSection = Section({}), .Validate = ValidateGeodesicsConfigSection};
    }
}
