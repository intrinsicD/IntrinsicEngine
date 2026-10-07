module;
#include <array>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <nlohmann/json.hpp>
module Extrinsic.Runtime.SceneInteractionModule;
import Geometry.Properties.Types;
import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.ConfigFieldSpec;
#include "Config/internal/Runtime.PointConfigJson.hpp"
#include "Config/internal/Runtime.ConfigFieldJson.hpp"
namespace Extrinsic::Runtime
{
    namespace
    {
        using Json = nlohmann::json;
        // Normal-float bounds: a smaller positive double would round to zero or a
        // denormal step, a larger one would overflow the float the gizmo reads.
        constexpr ConfigFieldSpec Step(std::string_view name, std::string_view description)
        {
            return {.Name = name, .Type = ConfigFieldType::Float, .Description = description,
                    .Min = double(std::numeric_limits<float>::min()),
                    .Max = double(std::numeric_limits<float>::max())};
        }
        constexpr std::array kFields{
            Step("translate_step", "Translation snap step in world units."),
            Step("rotate_step_degrees", "Rotation snap step in degrees."),
            Step("scale_step", "Scale snap step."),
        };
        Json Encode(const GizmoSnapConfig& config)
        {
            return Json{{"translate_step", config.TranslateStep},
                        {"rotate_step_degrees", config.RotateStepDegrees},
                        {"scale_step", config.ScaleStep}};
        }
        Core::Config::EngineConfigSection Section(const GizmoSnapConfig& value)
        {
            return ConfigDetail::MakeConfigSection(kGizmoSnapConfigSectionName, kGizmoSnapConfigSectionSchemaId, 1u,
                                                   Encode(value).dump());
        }
        GizmoSnapConfig Decode(const Json& doc)
        {
            return {.TranslateStep = doc["translate_step"].get<float>(),
                    .RotateStepDegrees = doc["rotate_step_degrees"].get<float>(),
                    .ScaleStep = doc["scale_step"].get<float>()};
        }
    }
    Core::Config::EngineConfigSectionValidationResult ValidateGizmoSnapConfigSection(
        std::string_view payload, std::string_view, std::string_view subject)
    {
        const auto doc = Json::parse(payload, nullptr, false);
        auto merged = Encode({});
        if (auto error = ConfigDetail::ValidateDeclaredFields(doc, merged, kFields,
                "Gizmo snap config must be an object.", "Unknown gizmo snap field: "))
            return ConfigDetail::RejectConfigSection(subject, std::move(*error));
        return ConfigDetail::AcceptConfigSection(Encode(Decode(merged)).dump(), doc.size());
    }
    std::optional<GizmoSnapConfig> GetGizmoSnapConfig(const Core::Config::EngineConfig& config)
    {
        const auto payload = ConfigDetail::FindValidatedCanonicalPayload(config, kGizmoSnapConfigSectionName,
            kGizmoSnapConfigSectionSchemaId, 1u, nullptr, ValidateGizmoSnapConfigSection);
        if (!payload) return std::nullopt;
        return Decode(Json::parse(*payload));
    }
    void SetGizmoSnapConfig(Core::Config::EngineConfig& config, const GizmoSnapConfig& value)
    {
        Core::Config::UpsertEngineConfigSection(config.AppSections, Section(value));
    }
    Core::Config::EngineConfigSectionRegistration MakeGizmoSnapConfigSectionRegistration()
    {
        return {.DefaultSection = Section({}), .Validate = ValidateGizmoSnapConfigSection,
                .SchemaJson = ConfigDetail::BuildSectionSchemaJson(kGizmoSnapConfigSectionSchemaId, "Gizmo snap",
                    "Snap steps of the transform gizmo.",
                    kFields, Encode({}))};
    }
}
