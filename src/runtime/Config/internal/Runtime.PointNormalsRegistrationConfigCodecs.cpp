// Compiles NormalEstimation, Registration config codecs together to share JSON work.
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;

import Extrinsic.Runtime.NormalEstimationConfig;
import Extrinsic.Runtime.RegistrationConfig;

#include "Config/internal/Runtime.PointConfigJson.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        using Json = nlohmann::json;
        NormalEstimationConfig ParseNormalEstimationConfig(const Json &d)
        {
            NormalEstimationConfig c;
            c.StableEntityId = d.at("entity");
            for (unsigned i = 0; i < 4; ++i)
                if (d.at("method") == ToString(NormalEstimationMethod(i)))
                    c.Method = NormalEstimationMethod(i);
            for (unsigned i = 0; i < 3; ++i)
                if (d.at("backend") == ToString(NormalEstimationBackend(i)))
                    c.Backend = NormalEstimationBackend(i);
            ConfigDetail::DecodePointPropertyRef(d.at("positions"), c.Positions);
            ConfigDetail::DecodePointPropertyRef(d.at("output"), c.Output);
            c.KNeighbors = d.at("k_neighbors");
            c.MinimumNeighbors = d.at("minimum_neighbors");
            c.GpuQueryBatchSize = d.at("gpu_query_batch_size");
            c.UseRadiusSearch = d.at("use_radius");
            c.Radius = d.at("radius");
            c.Orientation =
                Geometry::PointCloud::Normals::OrientationMode(d.at("orientation").get<unsigned>());
            for (unsigned i = 0; i < 3; ++i)
                c.FallbackNormal[i] = d.at("fallback_normal").at(i);
            c.DegenerateNormalLengthEpsilon = d.at("degenerate_epsilon");
            c.CollinearEigenvalueRatioEpsilon = d.at("collinear_epsilon");
            c.Weighting =
                Geometry::HalfedgeMesh::VertexNormals::AveragingMode(d.at("weighting").get<unsigned>());
            c.OrientTowardFallback = d.at("orient_toward_fallback");
            return c;
        }
        Core::Config::EngineConfigSection MakeNormalEstimationConfigSection(const NormalEstimationConfig &c)
        {
            return {.Name = std::string{kNormalEstimationConfigSectionName},
                    .SchemaId = std::string{kNormalEstimationConfigSectionSchemaId},
                    .SchemaVersion = 1,
                    .PayloadJson = SerializeNormalEstimationConfig(c)};
        }
    } // namespace
    const char *ToString(NormalEstimationMethod m) noexcept
    {
        switch (m)
        {
        case NormalEstimationMethod::PointSetPCA:
            return "point_set_pca";
        case NormalEstimationMethod::MeshFaceWeighted:
            return "mesh_face_weighted";
        case NormalEstimationMethod::GraphNeighborhood:
            return "graph_neighborhood";
        case NormalEstimationMethod::MeshFaceNormals:
            return "mesh_face_normals";
        }
        return "invalid";
    }
    const char *ToString(NormalEstimationBackend b) noexcept
    {
        switch (b)
        {
        case NormalEstimationBackend::CpuKDTree:
            return "cpu_kdtree";
        case NormalEstimationBackend::CpuLBVH:
            return "cpu_lbvh";
        case NormalEstimationBackend::VulkanLBVH:
            return "vulkan_lbvh";
        }
        return "invalid";
    }
    std::string SerializeNormalEstimationConfig(const NormalEstimationConfig &c)
    {
        return ConfigDetail::SerializeConfigJson(Json{{"entity", c.StableEntityId},
                    {"method", ToString(c.Method)},
                    {"backend", ToString(c.Backend)},
                    {"positions", ConfigDetail::EncodeVec3PointPropertyRef(c.Positions)},
                    {"output", ConfigDetail::EncodeVec3PointPropertyRef(c.Output)},
                    {"k_neighbors", c.KNeighbors},
                    {"minimum_neighbors", c.MinimumNeighbors},
                    {"gpu_query_batch_size", c.GpuQueryBatchSize},
                    {"use_radius", c.UseRadiusSearch},
                    {"radius", c.Radius},
                    {"orientation", unsigned(c.Orientation)},
                    {"fallback_normal", {c.FallbackNormal.x, c.FallbackNormal.y, c.FallbackNormal.z}},
                    {"degenerate_epsilon", c.DegenerateNormalLengthEpsilon},
                    {"collinear_epsilon", c.CollinearEigenvalueRatioEpsilon},
                    {"weighting", unsigned(c.Weighting)},
                    {"orient_toward_fallback", c.OrientTowardFallback}});
    }
    Core::Config::EngineConfigSectionValidationResult ValidateNormalEstimationConfigSection(
        std::string_view payload, std::string_view, std::string_view subject)
    {
        using namespace Core::Config;
        using ConfigDetail::RejectConfigSection;
        EngineConfigSectionValidationResult result;
        auto input = ConfigDetail::ParseConfigJson(payload, false),
             d = ConfigDetail::ParseConfigJson(SerializeNormalEstimationConfig({}), true);
        if (auto error = ConfigDetail::ValidatePointConfigFields(
            input, d, "Normal estimation config must be an object.", "Unknown normal field: ",
            {"entity", "k_neighbors", "minimum_neighbors",
             "orientation", "weighting", "gpu_query_batch_size"}))
            return RejectConfigSection(subject, std::move(*error));
        if (d["k_neighbors"] == 0 || d["minimum_neighbors"] == 0 || d["orientation"] > 1 ||
            d["weighting"] > 4)
            return RejectConfigSection(subject, "Positive neighborhood sizes, orientation 0..1 and weighting 0..4 are required.");
        if (d["method"] != "point_set_pca" && d["method"] != "mesh_face_weighted" &&
            d["method"] != "graph_neighborhood" && d["method"] != "mesh_face_normals")
            return RejectConfigSection(subject, "Unknown normal method.");
        if (d["backend"] != "cpu_kdtree" && d["backend"] != "cpu_lbvh" && d["backend"] != "vulkan_lbvh")
            return RejectConfigSection(subject, "Normal backend must be cpu_kdtree, cpu_lbvh or vulkan_lbvh.");
        if (d["gpu_query_batch_size"] == 0 || d["gpu_query_batch_size"] > 16384)
            return RejectConfigSection(subject, "GPU query batch size must be in 1..16384.");
        if (!d["use_radius"].is_boolean() || !d["orient_toward_fallback"].is_boolean())
            return RejectConfigSection(subject, "Normal toggles must be boolean.");
        for (auto key : {"radius", "degenerate_epsilon", "collinear_epsilon"})
            if (!d[key].is_number() || !std::isfinite(d[key].get<double>()))
                return RejectConfigSection(subject, std::string(key) + " must be finite.");
        if (d["radius"].get<double>() < 0 || d["radius"].get<double>() > std::numeric_limits<float>::max() ||
            (d["use_radius"].get<bool>() && d["radius"] <= 0) || d["degenerate_epsilon"] <= 0 ||
            d["collinear_epsilon"] <= 0)
            return RejectConfigSection(subject, "Normal radii and numerical epsilons are outside their supported range.");
        auto &fallback = d["fallback_normal"];
        if (!fallback.is_array() || fallback.size() != 3)
            return RejectConfigSection(subject, "Fallback normal must have three finite float coordinates.");
        for (auto &x : fallback)
            if (!x.is_number() || !std::isfinite(x.get<double>()) ||
                std::abs(x.get<double>()) > std::numeric_limits<float>::max())
                return RejectConfigSection(subject, "Fallback normal coordinates must be finite floats.");
        for (auto key : {"positions", "output"})
        {
            const auto validation = ConfigDetail::ValidatePointPropertyRef(
                d[key], Geometry::PropertyValueKind::Vec3);
            if (validation == ConfigDetail::PointPropertyValidation::InvalidReference)
                return RejectConfigSection(subject, std::string(key) + " requires domain, nonempty name and kind=vec3.");
            if (validation == ConfigDetail::PointPropertyValidation::UnknownDomain)
                return RejectConfigSection(subject, std::string(key) + " has an unknown element domain.");
        }
        if (d["method"] == "mesh_face_normals")
        {
            for (const auto key : {"positions", "output"})
            {
                const auto expected = key == std::string_view{"positions"}
                    ? GeometryElementDomain::MeshVertex : GeometryElementDomain::MeshFace;
                if (d[key]["domain"] != ToString(expected) &&
                    d[key]["domain"] != ToString(GeometryElementDomain::Unknown))
                    return RejectConfigSection(subject, "Face normals require mesh vertex positions and a mesh face output.");
            }
        }
        else if (d["positions"]["domain"] != d["output"]["domain"] ||
                 d["positions"]["name"] == d["output"]["name"])
            return RejectConfigSection(subject, "Normals must use a distinct output property on the input domain.");
        result.State = EngineConfigState::Valid;
        result.CanonicalPayloadJson = SerializeNormalEstimationConfig(ParseNormalEstimationConfig(d));
        result.ParsedFieldCount = input.size();
        return result;
    }
    std::optional<NormalEstimationConfig> GetNormalEstimationConfig(const Core::Config::EngineConfig &c)
    {
        const auto payload = ConfigDetail::FindValidatedCanonicalPayload(
            c, kNormalEstimationConfigSectionName, kNormalEstimationConfigSectionSchemaId, 1u,
            nullptr, ValidateNormalEstimationConfigSection);
        if (!payload) return {};
        return ParseNormalEstimationConfig(ConfigDetail::ParseConfigJson(*payload, true));
    }
    void SetNormalEstimationConfig(Core::Config::EngineConfig &c, const NormalEstimationConfig &v)
    {
        Core::Config::UpsertEngineConfigSection(c.AppSections, MakeNormalEstimationConfigSection(v));
    }
    Core::Config::EngineConfigSectionRegistration MakeNormalEstimationConfigSectionRegistration()
    {
        return {.DefaultSection = MakeNormalEstimationConfigSection({}), .Validate = ValidateNormalEstimationConfigSection};
    }
} // namespace Extrinsic::Runtime

namespace Extrinsic::Runtime
{
    namespace
    {
        using Json = nlohmann::json;
        RegistrationConfig ParseRegistrationConfig(const Json& doc)
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
        Core::Config::EngineConfigSection MakeRegistrationConfigSection(const RegistrationConfig& value)
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
        using ConfigDetail::RejectConfigSection;
        EngineConfigSectionValidationResult result;
        const auto input = ConfigDetail::ParseConfigJson(payload, false);
        auto doc = ConfigDetail::ParseConfigJson(SerializeRegistrationConfig({}), true);
        if (auto error = ConfigDetail::ValidatePointConfigFields(
            input, doc, "Registration config must be an object.", "Unknown registration field: ",
            {"source_entity", "target_entity", "max_iterations", "trajectory_step"}))
            return RejectConfigSection(subject, std::move(*error));
        if (doc["max_iterations"] == 0) return RejectConfigSection(subject, "max_iterations must be positive.");
        for (auto key : {"max_correspondence_distance", "inlier_ratio", "convergence_threshold"})
            if (!doc[key].is_number() || !std::isfinite(doc[key].get<double>()))
                return RejectConfigSection(subject, std::string(key) + " must be finite.");
        if (doc["inlier_ratio"] <= 0 || doc["inlier_ratio"] > 1 || doc["convergence_threshold"] < 0)
            return RejectConfigSection(subject, "inlier_ratio must be in (0,1]; convergence_threshold must be nonnegative.");
        if (doc["variant"] != "point_to_point" && doc["variant"] != "point_to_plane")
            return RejectConfigSection(subject, "variant must be point_to_point or point_to_plane.");
        if (doc["backend"] != "cpu_kdtree" && doc["backend"] != "cpu_lbvh" && doc["backend"] != "vulkan_lbvh")
            return RejectConfigSection(subject, "backend must be cpu_kdtree, cpu_lbvh or vulkan_lbvh.");
        for (auto key : {"source_positions", "target_positions", "target_normals"})
        {
            const auto& ref = doc[key];
            using ConfigDetail::PointPropertyValidation;
            const auto validation = ConfigDetail::ValidatePointPropertyRef(ref, Geometry::PropertyValueKind::Vec3);
            if (validation == PointPropertyValidation::InvalidReference || !ref["domain"].is_string())
                return RejectConfigSection(subject, std::string(key) + " requires domain, name and kind=vec3.");
            if (validation == PointPropertyValidation::UnknownDomain)
                return RejectConfigSection(subject, std::string(key) + " has an unknown element domain.");
        }
        result.State = EngineConfigState::Valid;
        result.CanonicalPayloadJson = SerializeRegistrationConfig(ParseRegistrationConfig(doc));
        result.ParsedFieldCount = static_cast<std::uint32_t>(input.size());
        return result;
    }
    std::optional<RegistrationConfig> GetRegistrationConfig(const Core::Config::EngineConfig& config)
    {
        const auto payload = ConfigDetail::FindValidatedCanonicalPayload(
            config, kRegistrationConfigSectionName, kRegistrationConfigSectionSchemaId, 1u,
            nullptr, ValidateRegistrationConfigSection);
        if (!payload) return {};
        return ParseRegistrationConfig(ConfigDetail::ParseConfigJson(*payload, true));
    }
    void SetRegistrationConfig(Core::Config::EngineConfig& config, const RegistrationConfig& value)
    {
        Core::Config::UpsertEngineConfigSection(config.AppSections, MakeRegistrationConfigSection(value));
    }
    Core::Config::EngineConfigSectionRegistration MakeRegistrationConfigSectionRegistration()
    {
        return {.DefaultSection = MakeRegistrationConfigSection({}), .Validate = ValidateRegistrationConfigSection};
    }
}
