// UI-081 Compute arrow order: one element domain's anchors ordered by a Geometry.PointSampling
// method and published as the undoable glyph rank that vector-field layers draw prefixes of.
module;
#include "GeometryIntegration/Runtime.GeometryValueComparison.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
module Extrinsic.Runtime.PointSamplingOperations;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.WorldHandle;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Smoothing;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.VertexChannelBindings;
import Geometry.PointSampling;
import Geometry.Properties;
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.MeshSources.hpp"
#include "Editor/Operations/Runtime.MeshFieldOperations.PropertyGraph.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace GP = GeometryProcessingDetail;
        namespace PG = PropertyGraphDetail;
        using D = GeometryElementDomain;

        [[nodiscard]] D PositionDomainOf(const D domain) noexcept
        {
            switch (domain)
            {
            case D::MeshEdge:
            case D::MeshFace:  return D::MeshVertex;
            case D::GraphEdge: return D::GraphNode;
            default:           return domain;
            }
        }
    }

    EditorGlyphOrderResult ApplyEditorGlyphOrderCommand(
        const EditorProcessingCommands& commands, const EditorGlyphOrderCommand& command)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        EditorGlyphOrderResult result;
        const auto fail = [&](const EditorCommandStatus status, std::string message)
        {
            result.Status = status;
            result.Message = std::move(message);
            return result;
        };
        if (context.Scene == nullptr)
            return fail(EditorCommandStatus::MissingScene, "Scene is unavailable.");
        const auto entity = EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(), command.StableEntityId);
        if (!entity)
            return fail(EditorCommandStatus::StaleEntity, "Choose an existing entity.");
        const GeometryEntityAvailability availability = BuildGeometryAvailability(context.Scene->Raw(), *entity);
        if (!SupportsGeometryVectorFieldDomain(command.Domain) ||
            !SupportsGeometryElementDomain(availability, command.Domain))
            return fail(EditorCommandStatus::UnsupportedGeometryDomain, "This entity has no such element domain.");

        // Object-space anchors of the live rows on the displayed positions, as the arrows are drawn:
        // the domain's own, or edge midpoints and face centers from the vertex positions (topology,
        // deletion and position revisions guard publication).
        const D positionDomain = PositionDomainOf(command.Domain);
        const auto* positionSet = ResolveGeometryPropertySet(availability, positionDomain);
        const auto* bindings = context.Scene->Raw().try_get<VertexChannelBindingSet>(*entity);
        const GeometryPropertyRef positions{positionDomain,
            positionSet != nullptr ? std::string{ResolveDisplayedPositions(*positionSet, positionDomain, bindings).Name}
                                   : std::string{ECS::Components::GeometrySources::PropertyNames::kPosition},
            Geometry::PropertyValueKind::Vec3};
        GP::PointInputCapture samples;
        std::string diagnostic;
        if (!PG::CaptureSamples(availability, command.Domain, positions, samples, diagnostic))
            return fail(EditorCommandStatus::InvalidProcessingParameters, diagnostic);
        result.LiveCount = std::uint32_t(samples.Slots.size());
        if (samples.Slots.empty())
            return fail(EditorCommandStatus::InvalidProcessingParameters, "The domain has no live elements.");
        if (samples.SlotCount >= kGeometryGlyphOrderRowLimit)
            return fail(EditorCommandStatus::InvalidProcessingParameters,
                "The domain has too many elements for an arrow order (2^24 or more).");
        const auto* properties = ResolveGeometryPropertySet(availability, command.Domain);
        const GeometryPropertyRef output{command.Domain,
            std::string{GeometryGlyphOrderPropertyName(command.Domain)},
            Geometry::PropertyValueKind::Float};
        if (properties->Exists(output.Name) && !properties->Get<float>(output.Name))
            return fail(EditorCommandStatus::InvalidProcessingParameters,
                "'" + output.Name + "' exists with another type; rename or remove it first.");

        const std::size_t count = std::min<std::size_t>(samples.Points.size(), kEditorGlyphOrderCap);
        const auto start = std::chrono::steady_clock::now();
        const Geometry::PointSampling::Result order =
            Geometry::PointSampling::Order(std::span<const glm::vec3>(samples.Points),
                ToPointSamplingParams(PointSamplingConfig{.Method = command.Method}),
                count);
        result.Milliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (!order.Succeeded())
            return fail(EditorCommandStatus::GeometryProcessingFailed, "The ordering method failed.");
        result.OrderedCount = std::uint32_t(order.Order.size());

        // Ranks 0..K-1 from the method, then the remaining live rows in row order; -1 when deleted.
        const auto before = std::make_shared<const PG::Snapshot>(PG::Capture(*properties, output));
        std::vector<float> ranks(samples.SlotCount, -1.0f);
        std::vector<bool> ranked(samples.Slots.size(), false);
        float next = 0.0f;
        for (const std::uint32_t sample : order.Order)
        {
            ranks[samples.Slots[sample]] = next++;
            ranked[sample] = true;
        }
        for (std::size_t sample = 0; sample < samples.Slots.size(); ++sample)
            if (!ranked[sample])
                ranks[samples.Slots[sample]] = next++;
        const auto after =
            std::make_shared<const PG::Snapshot>(PG::Snapshot{.Exists = true, .Values = std::move(ranks)});
        if (PG::Same(*before, *after))
        {
            result.Status = EditorCommandStatus::NoChange;
            result.Message = "The arrow order is unchanged.";
            return result;
        }

        const auto mutate = [context, entity = *entity, output, before, after, watches = std::move(samples.Inputs)](
                                const bool redo)
        {
            if (!GP::GeometryPropertiesCurrent(context, entity, watches) ||
                !GP::EditorProcessingContextWorldCurrent(context))
                return EditorCommandHistoryStatus::StaleEntity;
            auto* props = GP::MutableGeometryProperties(context.Scene->Raw(), entity, output.Domain);
            if (props == nullptr || !PG::Same(PG::Capture(*props, output), redo ? *before : *after))
                return EditorCommandHistoryStatus::StaleEntity;
            const std::array outputs{output};
            const auto historyBefore = GP::ObserveGeometryProperties(context, entity, outputs);
            const PG::Snapshot& target = redo ? *after : *before;
            if (target.Exists)
                props->GetOrAdd<float>(output.Name).Vector() = std::get<std::vector<float>>(target.Values);
            else if (auto property = props->Get<float>(output.Name))
                props->Remove(property);
            GP::RebasePointPropertyHistory(context, entity, historyBefore);
            ECS::Components::DirtyTags::MarkGpuDirty(context.Scene->Raw(), entity);
            if (context.InvalidateWorkspaceSnapshotCache)
                context.InvalidateWorkspaceSnapshotCache();
            return EditorCommandHistoryStatus::Applied;
        };
        const auto status = context.CommandHistory ? context.CommandHistory
                                                         ->Execute({.Label = "Compute arrow order",
                                                             .Redo =
                                                                 [mutate]
                                                             {
                                                                 return mutate(true);
                                                             },
                                                             .Undo =
                                                                 [mutate]
                                                             {
                                                                 return mutate(false);
                                                             }})
                                                         .Status
                                                   : mutate(true);
        result.Status = EditorFeatureDetail::ToEditorCommandStatus(status);
        result.Message = result.Succeeded()
            ? "Arrow order computed for " + std::to_string(result.LiveCount) + " elements (" +
                std::to_string(result.OrderedCount) + " by " + std::string(DisplayName(command.Method)) + ", " +
                std::to_string(std::lround(result.Milliseconds)) + " ms)."
            : "The arrow order was rejected by history guards.";
        return result;
    }
}
