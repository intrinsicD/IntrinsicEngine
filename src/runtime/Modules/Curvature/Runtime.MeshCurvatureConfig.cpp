module;
#include <array>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <limits>
#include <nlohmann/json.hpp>
module Extrinsic.Runtime.MeshCurvatureConfig;
#include "Config/internal/Runtime.PointConfigJson.hpp"
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
        [[nodiscard]] bool IsStructuralVertexProperty(
            const std::string_view name) noexcept
        {
            return name == "v:position" || name == "v:deleted" ||
                   name == "v:connectivity" || name == "v:halfedge";
        }
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
    bool IsValidMeshCurvaturePropertyBindings(const MeshCurvatureConfig& config) noexcept
    {
        const MeshCurvatureConfig defaults;
        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            const auto& ref = config.*slots[i].Member;
            if (ref.Domain != GeometryElementDomain::MeshVertex ||
                ref.ValueKind != (defaults.*slots[i].Member).ValueKind ||
                ref.Name.empty() || ref.Name.find('\0') != std::string::npos ||
                (slots[i].Member == &MeshCurvatureConfig::Positions
                     ? IsStructuralVertexProperty(ref.Name) && ref.Name != "v:position"
                     : IsStructuralVertexProperty(ref.Name)))
                return false;
            for (std::size_t j = 0; j < i; ++j)
                if (ref.Name == (config.*slots[j].Member).Name) return false;
        }
        return true;
    }
    std::string SerializeMeshCurvatureConfig(const MeshCurvatureConfig& config) { return ConfigDetail::SerializeConfigJson(Encode(config)); }
    Core::Config::EngineConfigSectionValidationResult ValidateMeshCurvatureConfigSection(
        std::string_view payload, std::string_view, std::string_view subject)
    {
        using namespace Core::Config;
        using ConfigDetail::RejectConfigSection;
        EngineConfigSectionValidationResult result;
        auto doc = ConfigDetail::ParseConfigJson(payload, false);
        if (!doc.is_object()) return RejectConfigSection(subject, "Mesh curvature config must be an object.");
        const auto defaults = Encode({});
        for (const auto& [key, value] : doc.items())
            if (!defaults.contains(key)) return RejectConfigSection(subject, "Unknown curvature field: " + key);
        for (const auto& [key, value] : defaults.items())
            if (!doc.contains(key)) doc[key] = value;
        if (!doc["entity"].is_number_unsigned() || doc["entity"].get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max() ||
            !doc["output"].is_number_unsigned() || doc["output"].get<std::uint64_t>() > 3 ||
            !doc["publish_directions"].is_boolean())
            return RejectConfigSection(subject, "Invalid curvature entity, output mode or direction control.");
        MeshCurvatureConfig bindings;
        for (const auto& slot : slots)
        {
            const auto& ref = doc[slot.Key];
            if (!ref.is_object() || ref.size() != 3 || !ref.contains("domain") || ref["domain"] != ToString(GeometryElementDomain::MeshVertex) ||
                !ref.contains("kind") || ref["kind"] != slot.Kind || !ref.contains("name") || !ref["name"].is_string())
                return RejectConfigSection(subject, "Curvature requires typed mesh vertex bindings.");
            (bindings.*slot.Member).Name = ref["name"].get<std::string>();
        }
        if (!IsValidMeshCurvaturePropertyBindings(bindings))
            return RejectConfigSection(subject, "Curvature property names must be distinct and must not replace structural vertex storage.");
        result.State=EngineConfigState::Valid;
        result.CanonicalPayloadJson=ConfigDetail::SerializeConfigJson(doc);
        result.ParsedFieldCount=static_cast<std::uint32_t>(doc.size());
        return result;
    }
    std::optional<MeshCurvatureConfig> GetMeshCurvatureConfig(const Core::Config::EngineConfig& config)
    {
        const auto payload = ConfigDetail::FindValidatedCanonicalPayload(
            config, kMeshCurvatureConfigSectionName, kMeshCurvatureConfigSectionSchemaId, 1u,
            nullptr, ValidateMeshCurvatureConfigSection);
        if (!payload) return {};
        const auto doc=ConfigDetail::ParseConfigJson(*payload, true);
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
