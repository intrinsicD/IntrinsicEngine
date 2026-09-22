// Virtual-source geodesic distance published on the mesh's own vertices.
module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <bit>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

module Extrinsic.Runtime.MeshFieldOperations;

import Extrinsic.Core.Error;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.GeometryAvailability;
import Geometry.Geodesic;
import Geometry.HalfedgeMesh;
import Geometry.Properties;

#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"

#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"

#include "Editor/Operations/Runtime.GeometryProcessingOperations.MeshSources.hpp"
#include "Editor/Operations/Runtime.MeshFieldOperations.Properties.hpp"

namespace Extrinsic::Runtime
{
    using namespace GeometryProcessingDetail::MeshSupport;
    using namespace MeshFieldDetail;
    using EditorFeatureDetail::ResolveStableEntity;
    using EditorFeatureDetail::ToEditorCommandStatus;
    namespace GS = ECS::Components::GeometrySources;

    EditorGeodesicsResult ApplyEditorGeodesicsCommand(
        const EditorProcessingCommands& commands, const EditorGeodesicsCommand& command)
    {
        const EditorProcessingContext& context =
            EditorProcessingCommandsAccess::Resolve(commands);
        EditorGeodesicsResult result;
        auto fail = [&](EditorCommandStatus status, std::string message) {
            result.Status = status;
            result.Message = std::move(message);
            return result;
        };
        if (!context.Scene)
            return fail(EditorCommandStatus::MissingScene, "Scene is unavailable.");
        const auto validated = ValidateGeodesicsConfigSection(
            SerializeGeodesicsConfig(command.Config), {}, kGeodesicsConfigSectionName);
        if (!validated.Usable())
            return fail(EditorCommandStatus::InvalidProcessingParameters,
                        validated.Diagnostics.front().Message);
        auto& raw = context.Scene->Raw();
        const auto entity = ResolveStableEntity(raw, command.StableEntityId);
        if (!entity)
            return fail(EditorCommandStatus::StaleEntity, "Geodesics target is stale.");
        auto source = BuildHalfedgeMeshForProcessing(GS::BuildConstView(raw, *entity), "Geodesics",
                                                  command.Config.PositionProperty.Name);
        if (!source.Succeeded())
            return fail(source.Status, std::move(source.Diagnostic));
        for (auto face : source.Mesh.LiveFaces())
            for (auto vertex : source.Mesh.VerticesAroundFace(face))
                if (source.DeletedVertices[vertex.Index])
                    return fail(EditorCommandStatus::InvalidProcessingParameters,
                                "Geodesics triangle references a deleted vertex.");
        // Reject polygon triangulation: the method contract is a triangle surface.
        auto faceIds = source.SourceFaceForMeshFace;
        std::sort(faceIds.begin(), faceIds.end());
        if (std::adjacent_find(faceIds.begin(), faceIds.end()) != faceIds.end())
            return fail(EditorCommandStatus::InvalidProcessingParameters,
                        "Geodesics require triangle source faces.");
        std::vector<std::size_t> sources(command.Config.SourceVertices.begin(),
                                         command.Config.SourceVertices.end());
        for (auto v : sources)
            if (v >= source.DeletedVertices.size() || source.DeletedVertices[v])
                return fail(EditorCommandStatus::InvalidProcessingParameters,
                            "Geodesics source vertex is deleted or out of range.");
        auto view = GS::BuildMutableView(raw, *entity);
        auto& properties = view.VertexSource->Properties;
        const std::string distanceName = command.Config.DistanceProperty.Name;
        const std::string sourceName = command.Config.SourceMaskProperty.Name;
        struct State
        {
            bool HadDistance{false}, HadSource{false};
            std::vector<double> Distance;
            std::vector<bool> Source;
            bool operator==(const State&) const = default;
        };
        auto capture = [distanceName, sourceName](Geometry::PropertySet& props, State& state) {
            std::string diagnostic;
            return CaptureCurvatureProperty<double>(props, distanceName, props.Size(),
                                                    state.HadDistance, state.Distance,
                                                    diagnostic) &&
                   CaptureCurvatureProperty<bool>(props, sourceName, props.Size(), state.HadSource,
                                                  state.Source, diagnostic);
        };
        State before;
        if (!capture(properties, before))
            return fail(EditorCommandStatus::InvalidProcessingParameters,
                        "Geodesics output properties have incompatible types or sizes.");
        result.Diagnostics = Geometry::Geodesic::ComputeVirtualSourceDistance(
            source.Mesh, sources,
            Geometry::Geodesic::VirtualSourceParams{command.Config.MaxHalfedgeExpansions});
        if (!result.Diagnostics.Succeeded())
            return fail(EditorCommandStatus::GeometryProcessingFailed,
                        Geometry::Geodesic::ToString(result.Diagnostics.Status));
        for (std::size_t v = 0; v < source.DeletedVertices.size(); ++v)
            if (source.DeletedVertices[v])
            {
                result.Diagnostics.Distances[v] = std::numeric_limits<double>::infinity();
                --result.Diagnostics.UnreachableVertexCount;
            }
        State after{true, true, result.Diagnostics.Distances,
                    std::vector<bool>(source.Mesh.VerticesSize(), false)};
        for (auto v : sources)
            after.Source[v] = true;
        if (before == after)
        {
            result.Status = EditorCommandStatus::NoChange;
            result.Message = "Geodesics are unchanged.";
            return result;
        }
        const auto signature = MeshTopologyValueSignature(GS::BuildConstView(raw, *entity));
        if (!signature)
            return fail(EditorCommandStatus::InvalidProcessingParameters,
                        "Geodesics topology is invalid.");
        const auto positions =
            std::make_shared<const std::vector<glm::vec3>>(std::move(source.BeforePositions));
        const auto deletedVertices =
            std::make_shared<const std::vector<bool>>(std::move(source.DeletedVertices));
        const auto mutate = [scene = context.Scene, entity = *entity, signature, positions,
                             deletedVertices,
                             distanceName, sourceName,
                             positionProperty = command.Config.PositionProperty.Name, capture,
                             invalidate = context.InvalidateWorkspaceSnapshotCache](
                                const State& expected, const State& target) {
            auto& raw = scene->Raw();
            if (!raw.valid(entity) ||
                MeshTopologyValueSignature(GS::BuildConstView(raw, entity)) != signature)
                return EditorCommandHistoryStatus::StaleEntity;
            auto view = GS::BuildMutableView(raw, entity);
            if (!view.VertexSource)
                return EditorCommandHistoryStatus::StaleEntity;
            auto& props = view.VertexSource->Properties;
            const auto currentPositions = props.Get<glm::vec3>(positionProperty);
            const auto currentDeleted = props.Get<bool>("v:deleted");
            if ((props.Exists("v:deleted") && !currentDeleted) ||
                (currentDeleted ? currentDeleted.Vector() != *deletedVertices
                                : std::ranges::any_of(*deletedVertices, [](bool v) { return v; })))
                return EditorCommandHistoryStatus::StaleEntity;
            State current;
            if (props.Size() != positions->size() || !currentPositions ||
                currentPositions.Vector() != *positions ||
                !capture(props, current) || current != expected)
                return EditorCommandHistoryStatus::StaleEntity;
            if (!ApplyCurvatureProperty<double>(props, distanceName, target.HadDistance,
                                                target.Distance, 0.0) ||
                !ApplyCurvatureProperty<bool>(props, sourceName, target.HadSource, target.Source,
                                              false))
                return EditorCommandHistoryStatus::CommandFailed;
            if (invalidate)
                invalidate();
            return EditorCommandHistoryStatus::Applied;
        };
        if (context.CommandHistory)
        {
            const auto history =
                context.CommandHistory->Execute({.Label = "Compute geodesics",
                                                 .Redo =
                                                     [mutate, before, after] {
                                                         return mutate(before, after);
                                                     },
                                                 .Undo =
                                                     [mutate, before, after] {
                                                         return mutate(after, before);
                                                     }});
            result.Status = ToEditorCommandStatus(history.Status);
        }
        else
            result.Status = ToEditorCommandStatus(mutate(before, after));
        result.Message = result.Succeeded() ? "Virtual-source geodesics computed (cpu_reference)."
                                            : "Geodesics publication rejected.";
        return result;
    }
}
