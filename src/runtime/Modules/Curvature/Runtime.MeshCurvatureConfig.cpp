module;
#include <array>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <limits>
#include <span>
#include <nlohmann/json.hpp>
module Extrinsic.Runtime.MeshCurvatureConfig;
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
        constexpr std::array<K, 1> kDouble{K::Double};
        constexpr std::array<GeometryElementDomain, 1> kVertex{GeometryElementDomain::MeshVertex};
        constexpr std::array<std::string_view, 4> kOutputNames{"All", "Mean", "Gaussian", "Principal directions"};
        constexpr ConfigFieldSpec Ref(std::string_view name, std::span<const K> kinds, std::string_view description)
        {
            return {.Name = name, .Type = FT::PropertyRef, .Description = description, .RefKinds = kinds,
                    .RefDomains = kVertex, .AnyScalar = true};
        }
        constexpr std::array kFields{
            ConfigFieldSpec{.Name = "entity", .Type = FT::UInt, .Description = "Stable id of the mesh entity."},
            ConfigFieldSpec{.Name = "output", .Type = FT::Enum, .Description = "Curvature quantities to publish.", .EnumNames = kOutputNames},
            ConfigFieldSpec{.Name = "publish_directions", .Type = FT::Bool, .Description = "Also publish principal directions."},
            Ref("positions", kVec3, "Vertex positions."),
            Ref("mean", kDouble, "Scalar vertex property receiving mean curvature."),
            Ref("gaussian", kDouble, "Scalar vertex property receiving Gaussian curvature."),
            Ref("min_principal", kDouble, "Scalar vertex property receiving the minimum principal curvature."),
            Ref("max_principal", kDouble, "Scalar vertex property receiving the maximum principal curvature."),
            Ref("direction1", kVec3, "Vec3 vertex property receiving the first principal direction."),
            Ref("direction2", kVec3, "Vec3 vertex property receiving the second principal direction."),
        };
        struct Slot { const char* Key; GeometryPropertyRef MeshCurvatureConfig::* Member; };
        constexpr std::array slots{
            Slot{"positions", &MeshCurvatureConfig::Positions},
            Slot{"mean", &MeshCurvatureConfig::Mean},
            Slot{"gaussian", &MeshCurvatureConfig::Gaussian},
            Slot{"min_principal", &MeshCurvatureConfig::MinPrincipal},
            Slot{"max_principal", &MeshCurvatureConfig::MaxPrincipal},
            Slot{"direction1", &MeshCurvatureConfig::Direction1},
            Slot{"direction2", &MeshCurvatureConfig::Direction2}
        };
        Json Encode(const MeshCurvatureConfig& config)
        {
            Json doc{{"entity", config.StableEntityId}, {"output", static_cast<unsigned>(config.Output)},
                     {"publish_directions", config.PublishPrincipalDirections}};
            for (const auto& slot : slots)
            {
                const auto& ref = config.*slot.Member;
                doc[slot.Key] = ConfigDetail::EncodePointPropertyRef(ref);
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
                (GeometryPropertyComponentCount((defaults.*slots[i].Member).ValueKind) == 1u
                    ? GeometryPropertyComponentCount(ref.ValueKind) != 1u
                    : ref.ValueKind != (defaults.*slots[i].Member).ValueKind) ||
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
        const auto input = ConfigDetail::ParseConfigJson(payload, false);
        auto doc = Encode({});
        if (auto error = ConfigDetail::ValidateDeclaredFields(input, doc, kFields,
                "Mesh curvature config must be an object.", "Unknown curvature field: "))
            return RejectConfigSection(subject, *error);
        MeshCurvatureConfig bindings;
        for (const auto& slot : slots) ConfigDetail::DecodePointPropertyRef(doc[slot.Key], bindings.*slot.Member);
        if (!IsValidMeshCurvaturePropertyBindings(bindings))
            return RejectConfigSection(subject, "Curvature property names must be distinct and must not replace structural vertex storage.");
        result.State=EngineConfigState::Valid;
        result.CanonicalPayloadJson=ConfigDetail::SerializeConfigJson(doc);
        result.ParsedFieldCount=static_cast<std::uint32_t>(input.size());
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
        for (const auto& slot : slots) ConfigDetail::DecodePointPropertyRef(doc[slot.Key], result.*slot.Member);
        return result;
    }
    void SetMeshCurvatureConfig(Core::Config::EngineConfig& config, const MeshCurvatureConfig& value)
    { Core::Config::UpsertEngineConfigSection(config.AppSections, Section(value)); }
    Core::Config::EngineConfigSectionRegistration MakeMeshCurvatureConfigSectionRegistration()
    {
        return {.DefaultSection = Section({}), .Validate = ValidateMeshCurvatureConfigSection,
                .SchemaJson = ConfigDetail::BuildSectionSchemaJson(kMeshCurvatureConfigSectionSchemaId, "Mesh Curvature",
                    "Mean, Gaussian and principal curvatures with principal directions on mesh vertices.",
                    kFields, Encode({}))};
    }
    std::span<const ConfigFieldSpec> MeshCurvatureConfigFieldSpecs() noexcept { return kFields; }
}
