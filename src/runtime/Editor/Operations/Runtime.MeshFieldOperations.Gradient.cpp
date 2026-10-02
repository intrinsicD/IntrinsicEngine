// Triangle gradients publish on original face slots through guarded editor history.
module;
#include "GeometryIntegration/Runtime.GeometryValueComparison.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
module Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.EditorCommandHistory;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Utils;
import Geometry.Properties;
#include "Config/internal/Runtime.PointConfigJson.hpp"
#include "Config/internal/Runtime.ConfigFieldJson.hpp"
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.ActionReadinessConfig.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.MeshSources.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.MeshReadiness.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace GS = ECS::Components::GeometrySources;
        namespace MS = GeometryProcessingDetail::MeshSupport;
        using Json = nlohmann::json;
        constexpr std::string_view kSchema = "intrinsic.runtime.sandbox.scalar_gradient";
        using FT = ConfigFieldType;
        using K = Geometry::PropertyValueKind;
        constexpr std::array<K, 1> kVec3{K::Vec3};
        constexpr std::array<K, 1> kScalar{K::Double};
        constexpr std::array<GeometryElementDomain, 1> kVertex{GeometryElementDomain::MeshVertex};
        constexpr std::array<GeometryElementDomain, 1> kFace{GeometryElementDomain::MeshFace};
        constexpr std::array kFields{
            ConfigFieldSpec{.Name = "positions", .Type = FT::PropertyRef, .Description = "Vertex positions.", .RefKinds = kVec3, .RefDomains = kVertex},
            ConfigFieldSpec{.Name = "scalar", .Type = FT::PropertyRef, .Description = "Scalar vertex property to differentiate (any scalar storage).", .RefKinds = kScalar, .RefDomains = kVertex, .AnyScalar = true},
            ConfigFieldSpec{.Name = "output", .Type = FT::PropertyRef, .Description = "Face vec3 property receiving the per-face gradient.", .RefKinds = kVec3, .RefDomains = kFace},
        };

        ScalarGradientConfig DecodeGradient(const Json& doc)
        {
            ScalarGradientConfig c;
            ConfigDetail::DecodePointPropertyRef(doc.at("positions"), c.Positions);
            ConfigDetail::DecodePointPropertyRef(doc.at("scalar"), c.Scalar);
            ConfigDetail::DecodePointPropertyRef(doc.at("output"), c.Output);
            return c;
        }
        Core::Config::EngineConfigSectionValidationResult ValidateGradient(
            std::string_view payload, std::string_view, std::string_view subject)
        {
            const auto doc = Json::parse(payload, nullptr, false);
            auto merged = Json::parse(SerializeScalarGradientConfig({}));
            if (auto error = ConfigDetail::ValidateDeclaredFields(doc, merged, kFields,
                    "Scalar gradient config must be an object.", "Unknown gradient field: "))
                return ConfigDetail::RejectConfigSection(subject, *error);
            for (const auto key : {"positions", "scalar", "output"})
            {
                const auto domain = std::string_view{key} == "output"
                    ? GeometryElementDomain::MeshFace : GeometryElementDomain::MeshVertex;
                const auto name = merged[key]["name"].get<std::string>();
                if (name.find('\0') != std::string::npos ||
                    (std::string_view{key} == "output" && IsTopologyProperty(domain, name)))
                    return ConfigDetail::RejectConfigSection(subject, "Invalid or structural gradient output name.");
            }
            return {.State = Core::Config::EngineConfigState::Valid,
                    .CanonicalPayloadJson = SerializeScalarGradientConfig(DecodeGradient(merged)),
                    .ParsedFieldCount = static_cast<std::uint32_t>(doc.size())};
        }
        Core::Config::EngineConfigSection GradientSection(const ScalarGradientConfig& c)
        {
            return {.Name = std::string{kScalarGradientConfigSectionName},
                    .SchemaId = std::string{kSchema}, .SchemaVersion = 1u,
                    .PayloadJson = SerializeScalarGradientConfig(c)};
        }
        // Every independent blocking reason in check order (see the smoothing owner); the scalar
        // and output checks need a valid triangle mesh.
        std::optional<ECS::EntityHandle> GradientTarget(
            const EditorProcessingContext& context, std::uint32_t id,
            const ScalarGradientConfig& c, std::vector<ActionReadinessReason>& reasons)
        {
            using C = ActionReadinessCode;
            const auto payload = SerializeScalarGradientConfig(c);
            AppendConfigReadinessReasons(reasons, payload, kFields, ValidateGradient(payload, {}, kScalarGradientConfigSectionName));
            if (!context.Scene)
            {
                reasons.push_back({C::WorkspaceUnavailable, {}, "Scene is unavailable."});
                return {};
            }
            // A cross-field rejection already explains the property names it concerns.
            const bool conflicting = std::ranges::any_of(reasons,
                [](const ActionReadinessReason& reason) { return reason.Code == C::ConflictingOptions; });
            const auto open = [&](std::string_view field) { return !ReadinessNamesField(reasons, field); };
            const auto entity = EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(), id);
            if (!entity)
            {
                reasons.push_back({C::MissingEntity, {}, "Choose an existing mesh entity."});
                return {};
            }
            const auto view = GS::BuildConstView(context.Scene->Raw(), *entity);
            if (!open("positions")) return {}; // the mesh is validated through its positions
            if (std::string diagnostic;
                MS::ValidateMeshSoupSourceMetadata(view, diagnostic, c.Positions.Name) != EditorCommandStatus::Applied)
            {
                // Metadata and face-ring topology failures concern the mesh, not the positions control.
                reasons.push_back({C::Unclassified, {}, std::move(diagnostic)});
                return {};
            }
            if (conflicting) return {};
            if (open("scalar") && DetectGeometryPropertyValueKind(view.VertexSource->Properties, c.Scalar.Name) != c.Scalar.ValueKind)
                reasons.push_back({C::MissingProperty, "scalar", "Choose an existing scalar vertex property with matching storage type."});
            const auto& faces = view.FaceSource->Properties;
            const auto output = faces.Get<glm::vec3>(c.Output.Name);
            if (open("output") && faces.Exists(c.Output.Name) && (!output || output.Size() != faces.Size()))
                reasons.push_back({C::IncompatibleProperty, "output", "The gradient output already exists with incompatible storage."});
            if (!reasons.empty()) return {};
            return entity;
        }
    }

    std::string SerializeScalarGradientConfig(const ScalarGradientConfig& c)
    {
        return Json{{"positions", ConfigDetail::EncodePointPropertyRef(c.Positions)},
                    {"scalar", ConfigDetail::EncodePointPropertyRef(c.Scalar)},
                    {"output", ConfigDetail::EncodePointPropertyRef(c.Output)}}.dump();
    }
    Core::Config::EngineConfigSectionRegistration MakeScalarGradientConfigSectionRegistration()
    {
        return {.DefaultSection = GradientSection({}), .Validate = ValidateGradient,
                .SchemaJson = ConfigDetail::BuildSectionSchemaJson(kSchema, "Scalar Gradient",
                    "Per-face gradient of a scalar vertex property on a triangle mesh.",
                    kFields, Json::parse(SerializeScalarGradientConfig({})))};
    }
    std::span<const ConfigFieldSpec> ScalarGradientConfigFieldSpecs() noexcept { return kFields; }
    RuntimeEngineConfigApplyResult ApplyEditorScalarGradientConfig(
        const EditorProcessingCommands& commands, const ScalarGradientConfig& c)
    {
        return ApplyEditorProcessingConfig(commands,
            ValidateGradient(SerializeScalarGradientConfig(c), {}, kScalarGradientConfigSectionName),
            std::string{kScalarGradientConfigSectionName},
            [&](Core::Config::EngineConfig& config) {
                Core::Config::UpsertEngineConfigSection(config.AppSections, GradientSection(c));
            });
    }
    std::optional<ScalarGradientConfig> GetEditorScalarGradientConfig(const EditorProcessingCommands& commands)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (!context.EngineConfigControlState) return {};
        const auto payload = ConfigDetail::FindValidatedCanonicalPayload(
            context.EngineConfigControlState->ActiveConfig, kScalarGradientConfigSectionName,
            kSchema, 1u, nullptr, ValidateGradient);
        return payload ? std::optional{DecodeGradient(Json::parse(*payload))} : std::nullopt;
    }
    ActionReadiness PreviewEditorScalarGradientCommand(
        const EditorProcessingCommands& commands, std::uint32_t id, const ScalarGradientConfig& c)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        std::vector<ActionReadinessReason> reasons;
        // Face rings need every other check to pass, so they add at most the one reason.
        if (const auto entity = GradientTarget(context, id, c, reasons); entity)
            if (std::string diagnostic; !MS::PrepareMeshSoupFaceRings(context, *entity,
                    BuildGeometryAvailability(context.Scene->Raw(), *entity), diagnostic, c.Positions, true))
                reasons.push_back({ActionReadinessCode::Unclassified, {}, std::move(diagnostic)}); // the mesh, not the positions control
        return MakeActionReadiness(std::move(reasons));
    }
    EditorScalarGradientResult ApplyEditorScalarGradientCommand(
        const EditorProcessingCommands& commands, std::uint32_t id, const ScalarGradientConfig& c)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        EditorScalarGradientResult result;
        const auto fail = [&](std::string message) {
            result.Status = EditorCommandStatus::InvalidProcessingParameters;
            result.Message = std::move(message);
            return result;
        };
        std::vector<ActionReadinessReason> reasons;
        const auto entity = GradientTarget(context, id, c, reasons);
        if (!entity) return fail(reasons.front().Message);
        const auto view = GS::BuildConstView(context.Scene->Raw(), *entity);
        if (MS::ValidateMeshSoupFaceRings(view, result.Message, c.Positions.Name, true) != EditorCommandStatus::Applied)
            return fail(result.Message);
        auto source = MS::BuildHalfedgeMeshForProcessing(view, "Scalar gradient", c.Positions.Name);
        if (!source.Succeeded()) return fail(source.Diagnostic);
        const auto scalar = CaptureGeometryScalarProperty(view.VertexSource->Properties, c.Scalar);
        if (!scalar.Exists || GeometryScalarPropertySize(scalar) != source.Mesh.VerticesSize())
            return fail("The scalar input has incompatible storage or cardinality.");
        std::vector<double> values(source.Mesh.VerticesSize());
        bool finite = true;
        std::visit([&](const auto& input) {
            for (std::size_t i = 0; i < values.size(); ++i)
                if (!source.DeletedVertices[i])
                {
                    values[i] = static_cast<double>(input[i]);
                    finite &= std::isfinite(values[i]) &&
                        static_cast<long double>(values[i]) == static_cast<long double>(input[i]);
                }
        }, scalar.Values);
        if (!finite) return fail("Scalar values must be finite and exactly representable as doubles on live vertices.");
        const auto gradients = Geometry::MeshUtils::ComputeFaceScalarGradients(source.Mesh, values);
        struct State { bool Exists{}; std::vector<glm::vec3> Values{}; };
        const auto old = view.FaceSource->Properties.Get<glm::vec3>(c.Output.Name);
        const State before{bool(old), old ? old.Vector() : std::vector<glm::vec3>{}};
        State after{true, old ? old.Vector() : std::vector<glm::vec3>(view.FaceSource->Properties.Size(), glm::vec3{0})};
        for (std::size_t i = 0; i < gradients.size(); ++i)
        {
            for (int axis = 0; axis < 3; ++axis)
                if (!std::isfinite(gradients[i][axis]) || std::abs(gradients[i][axis]) > std::numeric_limits<float>::max())
                    return fail("Gradient cannot be represented by finite float vectors.");
            after.Values[source.SourceFaceForMeshFace[i]] = glm::vec3{gradients[i]};
        }
        result.FaceCount = gradients.size();
        if (before.Exists && GeometryValueComparison::BitEqual(before.Values, after.Values))
        {
            result.Message = "Face scalar gradient is unchanged.";
            return result;
        }
        const auto signature = MS::MeshTopologyValueSignature(view);
        if (!signature) return fail("Mesh topology is invalid.");
        const auto mutate = [context, entity = *entity, c, signature, scalar,
                             positions = std::move(source.BeforePositions), deleted = std::move(source.DeletedVertices)]
            (const State& expected, const State& target) {
            auto& raw = context.Scene->Raw();
            if (!raw.valid(entity)) return EditorCommandHistoryStatus::StaleEntity;
            const auto current = GS::BuildConstView(raw, entity);
            if (MS::MeshTopologyValueSignature(current) != signature || !current.VertexSource || !current.FaceSource)
                return EditorCommandHistoryStatus::StaleEntity;
            const auto p = current.VertexSource->Properties.Get<glm::vec3>(c.Positions.Name);
            const auto d = current.VertexSource->Properties.Get<bool>("v:deleted");
            if (!p || !GeometryValueComparison::BitEqual(p.Vector(), positions) ||
                (current.VertexSource->Properties.Exists("v:deleted") && !d) ||
                (d ? d.Vector() != deleted : std::ranges::any_of(deleted, [](bool x) { return x; })) ||
                !SameGeometryScalarPropertySnapshot(CaptureGeometryScalarProperty(current.VertexSource->Properties, c.Scalar), scalar))
                return EditorCommandHistoryStatus::StaleEntity;
            auto& props = GS::BuildMutableView(raw, entity).FaceSource->Properties;
            const auto output = std::as_const(props).Get<glm::vec3>(c.Output.Name);
            if (props.Exists(c.Output.Name) != expected.Exists ||
                (expected.Exists && (!output || !GeometryValueComparison::BitEqual(output.Vector(), expected.Values))))
                return EditorCommandHistoryStatus::StaleEntity;
            if (target.Exists) props.GetOrAdd<glm::vec3>(c.Output.Name).Vector() = target.Values;
            else if (auto property = props.Get<glm::vec3>(c.Output.Name)) props.Remove(property);
            ECS::Components::DirtyTags::MarkGpuDirty(raw, entity);
            if (context.InvalidateWorkspaceSnapshotCache) context.InvalidateWorkspaceSnapshotCache();
            return EditorCommandHistoryStatus::Applied;
        };
        const auto status = context.CommandHistory
            ? context.CommandHistory->Execute({.Label = "Compute scalar gradient",
                  .Redo = [mutate, before, after] { return mutate(before, after); },
                  .Undo = [mutate, before, after] { return mutate(after, before); }}).Status
            : mutate(before, after);
        result.Status = EditorFeatureDetail::ToEditorCommandStatus(status);
        result.Message = result.Succeeded() ? "Face scalar gradient computed (cpu_reference)."
                                            : "Gradient publication rejected by history guards.";
        return result;
    }
}
