module;
#include <cmath>
#include <nlohmann/json.hpp>
module Extrinsic.Runtime.SelectionController;
namespace Extrinsic::Runtime
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::string_view schema = "intrinsic.runtime.selection";
        constexpr const char* targets[] = {"entity", "vertex", "edge", "face"};
        Core::Config::EngineConfigSection Section(const SelectionInteractionConfig& value)
        {
            return {.Name = std::string{kSelectionConfigSectionName},
                    .SchemaId = std::string{schema},
                    .SchemaVersion = 1u,
                    .PayloadJson = SerializeSelectionInteractionConfig(value)};
        }
    } // namespace
    std::string SerializeSelectionInteractionConfig(const SelectionInteractionConfig& config)
    {
        const auto target = static_cast<unsigned>(config.Target);
        return Json{{"target", target < 4 ? targets[target] : "invalid"},
                    {"highlight", config.Highlight},
                    {"point_radius", config.PointRadius}}
            .dump();
    }
    Core::Config::EngineConfigSectionValidationResult ValidateSelectionConfigSection(
        std::string_view payload, std::string_view, std::string_view subject)
    {
        using namespace Core::Config;
        EngineConfigSectionValidationResult result;
        const auto doc = Json::parse(payload, nullptr, false);
        auto reject = [&](std::string message) {
            result.Diagnostics.push_back({.Code = EngineConfigDiagnosticCode::InvalidValue,
                                          .Subject = std::string{subject},
                                          .Message = std::move(message)});
            return result;
        };
        if (!doc.is_object())
            return reject("Selection config must be an object.");
        for (auto it = doc.begin(); it != doc.end(); ++it)
            if (it.key() != "target" && it.key() != "highlight" && it.key() != "point_radius")
                return reject("Unknown selection field: " + it.key());
        SelectionInteractionConfig config;
        if (doc.contains("target"))
        {
            if (!doc["target"].is_string())
                return reject("target must be entity, vertex, edge, or face.");
            bool found = false;
            for (unsigned i = 0; i < 4; ++i)
                if (doc["target"] == targets[i])
                {
                    config.Target = static_cast<SelectionTarget>(i);
                    found = true;
                }
            if (!found)
                return reject("target must be entity, vertex, edge, or face.");
        }
        if (doc.contains("highlight"))
        {
            if (!doc["highlight"].is_boolean())
                return reject("highlight must be boolean.");
            config.Highlight = doc["highlight"].get<bool>();
        }
        if (doc.contains("point_radius"))
        {
            if (!doc["point_radius"].is_number())
                return reject("point_radius must be positive and finite.");
            const double radius = doc["point_radius"].get<double>();
            if (!std::isfinite(radius) || radius < 1.e-6 || radius > 1.e6)
                return reject("point_radius must be between 0.000001 and 1000000 world units.");
            config.PointRadius = static_cast<float>(radius);
        }
        result.State = EngineConfigState::Valid;
        result.CanonicalPayloadJson = SerializeSelectionInteractionConfig(config);
        result.ParsedFieldCount = static_cast<std::uint32_t>(doc.size());
        return result;
    }
    std::optional<SelectionInteractionConfig> GetSelectionInteractionConfig(
        const Core::Config::EngineConfig& config)
    {
        const auto* section =
            Core::Config::FindEngineConfigSection(config.AppSections, kSelectionConfigSectionName);
        if (!section || section->SchemaId != schema || section->SchemaVersion != 1u)
            return std::nullopt;
        const auto validation =
            ValidateSelectionConfigSection(section->PayloadJson, {}, kSelectionConfigSectionName);
        if (!validation.Usable())
            return std::nullopt;
        const auto doc = Json::parse(validation.CanonicalPayloadJson);
        SelectionInteractionConfig value;
        for (unsigned i = 0; i < 4; ++i)
            if (doc["target"] == targets[i])
                value.Target = static_cast<SelectionTarget>(i);
        value.Highlight = doc["highlight"].get<bool>();
        value.PointRadius = doc["point_radius"].get<float>();
        return value;
    }
    void SetSelectionInteractionConfig(Core::Config::EngineConfig& config,
                                       const SelectionInteractionConfig& value)
    {
        Core::Config::UpsertEngineConfigSection(config.AppSections, Section(value));
    }
    Core::Config::EngineConfigSectionRegistration MakeSelectionConfigSectionRegistration()
    {
        return {.DefaultSection = Section({}), .Validate = ValidateSelectionConfigSection};
    }
} // namespace Extrinsic::Runtime
