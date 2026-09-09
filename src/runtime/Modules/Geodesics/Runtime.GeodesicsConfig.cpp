module;
#include <algorithm>
#include <limits>
#include <nlohmann/json.hpp>
module Extrinsic.Runtime.GeodesicsConfig;
namespace Extrinsic::Runtime
{
    namespace
    {
        using Json = nlohmann::json;
        Core::Config::EngineConfigSection Section(const GeodesicsConfig& value)
        {
            return {.Name = std::string{kGeodesicsConfigSectionName},
                    .SchemaId = std::string{kGeodesicsConfigSectionSchemaId},
                    .SchemaVersion = 1u,
                    .PayloadJson = SerializeGeodesicsConfig(value)};
        }
    }
    std::string SerializeGeodesicsConfig(const GeodesicsConfig& config)
    {
        return Json{{"source_vertices", config.SourceVertices},
                    {"max_halfedge_expansions", config.MaxHalfedgeExpansions},
                    {"position_property", config.PositionProperty}}
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
                it.key() != "position_property")
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
        if (doc.contains("position_property"))
        {
            if (!doc["position_property"].is_string())
                return reject("position_property must name a vertex float3 property.");
            config.PositionProperty = doc["position_property"].get<std::string>();
            if (!config.PositionProperty.starts_with("v:") || config.PositionProperty.size() < 3)
                return reject("position_property must name a vertex float3 property (v:...).");
        }
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
            section->SchemaVersion != 1u)
            return std::nullopt;
        const auto validated =
            ValidateGeodesicsConfigSection(section->PayloadJson, {}, kGeodesicsConfigSectionName);
        if (!validated.Usable())
            return std::nullopt;
        const auto doc = Json::parse(validated.CanonicalPayloadJson);
        return GeodesicsConfig{doc["source_vertices"].get<std::vector<std::uint32_t>>(),
                               doc["max_halfedge_expansions"].get<std::uint32_t>(),
                               doc["position_property"].get<std::string>()};
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
