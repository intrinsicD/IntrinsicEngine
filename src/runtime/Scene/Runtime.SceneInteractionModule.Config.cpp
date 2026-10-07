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
import Extrinsic.Runtime.EngineConfigControl;
#include "Config/internal/Runtime.PointConfigJson.hpp"
#include "Config/internal/Runtime.ConfigFieldJson.hpp"
namespace Extrinsic::Runtime
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr ConfigFieldSpec Step(std::string_view name, std::string_view description)
        {
            return {.Name = name, .Type = ConfigFieldType::Float, .Description = description,
                    .Min = 0.0, .ExclusiveMin = true};
        }
        // Rotation steps start at 0.001 degrees: finer turns are float noise in the
        // gizmo matrix, and SnapGizmoRotation relies on this floor.
        constexpr std::array kFields{
            Step("translate_step", "Translation snap step in world units."),
            ConfigFieldSpec{.Name = "rotate_step_degrees", .Type = ConfigFieldType::Float,
                            .Description = "Rotation snap step in degrees.", .Min = 0.001},
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
        Core::Config::EngineConfigLoadResult PreviewWith(const EngineConfigControl& control,
                                                         const GizmoSnapConfig& draft)
        {
            Core::Config::EngineConfig candidate = control.GetEngineConfigControlState().ActiveConfig;
            SetGizmoSnapConfig(candidate, draft);
            return control.PreviewEngineConfigControlDocument(Core::Config::SerializeEngineConfig(candidate),
                                                              std::string{kGizmoSnapConfigSectionName});
        }
        // A rejected section falls back to the active value.
        bool Accepts(const Core::Config::EngineConfigLoadResult& preview, const GizmoSnapConfig& draft)
        {
            return Core::Config::IsConfigUsable(preview) && GetGizmoSnapConfig(preview.Preview.Config) == draft;
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
        // A positive double below the normal float range would become a zero or
        // denormal step; one above it would overflow the float the gizmo reads.
        for (const auto& field : kFields)
            if (const double step = merged[std::string{field.Name}].get<double>();
                step < double(std::numeric_limits<float>::min()) || step > double(std::numeric_limits<float>::max()))
                return ConfigDetail::RejectConfigSection(
                    subject, std::string{field.Name} + " is too small or too large for a float.");
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
    std::string PreviewGizmoSnapConfig(const EngineConfigControl& control, const GizmoSnapConfig& draft)
    {
        const auto preview = PreviewWith(control, draft);
        if (Accepts(preview, draft)) return {};
        return preview.Diagnostics.empty() ? std::string{"Snap steps were rejected."} : preview.Diagnostics.front().Message;
    }
    RuntimeEngineConfigApplyResult ApplyGizmoSnapConfig(EngineConfigControl& control, const GizmoSnapConfig& draft)
    {
        auto preview = PreviewWith(control, draft);
        if (!Accepts(preview, draft))
            return {.Status = RuntimeEngineConfigApplyStatus::Rejected, .Source = RuntimeConfigControlSource::Editor,
                    .LoadResult = std::move(preview)};
        return control.ApplyEngineConfigHotSubset(preview, RuntimeConfigControlSource::Editor);
    }
}
