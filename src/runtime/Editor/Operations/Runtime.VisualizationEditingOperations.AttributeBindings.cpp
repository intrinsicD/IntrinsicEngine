// RUNTIME-315: the unified render-attribute binding model. Rows come from the
// runtime attribute x element-domain table; the current source of each row is
// read from the storage that owns it (structural streams, visualization
// overlay, point/line render hints).
module;
#include <entt/entity/fwd.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <entt/entity/registry.hpp>

module Extrinsic.Runtime.VisualizationEditingOperations;

import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.VertexChannelBindings;
import Geometry.Properties;

#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/internal/Runtime.EditorVisualizationHelpers.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace G = Graphics::Components;
        using D = GeometryElementDomain;

        struct ColorOverlayTarget
        {
            EditorVisualizationTarget Target{EditorVisualizationTarget::Surface};
            G::VisualizationConfig::Domain VisDomain{G::VisualizationConfig::Domain::Vertex};
        };

        // The lane a domain's Color binding lives on; the same mapping the
        // property-display recipe path (`show_property`) writes.
        [[nodiscard]] ColorOverlayTarget ColorOverlayTargetFor(const D domain) noexcept
        {
            using VD = G::VisualizationConfig::Domain;
            using T = EditorVisualizationTarget;
            switch (domain)
            {
            case D::MeshFace: return {T::Surface, VD::Face};
            case D::MeshEdge:
            case D::GraphEdge: return {T::Edges, VD::Edge};
            case D::GraphNode: return {T::Edges, VD::Vertex};
            case D::PointCloudPoint: return {T::Points, VD::Vertex};
            default: return {T::Surface, VD::Vertex};
            }
        }

        [[nodiscard]] std::optional<std::string> BoundColorSource(
            const entt::registry& raw, const ECS::EntityHandle entity, const D domain)
        {
            using Source = G::VisualizationConfig::ColorSource;
            using VD = G::VisualizationConfig::Domain;
            const ColorOverlayTarget target = ColorOverlayTargetFor(domain);
            const std::optional<G::VisualizationConfig> config =
                EditorFeatureDetail::EffectiveVisualizationConfigForTarget(raw, entity, target.Target);
            if (!config.has_value())
                return std::nullopt;
            if (config->Source == Source::ScalarField && config->ScalarDomain == target.VisDomain &&
                !config->ScalarFieldName.empty())
            {
                return config->ScalarFieldName;
            }
            const bool bufferMatches =
                (config->Source == Source::PerVertexBuffer && target.VisDomain == VD::Vertex) ||
                (config->Source == Source::PerEdgeBuffer && target.VisDomain == VD::Edge) ||
                (config->Source == Source::PerFaceBuffer && target.VisDomain == VD::Face);
            if (bufferMatches && !config->ColorBufferName.empty())
                return config->ColorBufferName;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::string> BoundSourceName(
            const entt::registry& raw, const ECS::EntityHandle entity,
            const RenderAttributeRule& rule)
        {
            switch (rule.Attribute)
            {
            case RenderAttribute::Position:
            case RenderAttribute::Normal:
            case RenderAttribute::Texcoord:
                if (const auto* bindings = raw.try_get<VertexChannelBindingSet>(entity))
                {
                    const VertexChannelSourceBinding* binding =
                        FindVertexChannelSourceBinding(*bindings, rule.Attribute);
                    if (binding != nullptr && IsVertexChannelBindingEnabled(*binding) &&
                        binding->Property.Domain == rule.Domain)
                    {
                        return binding->Property.Name;
                    }
                }
                return std::nullopt;
            case RenderAttribute::Color:
                return BoundColorSource(raw, entity, rule.Domain);
            case RenderAttribute::PointSize:
                if (const auto* points = raw.try_get<G::RenderPoints>(entity))
                {
                    if (const auto* name = std::get_if<std::string>(&points->SizeSource))
                        return *name;
                }
                return std::nullopt;
            case RenderAttribute::LineWidth:
                if (const auto* edges = raw.try_get<G::RenderEdges>(entity))
                {
                    if (const auto* name = std::get_if<std::string>(&edges->WidthSource))
                        return *name;
                }
                return std::nullopt;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::string ResolutionReason(
            const RenderAttributeRule& rule, const GeometryPropertyResolution& resolution)
        {
            if (resolution.Status == GeometryPropertyResolutionStatus::ValueKindMismatch)
                return "requires " + std::string{RenderAttributeExpectedTypeText(rule)};
            return std::string{ToString(resolution.Status)};
        }
    } // namespace

    EditorAttributeBindingModel BuildEditorAttributeBindingModel(
        const ECS::Scene::Registry& scene, const std::uint32_t stableEntityId)
    {
        EditorAttributeBindingModel model{.StableEntityId = stableEntityId};
        const entt::registry& raw = scene.Raw();
        const std::optional<ECS::EntityHandle> entity =
            EditorFeatureDetail::ResolveStableEntity(raw, stableEntityId);
        if (!entity.has_value())
            return model;
        model.HasEntity = true;

        const GeometryEntityAvailability availability = BuildGeometryAvailability(raw, *entity);
        for (const RenderAttributeRule& rule : RenderAttributeRules())
        {
            const Geometry::PropertySet* properties =
                ResolveGeometryPropertySet(availability, rule.Domain);
            if (properties == nullptr)
                continue;

            EditorAttributeBindingRow row{
                .Attribute = rule.Attribute,
                .Domain = rule.Domain,
                .ExpectedType = std::string{RenderAttributeExpectedTypeText(rule)},
                .ExpectedElementCount = properties->Size(),
                .DefaultSource = std::string{rule.DefaultDescription},
            };
            if (const std::optional<std::string> bound = BoundSourceName(raw, *entity, rule))
            {
                row.Bound = true;
                row.Source = GeometryPropertyRef{
                    .Domain = rule.Domain,
                    .Name = *bound,
                    .ValueKind = DetectGeometryPropertyValueKind(*properties, *bound),
                };
                row.Resolution =
                    ResolveRenderAttributeSource(availability, rule.Attribute, rule.Domain, *bound);
                row.UsingFallback = !row.Resolution.Resolved();
                if (row.UsingFallback)
                    row.Diagnostic = "'" + *bound + "' " + ResolutionReason(rule, row.Resolution) +
                                     "; drawing the default (" + row.DefaultSource + ")";
            }

            for (const std::string& name : properties->Properties())
            {
                if (IsTopologyProperty(rule.Domain, name))
                    continue;
                const GeometryPropertyResolution resolution =
                    ResolveRenderAttributeSource(availability, rule.Attribute, rule.Domain, name);
                row.Candidates.push_back(EditorAttributeBindingCandidate{
                    .Property = GeometryPropertyRef{
                        .Domain = rule.Domain,
                        .Name = name,
                        .ValueKind = resolution.ResolvedValueKind,
                    },
                    .ElementCount = resolution.ElementCount,
                    .Compatible = resolution.Resolved(),
                    .Reason = resolution.Status,
                    .DisabledReason = resolution.Resolved()
                        ? std::string{}
                        : ResolutionReason(rule, resolution),
                });
            }
            std::ranges::sort(row.Candidates, {},
                              [](const EditorAttributeBindingCandidate& c) -> const std::string& {
                                  return c.Property.Name;
                              });
            model.Rows.push_back(std::move(row));
        }
        return model;
    }
}
