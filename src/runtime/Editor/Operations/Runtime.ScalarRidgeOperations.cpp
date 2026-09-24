module;
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

module Extrinsic.Runtime.ScalarRidgeOperations;

import Extrinsic.Core.Error;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.GeometryAvailability;
import Geometry.Graph;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.CurvatureExtrema;
import Geometry.Properties;

#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/internal/Runtime.EditorGeneratedEntity.hpp"
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.MeshSources.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace GS = ECS::Components::GeometrySources;
        namespace C = Geometry::CurvatureExtrema;
        constexpr std::string_view kFieldProperty = "v:scalar_ridge_field";
    }

    EditorScalarRidgeResult ApplyEditorScalarRidgeCommand(
        const EditorProcessingCommands& commands, const EditorScalarRidgeCommand& command)
    {
        const EditorProcessingContext& context =
            EditorProcessingCommandsAccess::Resolve(commands);
        EditorScalarRidgeResult result;
        auto fail = [&](EditorCommandStatus status, std::string message) {
            result.Status = status;
            result.Message = std::move(message);
            return result;
        };
        if (!context.Scene)
            return fail(EditorCommandStatus::MissingScene, "Scene is unavailable.");
        if (command.Property.Domain != GeometryElementDomain::MeshVertex ||
            !command.Property.HasName())
            return fail(EditorCommandStatus::InvalidProcessingParameters,
                        "Choose a scalar mesh vertex property.");
        if (command.Scale > 2u || (!command.Ridges && !command.Valleys))
            return fail(EditorCommandStatus::InvalidProcessingParameters,
                        "Choose a scale and at least one of ridges or valleys.");
        auto& raw = context.Scene->Raw();
        const auto entity = EditorFeatureDetail::ResolveStableEntity(raw, command.StableEntityId);
        if (!entity)
            return fail(EditorCommandStatus::StaleEntity, "Scalar ridge target is stale.");
        const auto view = GS::BuildConstView(raw, *entity);
        auto source =
            GeometryProcessingDetail::MeshSupport::BuildHalfedgeMeshForProcessing(view, "Scalar ridges");
        if (!source.Succeeded())
            return fail(source.Status, std::move(source.Diagnostic));
        const auto matrix = GeometryProcessingDetail::ComposeEditorWorldMatrix(raw, *entity);
        if (!matrix)
            return fail(EditorCommandStatus::MissingTransform,
                        "Scalar ridge target has no finite world transform.");

        // The processing mesh keeps source vertex slots, so the captured
        // property copies across by index; deleted slots stay NaN.
        const auto captured = CaptureGeometryScalarProperty(view.VertexSource->Properties,
                                                            command.Property);
        const std::size_t slots = source.DeletedVertices.size();
        if (!captured.Exists || GeometryScalarPropertySize(captured) != slots)
            return fail(EditorCommandStatus::InvalidProcessingParameters,
                        "Property '" + command.Property.Name +
                            "' is not a scalar vertex property of this mesh.");
        auto field = source.Mesh.VertexProperties().GetOrAdd<double>(
            std::string(kFieldProperty), std::numeric_limits<double>::quiet_NaN());
        std::visit([&](const auto& values) {
            for (std::size_t v = 0; v < slots && v < field.Vector().size(); ++v)
                if (!source.DeletedVertices[v])
                    field[v] = static_cast<double>(values[v]);
        }, captured.Values);

        C::Params params = C::kScalarDefaults;
        params.RadiusRatio = command.RadiusRatio;
        params.MinimumSharpness = command.MinimumSharpness;
        params.MinimumStrength = command.MinimumStrength;
        const auto start = std::chrono::steady_clock::now();
        const auto extracted = C::ExtractScalarExtrema(source.Mesh, kFieldProperty, params);
        result.ComputeMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        if (!extracted.Succeeded())
            return fail(extracted.Diagnostic.State == C::Status::InvalidParameters
                            ? EditorCommandStatus::InvalidProcessingParameters
                            : EditorCommandStatus::GeometryProcessingFailed,
                        std::string("Scalar ridge extraction failed: ") +
                            C::ToString(extracted.Diagnostic.State));

        Geometry::Graph::Graph graph;
        std::vector<std::optional<Geometry::VertexHandle>> vertexForPoint(extracted.Points.size());
        auto vertex = [&](std::uint32_t point) {
            auto& handle = vertexForPoint[point];
            if (!handle)
                handle = graph.AddVertex(
                    glm::vec3(*matrix * glm::vec4(extracted.Points[point].Position, 1.0f)));
            return *handle;
        };
        auto kind = graph.GetOrAddEdgeProperty<float>("e:scalar_extremum", 0.0f);
        auto strength = graph.GetOrAddEdgeProperty<float>("e:strength", 0.0f);
        for (const auto& segment : extracted.Segments)
        {
            const bool ridge = segment.Signal == C::Kind::ScalarRidge;
            if (segment.Scale != command.Scale || (ridge ? !command.Ridges : !command.Valleys))
                continue;
            if (command.RequirePersistence &&
                (segment.PersistentScaleMask & ~(1u << segment.Scale)) == 0u)
                continue;
            const auto edge = graph.AddEdge(vertex(segment.PointA), vertex(segment.PointB));
            if (!edge)
                continue;
            kind[*edge] = ridge ? 1.0f : -1.0f;
            strength[*edge] = static_cast<float>(segment.Strength);
            ++(ridge ? result.RidgeSegmentCount : result.ValleySegmentCount);
        }
        result.OutputVertexCount = graph.VertexCount();
        if (graph.EdgeCount() == 0u)
        {
            result.Status = EditorCommandStatus::NoChange;
            result.Message = "No ridges or valleys pass the thresholds at this scale.";
            return result;
        }

        const auto* name = raw.try_get<ECS::Components::MetaData>(*entity);
        const auto published = GeometryProcessingDetail::PublishEditorGeneratedEntity(
            context, {.Source = *entity,
                      .Name = (name ? name->EntityName : std::string("Mesh")) + " ridges (" +
                              command.Property.Name + ")",
                      .Graph = std::move(graph),
                      .IdentityHigh = 0x5343414c52494447ull,
                      .Label = "Extract scalar ridges"});
        result.Status = published.Status;
        result.OutputEntityId = published.OutputEntityId;
        result.Message = published.Succeeded()
            ? std::to_string(result.RidgeSegmentCount) + " ridge and " +
                  std::to_string(result.ValleySegmentCount) + " valley segments extracted."
            : published.Message;
        return result;
    }
}
