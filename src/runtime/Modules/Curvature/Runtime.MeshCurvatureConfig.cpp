module;
#include <array>
#include <limits>
#include <set>
#include <nlohmann/json.hpp>
module Extrinsic.Runtime.MeshCurvatureConfig;
namespace Extrinsic::Runtime
{
    namespace
    {
        using Json = nlohmann::json;
        struct Slot { const char* Key; GeometryPropertyRef MeshCurvatureConfig::* Member; const char* Kind; };
        constexpr std::array slots{
            Slot{"positions", &MeshCurvatureConfig::Positions, "vec3"},
            Slot{"mean", &MeshCurvatureConfig::Mean, "double"},
            Slot{"gaussian", &MeshCurvatureConfig::Gaussian, "double"},
            Slot{"min_principal", &MeshCurvatureConfig::MinPrincipal, "double"},
            Slot{"max_principal", &MeshCurvatureConfig::MaxPrincipal, "double"},
            Slot{"direction1", &MeshCurvatureConfig::Direction1, "vec3"},
            Slot{"direction2", &MeshCurvatureConfig::Direction2, "vec3"}
        };
        Json Encode(const MeshCurvatureConfig& config)
        {
            Json doc{{"entity", config.StableEntityId}, {"output", static_cast<unsigned>(config.Output)},
                     {"publish_directions", config.PublishPrincipalDirections}};
            const MeshCurvatureConfig defaults;
            for (const auto& slot : slots)
            {
                const auto& ref = config.*slot.Member;
                doc[slot.Key] = {{"domain", ToString(ref.Domain)},
                                 {"name", ref.Name},
                                 {"kind", ref.ValueKind == (defaults.*slot.Member).ValueKind ? slot.Kind : "invalid"}};
            }
            return doc;
        }
        Core::Config::EngineConfigSection Section(const MeshCurvatureConfig& config)
        {
            return {.Name=std::string{kMeshCurvatureConfigSectionName},
                    .SchemaId=std::string{kMeshCurvatureConfigSectionSchemaId}, .SchemaVersion=1,
                    .PayloadJson=SerializeMeshCurvatureConfig(config)};
        }
    }
    std::string SerializeMeshCurvatureConfig(const MeshCurvatureConfig& config) { return Encode(config).dump(); }
    Core::Config::EngineConfigSectionValidationResult ValidateMeshCurvatureConfigSection(
        std::string_view payload, std::string_view, std::string_view subject)
    {
        using namespace Core::Config;
        EngineConfigSectionValidationResult result;
        auto reject = [&](std::string message) {
            result.Diagnostics.push_back({.Code=EngineConfigDiagnosticCode::InvalidValue,
                .Subject=std::string{subject}, .Message=std::move(message)});
            return result;
        };
        auto doc = Json::parse(payload, nullptr, false);
        if (!doc.is_object()) return reject("Mesh curvature config must be an object.");
        const auto defaults = Encode({});
        for (const auto& [key, value] : doc.items())
            if (!defaults.contains(key)) return reject("Unknown curvature field: " + key);
        for (const auto& [key, value] : defaults.items())
            if (!doc.contains(key)) doc[key] = value;
        if (!doc["entity"].is_number_unsigned() || doc["entity"].get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max() ||
            !doc["output"].is_number_unsigned() || doc["output"].get<std::uint64_t>() > 3 ||
            !doc["publish_directions"].is_boolean())
            return reject("Invalid curvature entity, output mode or direction control.");
        std::set<std::string> names;
        for (const auto& slot : slots)
        {
            const auto& ref = doc[slot.Key];
            if (!ref.is_object() || ref.size() != 3 || !ref.contains("domain") || ref["domain"] != ToString(GeometryElementDomain::MeshVertex) ||
                !ref.contains("kind") || ref["kind"] != slot.Kind || !ref.contains("name") || !ref["name"].is_string())
                return reject("Curvature requires typed mesh vertex bindings.");
            const auto name = ref["name"].get<std::string>();
            if (!name.starts_with("v:") || name.size() < 3 || name.find('\0') != std::string::npos ||
                name == "v:deleted" || (slot.Member != &MeshCurvatureConfig::Positions && name == "v:position") || !names.insert(name).second)
                return reject("Curvature property names must be distinct public vertex properties (v:...).");
        }
        result.State=EngineConfigState::Valid;
        result.CanonicalPayloadJson=doc.dump();
        result.ParsedFieldCount=static_cast<std::uint32_t>(doc.size());
        return result;
    }
    std::optional<MeshCurvatureConfig> GetMeshCurvatureConfig(const Core::Config::EngineConfig& config)
    {
        const auto* section=Core::Config::FindEngineConfigSection(config.AppSections, kMeshCurvatureConfigSectionName);
        if (!section || section->SchemaId != kMeshCurvatureConfigSectionSchemaId || section->SchemaVersion != 1) return std::nullopt;
        const auto validation=ValidateMeshCurvatureConfigSection(section->PayloadJson, {}, kMeshCurvatureConfigSectionName);
        if (!validation.Usable()) return std::nullopt;
        const auto doc=Json::parse(validation.CanonicalPayloadJson);
        MeshCurvatureConfig result;
        result.StableEntityId=doc["entity"];
        result.Output=static_cast<EditorMeshCurvatureOutput>(doc["output"].get<unsigned>());
        result.PublishPrincipalDirections=doc["publish_directions"];
        for (const auto& slot : slots) (result.*slot.Member).Name=doc[slot.Key]["name"];
        return result;
    }
    void SetMeshCurvatureConfig(Core::Config::EngineConfig& config, const MeshCurvatureConfig& value)
    { Core::Config::UpsertEngineConfigSection(config.AppSections, Section(value)); }
    Core::Config::EngineConfigSectionRegistration MakeMeshCurvatureConfigSectionRegistration()
    { return {.DefaultSection=Section({}), .Validate=ValidateMeshCurvatureConfigSection}; }
}
