// Triangle gradients publish on original face slots through guarded editor history.
module;
#include "GeometryIntegration/Runtime.GeometryValueComparison.hpp"
#include <algorithm>
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
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
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
            if (auto error = ConfigDetail::ValidatePointConfigFields(
                    doc, merged, "Scalar gradient config must be an object.", "Unknown gradient field: ", {}))
                return ConfigDetail::RejectConfigSection(subject, *error);
            for (const auto key : {"positions", "scalar", "output"})
            {
                const bool scalar = std::string_view{key} == "scalar";
                const auto domain = std::string_view{key} == "output"
                    ? GeometryElementDomain::MeshFace : GeometryElementDomain::MeshVertex;
                if (ConfigDetail::ValidatePointPropertyRef(merged[key], scalar
                        ? Geometry::PropertyValueKind::Double : Geometry::PropertyValueKind::Vec3, scalar)
                        != ConfigDetail::PointPropertyValidation::Valid ||
                    merged[key]["domain"] != ToString(domain))
                    return ConfigDetail::RejectConfigSection(subject,
                        "Gradient requires vertex positions (vec3), a vertex scalar and a face vec3 output.");
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
        std::optional<ECS::EntityHandle> GradientTarget(
            const EditorProcessingContext& context, std::uint32_t id,
            const ScalarGradientConfig& c, std::string& diagnostic)
        {
            const auto validation = ValidateGradient(SerializeScalarGradientConfig(c), {}, kScalarGradientConfigSectionName);
            if (!validation.Usable())
            {
                diagnostic = validation.Diagnostics.front().Message;
                return {};
            }
            if (!context.Scene) { diagnostic = "Scene is unavailable."; return {}; }
            const auto entity = EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(), id);
            if (!entity) { diagnostic = "Choose an existing mesh entity."; return {}; }
            const auto view = GS::BuildConstView(context.Scene->Raw(), *entity);
            if (MS::ValidateMeshSoupSourceMetadata(view, diagnostic, c.Positions.Name) != EditorCommandStatus::Applied)
                return {};
            if (DetectGeometryPropertyValueKind(view.VertexSource->Properties, c.Scalar.Name) != c.Scalar.ValueKind)
            {
                diagnostic = "Choose an existing scalar vertex property with matching storage type.";
                return {};
            }
            const auto& faces = view.FaceSource->Properties;
            const auto output = faces.Get<glm::vec3>(c.Output.Name);
            if (faces.Exists(c.Output.Name) && (!output || output.Size() != faces.Size()))
            {
                diagnostic = "The gradient output already exists with incompatible storage.";
                return {};
            }
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
        return {.DefaultSection = GradientSection({}), .Validate = ValidateGradient};
    }
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
        std::string diagnostic;
        const auto entity = GradientTarget(context, id, c, diagnostic);
        const bool ready = entity && MS::PrepareMeshSoupFaceRings(context, *entity,
            BuildGeometryAvailability(context.Scene->Raw(), *entity), diagnostic, c.Positions, true);
        return {ready, std::move(diagnostic)};
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
        const auto entity = GradientTarget(context, id, c, result.Message);
        if (!entity) return fail(result.Message);
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
