module;
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
module Extrinsic.Runtime.RegistrationConfig;
#include "Config/internal/Runtime.PointConfigJson.hpp"
namespace Extrinsic::Runtime
{
    namespace
    {
        using Json = nlohmann::json;
        RegistrationConfig Parse(const Json& doc)
        {
            RegistrationConfig c;
            c.SourceStableEntityId = doc.at("source_entity");
            c.TargetStableEntityId = doc.at("target_entity");
            c.Variant = doc.at("variant") == "point_to_plane" ? EditorICPVariant::PointToPlane : EditorICPVariant::PointToPoint;
            c.Backend = doc.at("backend") == "vulkan_lbvh" ? RegistrationBackend::VulkanLBVH :
                        doc.at("backend") == "cpu_lbvh" ? RegistrationBackend::CpuLBVH : RegistrationBackend::CpuKDTree;
            c.MaxIterations = doc.at("max_iterations");
            c.MaxCorrespondenceDistance = doc.at("max_correspondence_distance");
            c.InlierRatio = doc.at("inlier_ratio");
            c.ConvergenceThreshold = doc.at("convergence_threshold");
            c.TrajectoryStep = doc.at("trajectory_step");
            ConfigDetail::DecodePointPropertyRef(doc.at("source_positions"), c.SourcePositions);
            ConfigDetail::DecodePointPropertyRef(doc.at("target_positions"), c.TargetPositions);
            ConfigDetail::DecodePointPropertyRef(doc.at("target_normals"), c.TargetNormals);
            return c;
        }
        Core::Config::EngineConfigSection Section(const RegistrationConfig& value)
        {
            return {.Name = std::string{kRegistrationConfigSectionName},
                    .SchemaId = std::string{kRegistrationConfigSectionSchemaId}, .SchemaVersion = 1,
                    .PayloadJson = SerializeRegistrationConfig(value)};
        }
    }
    const char* ToString(RegistrationBackend backend) noexcept
    {
        switch (backend)
        {
        case RegistrationBackend::CpuKDTree: return "cpu_kdtree";
        case RegistrationBackend::CpuLBVH: return "cpu_lbvh";
        case RegistrationBackend::VulkanLBVH: return "vulkan_lbvh";
        }
        return "invalid";
    }
    std::string SerializeRegistrationConfig(const RegistrationConfig& c)
    {
        return ConfigDetail::SerializeConfigJson(Json{{"source_entity", c.SourceStableEntityId}, {"target_entity", c.TargetStableEntityId},
            {"source_positions", ConfigDetail::EncodeVec3PointPropertyRef(c.SourcePositions)}, {"target_positions", ConfigDetail::EncodeVec3PointPropertyRef(c.TargetPositions)},
            {"target_normals", ConfigDetail::EncodeVec3PointPropertyRef(c.TargetNormals)}, {"backend", ToString(c.Backend)},
            {"variant", c.Variant == EditorICPVariant::PointToPlane ? "point_to_plane" :
                        c.Variant == EditorICPVariant::PointToPoint ? "point_to_point" : "invalid"},
            {"max_iterations", c.MaxIterations}, {"max_correspondence_distance", c.MaxCorrespondenceDistance},
            {"inlier_ratio", c.InlierRatio}, {"trajectory_step", c.TrajectoryStep},
            {"convergence_threshold", c.ConvergenceThreshold}});
    }
    Core::Config::EngineConfigSectionValidationResult ValidateRegistrationConfigSection(
        std::string_view payload, std::string_view, std::string_view subject)
    {
        using namespace Core::Config;
        EngineConfigSectionValidationResult result;
        auto reject = [&](std::string message) {
            result.Diagnostics.push_back({.Code = EngineConfigDiagnosticCode::InvalidValue,
                .Subject = std::string{subject}, .Message = std::move(message)});
            return result;
        };
        const auto input = ConfigDetail::ParseConfigJson(payload, false);
        auto doc = ConfigDetail::ParseConfigJson(SerializeRegistrationConfig({}), true);
        if (auto error = ConfigDetail::ValidatePointConfigFields(
            input, doc, "Registration config must be an object.", "Unknown registration field: ",
            {"source_entity", "target_entity", "max_iterations", "trajectory_step"}))
            return reject(std::move(*error));
        if (doc["max_iterations"] == 0) return reject("max_iterations must be positive.");
        for (auto key : {"max_correspondence_distance", "inlier_ratio", "convergence_threshold"})
            if (!doc[key].is_number() || !std::isfinite(doc[key].get<double>()))
                return reject(std::string(key) + " must be finite.");
        if (doc["inlier_ratio"] <= 0 || doc["inlier_ratio"] > 1 || doc["convergence_threshold"] < 0)
            return reject("inlier_ratio must be in (0,1]; convergence_threshold must be nonnegative.");
        if (doc["variant"] != "point_to_point" && doc["variant"] != "point_to_plane")
            return reject("variant must be point_to_point or point_to_plane.");
        if (doc["backend"] != "cpu_kdtree" && doc["backend"] != "cpu_lbvh" && doc["backend"] != "vulkan_lbvh")
            return reject("backend must be cpu_kdtree, cpu_lbvh or vulkan_lbvh.");
        for (auto key : {"source_positions", "target_positions", "target_normals"})
        {
            const auto& ref = doc[key];
            using ConfigDetail::PointPropertyValidation;
            const auto validation = ConfigDetail::ValidatePointPropertyRef(ref, Geometry::PropertyValueKind::Vec3);
            if (validation == PointPropertyValidation::InvalidReference || !ref["domain"].is_string())
                return reject(std::string(key) + " requires domain, name and kind=vec3.");
            if (validation == PointPropertyValidation::UnknownDomain)
                return reject(std::string(key) + " has an unknown element domain.");
        }
        result.State = EngineConfigState::Valid;
        result.CanonicalPayloadJson = SerializeRegistrationConfig(Parse(doc));
        result.ParsedFieldCount = static_cast<std::uint32_t>(input.size());
        return result;
    }
    std::optional<RegistrationConfig> GetRegistrationConfig(const Core::Config::EngineConfig& config)
    {
        const auto payload = ConfigDetail::FindValidatedCanonicalPayload(
            config, kRegistrationConfigSectionName, kRegistrationConfigSectionSchemaId, 1u,
            nullptr, ValidateRegistrationConfigSection);
        if (!payload) return {};
        return Parse(ConfigDetail::ParseConfigJson(*payload, true));
    }
    void SetRegistrationConfig(Core::Config::EngineConfig& config, const RegistrationConfig& value)
    {
        Core::Config::UpsertEngineConfigSection(config.AppSections, Section(value));
    }
    Core::Config::EngineConfigSectionRegistration MakeRegistrationConfigSectionRegistration()
    {
        return {.DefaultSection = Section({}), .Validate = ValidateRegistrationConfigSection};
    }
}
