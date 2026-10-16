// Compiles NormalEstimation, Registration config codecs together to share JSON work.
#include <cmath>
#include <cstdint>
#include <span>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

import Extrinsic.Runtime.ConfigFieldSpec;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;

import Extrinsic.Runtime.NormalEstimationConfig;
import Extrinsic.Runtime.RegistrationConfig;

#include "Config/internal/Runtime.PointConfigJson.hpp"
#include "Config/internal/Runtime.ConfigFieldJson.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr Geometry::PropertyValueKind kVec3Kind[]{Geometry::PropertyValueKind::Vec3};
        constexpr std::string_view kNormalEstimationOrientation[]{"Unoriented", "Minimum spanning tree"};
        constexpr std::string_view kNormalEstimationWeighting[]{"Uniform", "Area", "Angle", "Area and angle", "Max"};
        constexpr std::string_view kNormalEstimationMethod[]{"point_set_pca", "mesh_face_weighted", "graph_neighborhood", "mesh_face_normals"};
        constexpr std::string_view kNormalEstimationBackend[]{"cpu_kdtree", "cpu_lbvh", "vulkan_lbvh", "vulkan"};
        constexpr ConfigFieldSpec kNormalEstimationFallbackNormalItem{.Name = "coordinate", .Type = ConfigFieldType::Float, .Min = -double(std::numeric_limits<float>::max()), .Max = double(std::numeric_limits<float>::max())};
        constexpr std::string_view kNormalEstimationBackendNames[]{"CPU KD-tree", "CPU LBVH (cached)", "Vulkan LBVH (resident PCA)", "Vulkan (GPU property residency, double precision)"};
        constexpr ConfigFieldSpec kNormalEstimationFields[]{
            {.Name = "entity", .Type = ConfigFieldType::UInt, .Description = "Entity."},
            {.Name = "k_neighbors", .Type = ConfigFieldType::UInt, .Description = "K neighbors.", .Min = 1},
            {.Name = "minimum_neighbors", .Type = ConfigFieldType::UInt, .Description = "Minimum neighbors.", .Min = 1},
            {.Name = "orientation", .Type = ConfigFieldType::Enum, .Description = "Orientation.", .EnumNames = kNormalEstimationOrientation},
            {.Name = "weighting", .Type = ConfigFieldType::Enum, .Description = "Weighting.", .EnumNames = kNormalEstimationWeighting},
            {.Name = "gpu_query_batch_size", .Type = ConfigFieldType::UInt, .Description = "Gpu query batch size.", .Min = 1, .Max = 16384},
            {.Name = "method", .Type = ConfigFieldType::StringEnum, .Description = "Method.", .EnumValues = kNormalEstimationMethod},
            {.Name = "backend", .Type = ConfigFieldType::StringEnum, .Description = "Backend.", .EnumNames = kNormalEstimationBackendNames, .EnumValues = kNormalEstimationBackend},
            {.Name = "use_radius", .Type = ConfigFieldType::Bool, .Description = "Select radius search; radius must remain positive in float storage."},
            {.Name = "orient_toward_fallback", .Type = ConfigFieldType::Bool, .Description = "Orient toward fallback."},
            {.Name = "radius", .Type = ConfigFieldType::Float, .Description = "Must remain positive in float storage when use_radius is enabled; zero is allowed otherwise.", .Min = 0, .Max = double(std::numeric_limits<float>::max())},
            {.Name = "degenerate_epsilon", .Type = ConfigFieldType::Float, .Description = "Degenerate epsilon.", .Min = 0, .ExclusiveMin = true},
            {.Name = "collinear_epsilon", .Type = ConfigFieldType::Float, .Description = "Collinear epsilon.", .Min = 0, .ExclusiveMin = true},
            {.Name = "fallback_normal", .Type = ConfigFieldType::Array, .Description = "Fallback normal.", .Item = &kNormalEstimationFallbackNormalItem, .MinItems = 3u, .MaxItems = 3u},
            {.Name = "positions", .Type = ConfigFieldType::PropertyRef, .Description = "Mesh-face normals require mesh-vertex positions and a mesh-face output (Unknown is also accepted). Other methods require distinct input/output names on the same domain.", .RefKinds = kVec3Kind},
            {.Name = "output", .Type = ConfigFieldType::PropertyRef, .Description = "Output.", .RefKinds = kVec3Kind},
        };
        constexpr std::string_view kRegistrationVariant[]{"point_to_point", "point_to_plane"};
        constexpr std::string_view kRegistrationBackend[]{"cpu_kdtree", "cpu_lbvh", "vulkan_lbvh"};
        constexpr std::string_view kRegistrationBackendNames[]{"CPU KD-tree (reference)", "CPU LBVH (cached)", "Vulkan LBVH (CPU solve)"};
        constexpr ConfigFieldSpec kRegistrationFields[]{
            {.Name = "source_entity", .Type = ConfigFieldType::UInt, .Description = "Source entity."},
            {.Name = "target_entity", .Type = ConfigFieldType::UInt, .Description = "Target entity."},
            {.Name = "max_iterations", .Type = ConfigFieldType::UInt, .Description = "Max iterations.", .Min = 1},
            {.Name = "trajectory_step", .Type = ConfigFieldType::UInt, .Description = "Trajectory step."},
            {.Name = "variant", .Type = ConfigFieldType::StringEnum, .Description = "Variant.", .EnumValues = kRegistrationVariant},
            {.Name = "backend", .Type = ConfigFieldType::StringEnum, .Description = "Backend.", .EnumNames = kRegistrationBackendNames, .EnumValues = kRegistrationBackend},
            {.Name = "max_correspondence_distance", .Type = ConfigFieldType::Float, .Description = "Max correspondence distance."},
            {.Name = "inlier_ratio", .Type = ConfigFieldType::Float, .Description = "Inlier ratio.", .Min = 0, .Max = 1, .ExclusiveMin = true},
            {.Name = "convergence_threshold", .Type = ConfigFieldType::Float, .Description = "Convergence threshold.", .Min = 0},
            {.Name = "source_positions", .Type = ConfigFieldType::PropertyRef, .Description = "Source positions.", .RefKinds = kVec3Kind},
            {.Name = "target_positions", .Type = ConfigFieldType::PropertyRef, .Description = "Target positions.", .RefKinds = kVec3Kind},
            {.Name = "target_normals", .Type = ConfigFieldType::PropertyRef, .Description = "Target normals.", .RefKinds = kVec3Kind},
        };

        using Json = nlohmann::json;
        NormalEstimationConfig ParseNormalEstimationConfig(const Json &d)
        {
            NormalEstimationConfig c;
            c.StableEntityId = d.at("entity");
            for (unsigned i = 0; i < 4; ++i)
                if (d.at("method") == ToString(NormalEstimationMethod(i)))
                    c.Method = NormalEstimationMethod(i);
            for (unsigned i = 0; i < 4; ++i)
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
        case NormalEstimationBackend::Vulkan:
            return "vulkan";
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
        auto input = ConfigDetail::ParseConfigJson(payload, false),
             d = ConfigDetail::ParseConfigJson(SerializeNormalEstimationConfig({}), true);
        if (auto error = ConfigDetail::ValidatePointConfigFields(
            input, d, "Normal estimation config must be an object.", "Unknown normal field: ",
            kNormalEstimationFields))
            return RejectConfigSection(subject, std::move(*error));
        if (!ConfigDetail::InConfigFieldRange(kNormalEstimationFields, "k_neighbors", d["k_neighbors"].get<double>()) || !ConfigDetail::InConfigFieldRange(kNormalEstimationFields, "minimum_neighbors", d["minimum_neighbors"].get<double>()) || !ConfigDetail::InConfigFieldRange(kNormalEstimationFields, "orientation", d["orientation"].get<double>()) ||
            !ConfigDetail::InConfigFieldRange(kNormalEstimationFields, "weighting", d["weighting"].get<double>()))
            return RejectConfigSection(subject, "Positive neighborhood sizes, orientation 0..1 and weighting 0..4 are required.");
        if (!ConfigDetail::IsConfigFieldToken(kNormalEstimationFields, "method", d["method"]))
            return RejectConfigSection(subject, "Unknown normal method.");
        if (!ConfigDetail::IsConfigFieldToken(kNormalEstimationFields, "backend", d["backend"]))
            return RejectConfigSection(subject, "Normal backend must be cpu_kdtree, cpu_lbvh, vulkan_lbvh or vulkan.");
        if (!ConfigDetail::InConfigFieldRange(kNormalEstimationFields, "gpu_query_batch_size", d["gpu_query_batch_size"].get<double>()))
            return RejectConfigSection(subject, "GPU query batch size must be in 1..16384.");
        if (!d["use_radius"].is_boolean() || !d["orient_toward_fallback"].is_boolean())
            return RejectConfigSection(subject, "Normal toggles must be boolean.");
        for (auto key : {"radius", "degenerate_epsilon", "collinear_epsilon"})
            if (!d[key].is_number() || !std::isfinite(d[key].get<double>()))
                return RejectConfigSection(subject, std::string(key) + " must be finite.");
        if (!ConfigDetail::InConfigFieldRange(kNormalEstimationFields, "radius", d["radius"].get<double>()) ||
            (d["use_radius"].get<bool>() && d["radius"].get<float>() <= 0) || !ConfigDetail::InConfigFieldRange(kNormalEstimationFields, "degenerate_epsilon", d["degenerate_epsilon"].get<double>()) ||
            !ConfigDetail::InConfigFieldRange(kNormalEstimationFields, "collinear_epsilon", d["collinear_epsilon"].get<double>()))
            return RejectConfigSection(subject, "Normal radii and numerical epsilons are outside their supported range.");
        auto &fallback = d["fallback_normal"];
        if (!fallback.is_array() || fallback.size() != *FindConfigFieldSpec(kNormalEstimationFields, "fallback_normal")->MinItems)
            return RejectConfigSection(subject, "Fallback normal must have three finite float coordinates.");
        for (auto &x : fallback)
            if (!x.is_number() || !std::isfinite(x.get<double>()) ||
                !AcceptsConfigFieldNumber(*FindConfigFieldSpec(kNormalEstimationFields, "fallback_normal")->Item, x.get<double>()))
                return RejectConfigSection(subject, "Fallback normal coordinates must be finite floats.");
        for (auto key : {"positions", "output"})
        {
            const auto validation = ConfigDetail::ValidatePointPropertyRef(
                d[key], FindConfigFieldSpec(kNormalEstimationFields, key)->RefKinds.front());
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
        return ConfigDetail::AcceptConfigSection(SerializeNormalEstimationConfig(ParseNormalEstimationConfig(d)), input.size());
    }
    std::optional<NormalEstimationConfig> GetNormalEstimationConfig(const Core::Config::EngineConfig &c)
    {
        return ConfigDetail::GetPointConfig(c, kNormalEstimationConfigSectionName, kNormalEstimationConfigSectionSchemaId, 1u, ValidateNormalEstimationConfigSection, ParseNormalEstimationConfig);
    }
    void SetNormalEstimationConfig(Core::Config::EngineConfig &c, const NormalEstimationConfig &v)
    {
        Core::Config::UpsertEngineConfigSection(c.AppSections, ConfigDetail::MakeConfigSection(
            kNormalEstimationConfigSectionName, kNormalEstimationConfigSectionSchemaId, 1, SerializeNormalEstimationConfig(v)));
    }
    std::span<const ConfigFieldSpec> NormalEstimationConfigFieldSpecs() noexcept
    { return kNormalEstimationFields; }
    Core::Config::EngineConfigSectionRegistration MakeNormalEstimationConfigSectionRegistration()
    {
        return ConfigDetail::MakeSectionRegistration(
            kNormalEstimationConfigSectionName, kNormalEstimationConfigSectionSchemaId, 1, SerializeNormalEstimationConfig({}), ValidateNormalEstimationConfigSection,
            ConfigDetail::BuildSectionSchemaJson(kNormalEstimationConfigSectionSchemaId, "NormalEstimation", "Validated NormalEstimation configuration.",
                kNormalEstimationFields, ConfigDetail::ParseConfigJson(SerializeNormalEstimationConfig({}), true)));
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
        const auto input = ConfigDetail::ParseConfigJson(payload, false);
        auto doc = ConfigDetail::ParseConfigJson(SerializeRegistrationConfig({}), true);
        if (auto error = ConfigDetail::ValidatePointConfigFields(
            input, doc, "Registration config must be an object.", "Unknown registration field: ",
            kRegistrationFields))
            return RejectConfigSection(subject, std::move(*error));
        if (!ConfigDetail::InConfigFieldRange(kRegistrationFields, "max_iterations", doc["max_iterations"].get<double>())) return RejectConfigSection(subject, "max_iterations must be positive.");
        for (auto key : {"max_correspondence_distance", "inlier_ratio", "convergence_threshold"})
            if (!doc[key].is_number() || !std::isfinite(doc[key].get<double>()))
                return RejectConfigSection(subject, std::string(key) + " must be finite.");
        if (!ConfigDetail::InConfigFieldRange(kRegistrationFields, "inlier_ratio", doc["inlier_ratio"].get<double>()) || !ConfigDetail::InConfigFieldRange(kRegistrationFields, "convergence_threshold", doc["convergence_threshold"].get<double>()))
            return RejectConfigSection(subject, "inlier_ratio must be in (0,1]; convergence_threshold must be nonnegative.");
        if (!ConfigDetail::IsConfigFieldToken(kRegistrationFields, "variant", doc["variant"]))
            return RejectConfigSection(subject, "variant must be point_to_point or point_to_plane.");
        if (!ConfigDetail::IsConfigFieldToken(kRegistrationFields, "backend", doc["backend"]))
            return RejectConfigSection(subject, "backend must be cpu_kdtree, cpu_lbvh or vulkan_lbvh.");
        for (auto key : {"source_positions", "target_positions", "target_normals"})
        {
            const auto& ref = doc[key];
            using ConfigDetail::PointPropertyValidation;
            const auto validation = ConfigDetail::ValidatePointPropertyRef(ref, FindConfigFieldSpec(kRegistrationFields, key)->RefKinds.front());
            if (validation == PointPropertyValidation::InvalidReference || !ref["domain"].is_string())
                return RejectConfigSection(subject, std::string(key) + " requires domain, name and kind=vec3.");
            if (validation == PointPropertyValidation::UnknownDomain)
                return RejectConfigSection(subject, std::string(key) + " has an unknown element domain.");
        }
        return ConfigDetail::AcceptConfigSection(SerializeRegistrationConfig(ParseRegistrationConfig(doc)), input.size());
    }
    std::optional<RegistrationConfig> GetRegistrationConfig(const Core::Config::EngineConfig& config)
    {
        return ConfigDetail::GetPointConfig(config, kRegistrationConfigSectionName, kRegistrationConfigSectionSchemaId, 1u, ValidateRegistrationConfigSection, ParseRegistrationConfig);
    }
    void SetRegistrationConfig(Core::Config::EngineConfig& config, const RegistrationConfig& value)
    {
        Core::Config::UpsertEngineConfigSection(config.AppSections, ConfigDetail::MakeConfigSection(
            kRegistrationConfigSectionName, kRegistrationConfigSectionSchemaId, 1, SerializeRegistrationConfig(value)));
    }
    std::span<const ConfigFieldSpec> RegistrationConfigFieldSpecs() noexcept
    { return kRegistrationFields; }
    Core::Config::EngineConfigSectionRegistration MakeRegistrationConfigSectionRegistration()
    {
        return ConfigDetail::MakeSectionRegistration(
            kRegistrationConfigSectionName, kRegistrationConfigSectionSchemaId, 1, SerializeRegistrationConfig({}), ValidateRegistrationConfigSection,
            ConfigDetail::BuildSectionSchemaJson(kRegistrationConfigSectionSchemaId, "Registration", "Validated Registration configuration.",
                kRegistrationFields, ConfigDetail::ParseConfigJson(SerializeRegistrationConfig({}), true)));
    }
}
