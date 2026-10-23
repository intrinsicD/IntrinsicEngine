// The unified render-attribute binding model. Rows come from the runtime
// lane x attribute table (RenderLaneAttributeRows()), each validated per source
// domain by the attribute x element-domain table; the current source of each
// row is read from the storage that owns it (structural streams, the lane's
// visualization overlay, point/line render hints).
module;
#include <entt/entity/fwd.hpp>

#include <algorithm>
#include <array>
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
    EditorVisualizationTarget VisualizationTargetFor(const GeometryRenderLane lane) noexcept
    {
        switch (lane)
        {
        case GeometryRenderLane::Surface: return EditorVisualizationTarget::Surface;
        case GeometryRenderLane::Edges:   return EditorVisualizationTarget::Edges;
        case GeometryRenderLane::Points:  return EditorVisualizationTarget::Points;
        }
        return EditorVisualizationTarget::Surface;
    }

    std::optional<ColorOverlayTarget> ColorOverlayTargetFor(
        const GeometryElementDomain domain, const std::optional<EditorVisualizationTarget> lane) noexcept
    {
        using VD = Graphics::Components::VisualizationConfig::Domain;
        using CS = Graphics::Components::VisualizationConfig::ColorSource;
        const std::optional<GeometryRenderLane> defaultLane = DefaultColorLaneFor(domain);
        if (!defaultLane.has_value())
            return std::nullopt;
        // The buffer layout follows the property's domain; the lane is the chosen or default one.
        ColorOverlayTarget target{.Target = VisualizationTargetFor(*defaultLane)};
        if (domain == GeometryElementDomain::MeshFace)
            target = {target.Target, VD::Face, CS::PerFaceBuffer};
        else if (domain == GeometryElementDomain::MeshEdge || domain == GeometryElementDomain::GraphEdge)
            target = {target.Target, VD::Edge, CS::PerEdgeBuffer};
        if (!lane.has_value())
            return target;
        constexpr std::array kLanes{GeometryRenderLane::Surface, GeometryRenderLane::Edges, GeometryRenderLane::Points};
        const auto geometryLane = std::ranges::find_if(kLanes,
            [&](const GeometryRenderLane candidate)
            {
                return VisualizationTargetFor(candidate) == *lane;
            });
        if (geometryLane == kLanes.end() || !RenderLaneReadsDomain(*geometryLane, RenderAttribute::Color, domain))
            return std::nullopt;
        target.Target = *lane;
        return target;
    }

    std::optional<std::string> BoundColorOverlaySource(const entt::registry& raw,
        const ECS::EntityHandle entity,
        const GeometryElementDomain domain,
        const std::optional<EditorVisualizationTarget> lane)
    {
        namespace G = Graphics::Components;
        using Source = G::VisualizationConfig::ColorSource;
        const std::optional<ColorOverlayTarget> target = ColorOverlayTargetFor(domain, lane);
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

        // The property `lane` draws `rule.Attribute` from on `rule.Domain`, if
        // one is authored there. Position/Normal/Texcoord are still one
        // structural slot per entity, read by every lane.
        [[nodiscard]] std::optional<std::string> BoundSourceName(const entt::registry& raw,
            const ECS::EntityHandle entity,
            const GeometryRenderLane lane,
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
                return EditorFeatureDetail::BoundColorOverlaySource(
                    raw, entity, rule.Domain, EditorFeatureDetail::VisualizationTargetFor(lane));
            case RenderAttribute::PointSize:
                if (const auto* points = raw.try_get<G::RenderPoints>(entity);
                    points != nullptr && !points->SizeProperty.empty())
                    return points->SizeProperty;
                return std::nullopt;
            case RenderAttribute::LineWidth:
                if (const auto* edges = raw.try_get<G::RenderEdges>(entity);
                    edges != nullptr && !edges->WidthProperty.empty())
                    return edges->WidthProperty;
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
        for (const RenderLaneAttributeRow& laneRow : RenderLaneAttributeRows())
        {
            EditorAttributeBindingRow row{
                .Lane = laneRow.Lane,
                .Attribute = laneRow.Attribute,
                .DefaultSource = std::string{laneRow.DefaultDescription},
            };
            for (const GeometryElementDomain domain : laneRow.Domains)
            {
                const Geometry::PropertySet* properties = ResolveGeometryPropertySet(availability, domain);
                const RenderAttributeRule* rule = FindRenderAttributeRule(laneRow.Attribute, domain);
                if (properties == nullptr || rule == nullptr)
                    continue;
                if (row.Domains.empty())
                    row.ExpectedType = std::string{RenderAttributeExpectedTypeText(*rule)};
                row.Domains.push_back({.Domain = domain, .ElementCount = properties->Size()});

                if (const std::optional<std::string> bound = BoundSourceName(raw, *entity, laneRow.Lane, *rule);
                    bound.has_value() && !row.Bound)
                {
                    row.Bound = true;
                    row.Source = GeometryPropertyRef{
                        .Domain = domain,
                        .Name = *bound,
                        .ValueKind = DetectGeometryPropertyValueKind(*properties, *bound),
                    };
                    row.Resolution =
                        ResolveEditorAttributeBindingSource(availability, laneRow.Attribute, domain, *bound);
                    row.UsingFallback = !row.Resolution.Resolved();
                    if (row.UsingFallback)
                        row.Diagnostic = "'" + *bound + "' " + ResolutionReason(*rule, row.Resolution) +
                            "; drawing the default (" + row.DefaultSource + ")";
                }

                for (const std::string& name : properties->Properties())
                {
                    if (IsTopologyProperty(domain, name))
                        continue;
                    const GeometryPropertyResolution resolution =
                        ResolveEditorAttributeBindingSource(availability, laneRow.Attribute, domain, name);
                    row.Candidates.push_back(EditorAttributeBindingCandidate{
                        .Property =
                            GeometryPropertyRef{
                                .Domain = domain,
                                .Name = name,
                                .ValueKind = resolution.ResolvedValueKind,
                            },
                        .ElementCount = resolution.ElementCount,
                        .Compatible = resolution.Resolved(),
                        .Reason = resolution.Status,
                        .DisabledReason = resolution.Resolved() ? std::string{} : ResolutionReason(*rule, resolution),
                    });
                }
            }
            if (row.Domains.empty())
                continue;
            if (row.Attribute == RenderAttribute::Color)
                row.OverlayTarget = EditorFeatureDetail::VisualizationTargetFor(row.Lane);
            // By name; a name on several domains keeps the row's domain order.
            std::ranges::stable_sort(row.Candidates,
                {},
                [](const EditorAttributeBindingCandidate& c) -> const std::string&
                {
                    return c.Property.Name;
                });
            model.Rows.push_back(std::move(row));
        }
        return model;
    }
}
