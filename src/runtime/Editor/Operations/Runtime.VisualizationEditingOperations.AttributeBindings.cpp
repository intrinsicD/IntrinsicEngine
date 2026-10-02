// RUNTIME-315: the unified render-attribute binding model. Rows come from the
// runtime attribute x element-domain table; the current source of each row is
// read from the storage that owns it (structural streams, visualization
// overlay, point/line render hints).
module;
#include <entt/entity/fwd.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <tuple>
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
import Extrinsic.Runtime.VisualizationRecipes;
import Geometry.Properties;

#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/internal/Runtime.EditorVisualizationHelpers.hpp"

extern "C++"
{
namespace Extrinsic::Runtime::EditorFeatureDetail
{
    std::optional<ColorOverlayTarget> ColorOverlayTargetFor(const GeometryElementDomain domain) noexcept
    {
        using VD = Graphics::Components::VisualizationConfig::Domain;
        using CS = Graphics::Components::VisualizationConfig::ColorSource;
        using T = EditorVisualizationTarget;
        using D = GeometryElementDomain;
        switch (domain)
        {
        case D::MeshVertex: return ColorOverlayTarget{T::Surface, VD::Vertex, CS::PerVertexBuffer};
        case D::MeshFace: return ColorOverlayTarget{T::Surface, VD::Face, CS::PerFaceBuffer};
        case D::MeshEdge:
        case D::GraphEdge: return ColorOverlayTarget{T::Edges, VD::Edge, CS::PerEdgeBuffer};
        case D::GraphNode: return ColorOverlayTarget{T::Edges, VD::Vertex, CS::PerVertexBuffer};
        case D::PointCloudPoint: return ColorOverlayTarget{T::Points, VD::Vertex, CS::PerVertexBuffer};
        case D::MeshHalfedge:
        case D::GraphHalfedge:
        case D::Unknown: break;
        }
        return std::nullopt;
    }

    std::optional<std::string> BoundColorOverlaySource(
        const entt::registry& raw, const ECS::EntityHandle entity, const GeometryElementDomain domain)
    {
        namespace G = Graphics::Components;
        using Source = G::VisualizationConfig::ColorSource;
        const std::optional<ColorOverlayTarget> target = ColorOverlayTargetFor(domain);
        if (!target.has_value())
            return std::nullopt;
        const std::optional<G::VisualizationConfig> config =
            EffectiveVisualizationConfigForTarget(raw, entity, target->Target);
        if (!config.has_value())
            return std::nullopt;
        if (config->Source == Source::ScalarField && config->ScalarDomain == target->VisDomain &&
            !config->ScalarFieldName.empty())
        {
            return config->ScalarFieldName;
        }
        if (config->Source == target->BufferSource && !config->ColorBufferName.empty())
            return config->ColorBufferName;
        return std::nullopt;
    }
}
}

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace G = Graphics::Components;
        using D = GeometryElementDomain;

        [[nodiscard]] GeometryPropertyResolutionStatus ToResolutionStatus(
            const VisualizationRecipeStatus status) noexcept
        {
            using S = GeometryPropertyResolutionStatus;
            switch (status)
            {
            case VisualizationRecipeStatus::Encoded: return S::Resolved;
            case VisualizationRecipeStatus::UnsupportedDomain: return S::UnsupportedDomain;
            case VisualizationRecipeStatus::MissingSource: return S::MissingProperty;
            case VisualizationRecipeStatus::NonFiniteValue:
            case VisualizationRecipeStatus::InvalidRange: return S::NonFiniteValues;
            case VisualizationRecipeStatus::EmptySource:
            case VisualizationRecipeStatus::ElementCountMismatch:
            case VisualizationRecipeStatus::ElementCountOverflow: return S::ElementCountMismatch;
            default: return S::ValueKindMismatch;
            }
        }

        [[nodiscard]] GeometryPropertyResolution ResolveUncached(
            const GeometryEntityAvailability& availability, const RenderAttribute attribute,
            const D domain, const std::string_view name)
        {
            GeometryPropertyResolution resolution =
                ResolveRenderAttributeSource(availability, attribute, domain, name);
            const RenderAttributeRule* rule = FindRenderAttributeRule(attribute, domain);
            if (!resolution.Resolved() || rule == nullptr || !rule->ValidatedByVisualizationRecipe)
                return resolution;
            // Color draws through the property-display recipe, so its encoder
            // is the validator; no parallel kind/value rules here.
            const VisualizationEncodingResult encoded = EncodeVisualizationRecipe(
                availability,
                MakeEditorPropertyVisualizationRecipe(GeometryPropertyRef{
                    .Domain = domain, .Name = std::string{name},
                    .ValueKind = resolution.ResolvedValueKind}));
            resolution.Status = ToResolutionStatus(encoded.Status);
            return resolution;
        }

        // Value scans (finiteness, recipe encoding) are O(elements); the model
        // is rebuilt whenever the selected-model cache misses, so results are
        // memoized by the property's process-unique content revision.
        struct SourceScanKey
        {
            RenderAttribute Attribute{};
            D Domain{};
            std::string Name{};
            Geometry::PropertyRevision Revision{0u};
            std::size_t Count{0u};

            [[nodiscard]] friend bool operator<(const SourceScanKey& lhs, const SourceScanKey& rhs)
            {
                return std::tie(lhs.Revision, lhs.Attribute, lhs.Domain, lhs.Count, lhs.Name) <
                       std::tie(rhs.Revision, rhs.Attribute, rhs.Domain, rhs.Count, rhs.Name);
            }
        };

        constexpr std::size_t kMaxMemoizedSourceScans = 1024u;
        std::mutex g_SourceScanMutex;
        std::map<SourceScanKey, GeometryPropertyResolution> g_SourceScans;

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
                return EditorFeatureDetail::BoundColorOverlaySource(raw, entity, rule.Domain);
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

    GeometryPropertyResolution ResolveEditorAttributeBindingSource(
        const GeometryEntityAvailability& availability, const RenderAttribute attribute,
        const GeometryElementDomain domain, const std::string_view propertyName)
    {
        const Geometry::PropertySet* properties = ResolveGeometryPropertySet(availability, domain);
        const Geometry::PropertyRevision revision = properties != nullptr
            ? properties->FindPropertyRevision(propertyName).value_or(0u)
            : 0u;
        if (revision == 0u)  // unmodified or absent: no stable identity to key on
            return ResolveUncached(availability, attribute, domain, propertyName);

        SourceScanKey key{attribute, domain, std::string{propertyName}, revision, properties->Size()};
        {
            const std::scoped_lock lock{g_SourceScanMutex};
            if (const auto found = g_SourceScans.find(key); found != g_SourceScans.end())
                return found->second;
        }
        const GeometryPropertyResolution resolution =
            ResolveUncached(availability, attribute, domain, propertyName);
        const std::scoped_lock lock{g_SourceScanMutex};
        if (g_SourceScans.size() >= kMaxMemoizedSourceScans)
            g_SourceScans.clear();
        g_SourceScans.emplace(std::move(key), resolution);
        return resolution;
    }

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
                row.Resolution = ResolveEditorAttributeBindingSource(
                    availability, rule.Attribute, rule.Domain, *bound);
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
                    ResolveEditorAttributeBindingSource(availability, rule.Attribute, rule.Domain, name);
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
