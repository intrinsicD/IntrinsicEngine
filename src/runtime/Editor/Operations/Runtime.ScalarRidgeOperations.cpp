module;
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
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
import Geometry.HalfedgeMesh.ScalarfieldExtrema;
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
        namespace C = Geometry::ScalarfieldExtrema;
        using GeometryProcessingDetail::MeshSupport::MeshTopologyValueSignature;

        struct FeatureState
        {
            GeometryScalarPropertySnapshot Vertices, Edges, Basins;
            bool operator==(const FeatureState& other) const
            {
                return SameGeometryScalarPropertySnapshot(Vertices, other.Vertices) &&
                       SameGeometryScalarPropertySnapshot(Edges, other.Edges) &&
                       SameGeometryScalarPropertySnapshot(Basins, other.Basins);
            }
        };
        struct FeatureBindings
        {
            GeometryPropertyRef Vertices, Edges, Basins;
            bool WithBasins{};
        };
        // Captures the bound outputs; false when an existing one has the wrong
        // size or a type the snapshot cannot represent.
        bool CaptureFeatures(const Geometry::PropertySet& vertices, const Geometry::PropertySet& edges,
                             const FeatureBindings& bindings, FeatureState& state)
        {
            state.Vertices = CaptureGeometryScalarProperty(vertices, bindings.Vertices);
            state.Edges = CaptureGeometryScalarProperty(edges, bindings.Edges);
            state.Basins = bindings.WithBasins
                ? CaptureGeometryScalarProperty(vertices, bindings.Basins)
                : GeometryScalarPropertySnapshot{};
            const auto valid = [](const Geometry::PropertySet& props, const GeometryPropertyRef& ref,
                                  const GeometryScalarPropertySnapshot& snapshot) {
                return !props.Exists(ref.Name) ||
                       (snapshot.Exists && GeometryScalarPropertySize(snapshot) == props.Size());
            };
            return valid(vertices, bindings.Vertices, state.Vertices) &&
                   valid(edges, bindings.Edges, state.Edges) &&
                   (!bindings.WithBasins || valid(vertices, bindings.Basins, state.Basins));
        }
        bool ApplyFeatures(Geometry::PropertySet& vertices, Geometry::PropertySet& edges,
                           const FeatureBindings& bindings, const FeatureState& target)
        {
            if (!CanApplyGeometryScalarProperty(vertices, bindings.Vertices, target.Vertices) ||
                !CanApplyGeometryScalarProperty(edges, bindings.Edges, target.Edges) ||
                (bindings.WithBasins &&
                 !CanApplyGeometryScalarProperty(vertices, bindings.Basins, target.Basins)))
                return false;
            (void)ApplyGeometryScalarProperty(vertices, bindings.Vertices, target.Vertices);
            (void)ApplyGeometryScalarProperty(edges, bindings.Edges, target.Edges);
            if (bindings.WithBasins)
                (void)ApplyGeometryScalarProperty(vertices, bindings.Basins, target.Basins);
            return true;
        }
        bool ValidFeatureBindings(const EditorScalarRidgeCommand& command)
        {
            const auto scalarOutput = [](const GeometryPropertyRef& ref, GeometryElementDomain domain) {
                return ref.Domain == domain && ref.HasName() &&
                       GeometryPropertyComponentCount(ref.ValueKind) == 1u &&
                       !IsTopologyProperty(ref.Domain, ref.Name);
            };
            const bool basins = command.Method == EditorScalarExtremaMethod::Watershed;
            return scalarOutput(command.VertexFeatures, GeometryElementDomain::MeshVertex) &&
                   scalarOutput(command.EdgeFeatures, GeometryElementDomain::MeshEdge) &&
                   (!basins || scalarOutput(command.BasinLabels, GeometryElementDomain::MeshVertex)) &&
                   command.VertexFeatures.Name != command.Property.Name &&
                   (!basins || (command.BasinLabels.Name != command.Property.Name &&
                                command.BasinLabels.Name != command.VertexFeatures.Name));
        }
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
        const bool watershed = command.Method == EditorScalarExtremaMethod::Watershed;
        if ((!watershed && command.Method != EditorScalarExtremaMethod::HessianRidge) ||
            command.Scale > 2u || (!command.Ridges && !command.Valleys))
            return fail(EditorCommandStatus::InvalidProcessingParameters,
                        "Choose a method, a scale and at least one of ridges or valleys.");
        if (!command.PublishGraph && !command.PublishMeshFeatures)
            return fail(EditorCommandStatus::InvalidProcessingParameters,
                        "Publish the curve graph, mesh features, or both.");
        if (command.PublishMeshFeatures && !ValidFeatureBindings(command))
            return fail(EditorCommandStatus::InvalidProcessingParameters,
                        "Feature outputs need distinct scalar mesh vertex/edge properties "
                        "that are not topology or the input field.");
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
        if (command.PublishGraph && !matrix)
            return fail(EditorCommandStatus::MissingTransform,
                        "Scalar ridge target has no finite world transform.");
        if (command.PublishMeshFeatures && !view.EdgeSource)
            return fail(EditorCommandStatus::UnsupportedGeometryDomain,
                        "Mesh edge features require a mesh edge source.");

        // The processing mesh keeps source vertex slots, so the captured
        // property copies across by index; deleted slots stay NaN.
        const auto captured = CaptureGeometryScalarProperty(view.VertexSource->Properties,
                                                            command.Property);
        const std::size_t slots = source.DeletedVertices.size();
        if (!captured.Exists || GeometryScalarPropertySize(captured) != slots ||
            slots != source.Mesh.VerticesSize())
            return fail(EditorCommandStatus::InvalidProcessingParameters,
                        "Property '" + command.Property.Name +
                            "' is not a scalar vertex property of this mesh.");
        std::vector<double> field(slots, std::numeric_limits<double>::quiet_NaN());
        std::visit([&](const auto& values) {
            for (std::size_t v = 0; v < slots; ++v)
                if (!source.DeletedVertices[v])
                    field[v] = static_cast<double>(values[v]);
        }, captured.Values);

        C::Params params = C::kScalarDefaults;
        params.Algorithm = watershed ? C::Method::Watershed : C::Method::HessianRidge;
        params.MinimumPersistence = command.MinimumPersistence;
        params.RadiusRatio = command.RadiusRatio;
        params.MinimumSharpness = command.MinimumSharpness;
        params.MinimumStrength = command.MinimumStrength;
        const auto start = std::chrono::steady_clock::now();
        const auto extracted = C::Extract(source.Mesh, std::span<const double>{field}, params);
        result.ComputeMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        if (!extracted.Succeeded())
            return fail(extracted.Diagnostic.State == C::Status::InvalidParameters
                            ? EditorCommandStatus::InvalidProcessingParameters
                            : EditorCommandStatus::GeometryProcessingFailed,
                        std::string("Scalar ridge extraction failed: ") +
                            C::ToString(extracted.Diagnostic.State));

        std::vector<std::uint32_t> selected;
        for (std::uint32_t i = 0; i < extracted.Segments.size(); ++i)
        {
            const auto& segment = extracted.Segments[i];
            const bool ridge = segment.Signal == C::Kind::ScalarRidge;
            if (ridge ? !command.Ridges : !command.Valleys)
                continue;
            if (!watershed &&
                (segment.Scale != command.Scale ||
                 (command.RequirePersistence &&
                  (segment.PersistentScaleMask & ~(1u << segment.Scale)) == 0u)))
                continue;
            selected.push_back(i);
            ++(ridge ? result.RidgeSegmentCount : result.ValleySegmentCount);
        }
        if (selected.empty())
        {
            result.Status = EditorCommandStatus::NoChange;
            result.Message = watershed ? "No watershed curves pass the thresholds."
                                       : "No ridges or valleys pass the thresholds at this scale.";
            return result;
        }

        std::string featureMessage;
        if (command.PublishMeshFeatures)
        {
            const auto features = C::SnapToMesh(source.Mesh, extracted, selected);
            // Map processing-mesh edges to authoritative edge slots by endpoints.
            const auto& edgeProps = view.EdgeSource->Properties;
            const auto v0 = edgeProps.Get<std::uint32_t>(GS::PropertyNames::kEdgeV0);
            const auto v1 = edgeProps.Get<std::uint32_t>(GS::PropertyNames::kEdgeV1);
            const auto edgeDeleted = edgeProps.Get<bool>("e:deleted");
            if (!v0 || !v1 || v0.Vector().size() != edgeProps.Size() ||
                v1.Vector().size() != edgeProps.Size())
                return fail(EditorCommandStatus::InvalidProcessingParameters,
                            "Mesh edge features require canonical edge endpoints.");
            const auto key = [](std::uint32_t a, std::uint32_t b) {
                return (std::uint64_t{std::min(a, b)} << 32u) | std::max(a, b);
            };
            std::unordered_map<std::uint64_t, std::uint32_t> edgeSlot;
            std::vector<std::uint32_t> edgeSlots;
            for (std::uint32_t e = 0; e < edgeProps.Size(); ++e)
            {
                if (edgeDeleted && edgeDeleted.Vector()[e])
                    continue;
                edgeSlots.push_back(e);
                edgeSlot.emplace(key(v0.Vector()[e], v1.Vector()[e]), e);
            }
            std::vector<std::uint32_t> vertexMask(slots, 0u), edgeMask(edgeProps.Size(), 0u),
                basins(slots, 0u), vertexSlots;
            for (std::uint32_t v = 0; v < slots; ++v)
                if (!source.DeletedVertices[v])
                {
                    vertexSlots.push_back(v);
                    vertexMask[v] = features.Vertices[v];
                }
            for (auto edge : source.Mesh.LiveEdges())
            {
                if (!features.Edges[edge.Index])
                    continue;
                const auto h = source.Mesh.Halfedge(edge, 0);
                const auto found = edgeSlot.find(key(source.Mesh.FromVertex(h).Index,
                                                     source.Mesh.ToVertex(h).Index));
                if (found == edgeSlot.end())
                    return fail(EditorCommandStatus::InvalidProcessingParameters,
                                "A feature edge has no authoritative mesh edge.");
                edgeMask[found->second] = 1u;
            }
            result.FeatureVertexCount = features.VertexCount;
            result.FeatureEdgeCount = features.EdgeCount;
            FeatureBindings bindings{command.VertexFeatures, command.EdgeFeatures,
                                     command.BasinLabels, watershed};
            if (watershed)
            {
                // Unsupported vertices (no finite value) join no basin; they
                // publish as one past the last basin label.
                const auto none = static_cast<std::uint32_t>(extracted.Diagnostic.DescendingBasins);
                for (auto v : vertexSlots)
                    basins[v] = extracted.DescendingBasin[v] == C::kInvalidBasin
                        ? none : extracted.DescendingBasin[v];
                result.BasinCount = extracted.Diagnostic.DescendingBasins;
            }
            auto mutableView = GS::BuildMutableView(raw, *entity);
            auto& vertexProps = mutableView.VertexSource->Properties;
            auto& mutableEdges = mutableView.EdgeSource->Properties;
            FeatureState before;
            if (!CaptureFeatures(vertexProps, mutableEdges, bindings, before))
                return fail(EditorCommandStatus::InvalidProcessingParameters,
                            "Feature output properties have incompatible types or sizes.");
            FeatureState after = before;
            if (!PrepareGeometryScalarProperty(after.Vertices, command.VertexFeatures.ValueKind,
                    vertexProps.Size(), vertexSlots, std::span<const std::uint32_t>{vertexMask}) ||
                !PrepareGeometryScalarProperty(after.Edges, command.EdgeFeatures.ValueKind,
                    mutableEdges.Size(), edgeSlots, std::span<const std::uint32_t>{edgeMask}) ||
                (watershed &&
                 !PrepareGeometryScalarProperty(after.Basins, command.BasinLabels.ValueKind,
                    vertexProps.Size(), vertexSlots, std::span<const std::uint32_t>{basins})))
                return fail(EditorCommandStatus::InvalidProcessingParameters,
                            "Feature outputs cannot be represented exactly in the selected storage.");
            if (before == after)
                featureMessage = " Mesh features are unchanged.";
            else
            {
                const auto signature = MeshTopologyValueSignature(GS::BuildConstView(raw, *entity));
                if (!signature)
                    return fail(EditorCommandStatus::InvalidProcessingParameters,
                                "Scalar ridge target topology is invalid.");
                const auto mutate = [scene = context.Scene, entity = *entity, signature, bindings,
                                     invalidate = context.InvalidateWorkspaceSnapshotCache](
                                        const FeatureState& expected, const FeatureState& target) {
                    auto& raw = scene->Raw();
                    if (!raw.valid(entity) ||
                        MeshTopologyValueSignature(GS::BuildConstView(raw, entity)) != signature)
                        return EditorCommandHistoryStatus::StaleEntity;
                    auto view = GS::BuildMutableView(raw, entity);
                    if (!view.VertexSource || !view.EdgeSource)
                        return EditorCommandHistoryStatus::StaleEntity;
                    auto& vertices = view.VertexSource->Properties;
                    auto& edges = view.EdgeSource->Properties;
                    FeatureState current;
                    if (!CaptureFeatures(vertices, edges, bindings, current) || current != expected)
                        return EditorCommandHistoryStatus::StaleEntity;
                    if (!ApplyFeatures(vertices, edges, bindings, target))
                        return EditorCommandHistoryStatus::CommandFailed;
                    if (invalidate)
                        invalidate();
                    return EditorCommandHistoryStatus::Applied;
                };
                const auto status = context.CommandHistory
                    ? context.CommandHistory->Execute(
                          {.Label = "Mark scalar extremum features",
                           .Redo = [mutate, before, after] { return mutate(before, after); },
                           .Undo = [mutate, before, after] { return mutate(after, before); }})
                          .Status
                    : mutate(before, after);
                result.Status = EditorFeatureDetail::ToEditorCommandStatus(status);
                if (!result.Succeeded())
                    return fail(result.Status, "Mesh feature publication was rejected.");
                featureMessage = " " + std::to_string(result.FeatureVertexCount) +
                                 " feature vertices and " + std::to_string(result.FeatureEdgeCount) +
                                 " feature edges marked.";
            }
            if (!command.PublishGraph)
            {
                result.Status = before == after ? EditorCommandStatus::NoChange
                                                : EditorCommandStatus::Applied;
                result.Message = std::to_string(result.RidgeSegmentCount) + " ridge and " +
                                 std::to_string(result.ValleySegmentCount) + " valley segments." +
                                 featureMessage;
                return result;
            }
        }

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
        for (auto index : selected)
        {
            const auto& segment = extracted.Segments[index];
            const auto edge = graph.AddEdge(vertex(segment.PointA), vertex(segment.PointB));
            if (!edge)
                continue;
            kind[*edge] = segment.Signal == C::Kind::ScalarRidge ? 1.0f : -1.0f;
            strength[*edge] = static_cast<float>(segment.Strength);
        }
        result.OutputVertexCount = graph.VertexCount();

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
                  std::to_string(result.ValleySegmentCount) + " valley segments extracted." +
                  featureMessage
            : published.Message + featureMessage;
        return result;
    }
}
