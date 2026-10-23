module;

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include <glm/glm.hpp>

#include <entt/entity/registry.hpp>

module Extrinsic.Runtime.VertexChannelBindings;

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.Runtime.GeometryAvailability;

namespace Extrinsic::Runtime
{
    namespace
    {
        using D = GeometryElementDomain;
        using A = RenderAttribute;
        constexpr std::string_view kPosition =
            ECS::Components::GeometrySources::PropertyNames::kPosition;

        constexpr RenderAttributeRule Vec3Row(
            const A attribute, const D domain, const bool finite,
            const std::string_view canonical, const std::string_view description) noexcept
        {
            return RenderAttributeRule{
                .Attribute = attribute, .Domain = domain, .AcceptsVec3 = true,
                .RequireFiniteValues = finite, .CanonicalProperty = canonical,
                .DefaultDescription = description};
        }

        constexpr RenderAttributeRule Vec2Row(
            const D domain, const std::string_view canonical,
            const std::string_view description) noexcept
        {
            return RenderAttributeRule{
                .Attribute = A::Texcoord, .Domain = domain, .AcceptsVec2 = true,
                .CanonicalProperty = canonical, .DefaultDescription = description};
        }

        // Color goes through the visualization overlay: scalars are
        // colormapped, vectors are component colors; its encoder decides
        // which kinds and values are drawable.
        constexpr RenderAttributeRule ColorRow(
            const D domain, const std::string_view description) noexcept
        {
            return RenderAttributeRule{
                .Attribute = A::Color, .Domain = domain,
                .ValidatedByVisualizationRecipe = true,
                .DefaultDescription = description};
        }

        constexpr RenderAttributeRule PixelRow(
            const A attribute, const D domain, const std::string_view description) noexcept
        {
            return RenderAttributeRule{
                .Attribute = attribute, .Domain = domain, .AcceptsFloat = true,
                .RequireFiniteValues = true, .DefaultDescription = description};
        }

        // Shading normals and texcoords repair non-finite elements per element
        // (+Z / zero), so only positions and pixel sizes require finite sources.
        constexpr std::array kRules{
            Vec3Row(A::Position, D::MeshVertex, true, kPosition, kPosition),
            Vec3Row(A::Position, D::GraphNode, true, kPosition, kPosition),
            Vec3Row(A::Position, D::PointCloudPoint, true, kPosition, kPosition),
            Vec3Row(A::Normal, D::MeshVertex, false, "v:normal",
                    "h:normal corners, else v:normal, else +Z"),
            Vec3Row(A::Normal, D::MeshHalfedge, false, "h:normal",
                    "h:normal corners, else v:normal, else +Z"),
            Vec3Row(A::Normal, D::GraphNode, false, "", "no normal stream"),
            Vec3Row(A::Normal, D::PointCloudPoint, false, "", "no normal stream"),
            Vec2Row(D::MeshVertex, "v:texcoord", "h:texcoord corners, else v:texcoord, else zero"),
            Vec2Row(D::MeshHalfedge, "h:texcoord", "h:texcoord corners, else v:texcoord, else zero"),
            ColorRow(D::MeshVertex, "material (v:color vertex colors when present)"),
            ColorRow(D::MeshEdge, "lane color"),
            ColorRow(D::MeshFace, "material"),
            ColorRow(D::GraphNode, "lane color"),
            ColorRow(D::GraphEdge, "lane color"),
            ColorRow(D::PointCloudPoint, "lane color"),
            PixelRow(A::PointSize, D::MeshVertex, "uniform point size (px)"),
            PixelRow(A::PointSize, D::GraphNode, "uniform point size (px)"),
            PixelRow(A::PointSize, D::PointCloudPoint, "uniform point size (px)"),
            PixelRow(A::LineWidth, D::MeshEdge, "uniform line width (px)"),
            PixelRow(A::LineWidth, D::GraphEdge, "uniform line width (px)"),
        };

        constexpr std::array kMeshVertex{D::MeshVertex};
        constexpr std::array kSurfaceCorner{D::MeshVertex, D::MeshHalfedge};
        constexpr std::array kSurfaceColor{D::MeshVertex, D::MeshFace};
        constexpr std::array kEdgeVertices{D::MeshVertex, D::GraphNode};
        constexpr std::array kEdgeColor{D::MeshVertex, D::MeshEdge, D::GraphNode, D::GraphEdge};
        constexpr std::array kEdgeElements{D::MeshEdge, D::GraphEdge};
        constexpr std::array kPointVertices{D::MeshVertex, D::GraphNode, D::PointCloudPoint};

        using L = GeometryRenderLane;
        // Position and Normal are one structural stream per entity until each
        // lane gets its own slot, so their rows say the binding is shared.
        constexpr std::array kLaneRows{
            RenderLaneAttributeRow{L::Surface, A::Position, kMeshVertex, "v:position; shared by all primitives"},
            RenderLaneAttributeRow{L::Surface,
                A::Normal,
                kSurfaceCorner,
                "h:normal corners, else v:normal, else +Z; shared by all primitives"},
            RenderLaneAttributeRow{
                L::Surface, A::Texcoord, kSurfaceCorner, "h:texcoord corners, else v:texcoord, else zero"},
            RenderLaneAttributeRow{
                L::Surface, A::Color, kSurfaceColor, "material or texture, v:color when present, else grey-white"},
            RenderLaneAttributeRow{L::Edges, A::Position, kEdgeVertices, "v:position; shared by all primitives"},
            RenderLaneAttributeRow{L::Edges, A::Color, kEdgeColor, "v:color when present, else dark lane color"},
            RenderLaneAttributeRow{L::Edges, A::LineWidth, kEdgeElements, "uniform line width (px)"},
            RenderLaneAttributeRow{L::Points, A::Position, kPointVertices, "v:position; shared by all primitives"},
            RenderLaneAttributeRow{
                L::Points, A::Normal, kPointVertices, "v:normal, not yet read by points; shared by all primitives"},
            RenderLaneAttributeRow{
                L::Points, A::Color, kPointVertices, "v:color or p:color when present, else dark lane color"},
            RenderLaneAttributeRow{L::Points, A::PointSize, kPointVertices, "uniform point size (px)"},
        };
    } // namespace

    std::span<const RenderLaneAttributeRow> RenderLaneAttributeRows() noexcept
    {
        return kLaneRows;
    }

    const RenderLaneAttributeRow* FindRenderLaneAttributeRow(
        const GeometryRenderLane lane, const RenderAttribute attribute) noexcept
    {
        const auto found = std::ranges::find_if(kLaneRows,
            [&](const RenderLaneAttributeRow& row)
            {
                return row.Lane == lane && row.Attribute == attribute;
            });
        return found != kLaneRows.end() ? &*found : nullptr;
    }

    bool RenderLaneReadsDomain(
        const GeometryRenderLane lane, const RenderAttribute attribute, const GeometryElementDomain domain) noexcept
    {
        const RenderLaneAttributeRow* row = FindRenderLaneAttributeRow(lane, attribute);
        return row != nullptr && std::ranges::find(row->Domains, domain) != row->Domains.end();
    }

    std::optional<GeometryRenderLane> DefaultColorLaneFor(const GeometryElementDomain domain) noexcept
    {
        switch (domain)
        {
        case D::MeshVertex:
        case D::MeshFace:        return GeometryRenderLane::Surface;
        case D::MeshEdge:
        case D::GraphNode:
        case D::GraphEdge:       return GeometryRenderLane::Edges;
        case D::PointCloudPoint: return GeometryRenderLane::Points;
        case D::MeshHalfedge:
        case D::GraphHalfedge:
        case D::Unknown:         break;
        }
        return std::nullopt;
    }

    std::string_view ToString(const RenderAttribute attribute) noexcept
    {
        switch (attribute)
        {
        case A::Position: return "position";
        case A::Normal: return "normal";
        case A::Texcoord: return "texcoord";
        case A::Color: return "color";
        case A::PointSize: return "point_size";
        case A::LineWidth: return "line_width";
        }
        return "unknown";
    }

    bool TryParseRenderAttribute(const std::string_view value, RenderAttribute& out) noexcept
    {
        for (const A attribute : {A::Position, A::Normal, A::Texcoord, A::Color,
                                  A::PointSize, A::LineWidth})
        {
            if (value == ToString(attribute))
            {
                out = attribute;
                return true;
            }
        }
        return false;
    }

    std::span<const RenderAttributeRule> RenderAttributeRules() noexcept
    {
        return kRules;
    }

    const RenderAttributeRule* FindRenderAttributeRule(
        const RenderAttribute attribute, const GeometryElementDomain domain) noexcept
    {
        for (const RenderAttributeRule& rule : kRules)
        {
            if (rule.Attribute == attribute && rule.Domain == domain)
                return &rule;
        }
        return nullptr;
    }

    bool RenderAttributeAcceptsValueKind(
        const RenderAttributeRule& rule, const Geometry::PropertyValueKind kind) noexcept
    {
        using K = Geometry::PropertyValueKind;
        return (kind == K::Float && rule.AcceptsFloat) ||
               (kind == K::Vec2 && rule.AcceptsVec2) ||
               (kind == K::Vec3 && rule.AcceptsVec3) ||
               (kind == K::Vec4 && rule.AcceptsVec4);
    }

    std::string_view RenderAttributeExpectedTypeText(const RenderAttributeRule& rule) noexcept
    {
        if (rule.ValidatedByVisualizationRecipe)
            return "scalar or vector";
        if (rule.AcceptsVec3)
            return "vec3";
        if (rule.AcceptsVec2)
            return "vec2";
        if (rule.AcceptsFloat)
            return "float";
        return "unsupported";
    }

    GeometryPropertyResolution ResolveRenderAttributeSource(
        const GeometryEntityAvailability& availability,
        const RenderAttribute attribute,
        const GeometryElementDomain domain,
        const std::string_view propertyName) noexcept
    {
        const RenderAttributeRule* rule = FindRenderAttributeRule(attribute, domain);
        if (rule == nullptr || !SupportsGeometryElementDomain(availability, domain))
            return GeometryPropertyResolution{};  // UnsupportedDomain

        GeometryPropertyResolution resolution = ResolveGeometryProperty(
            availability, domain, propertyName, std::nullopt,
            ResolveGeometryElementCount(availability, domain), false);
        if (resolution.Status == GeometryPropertyResolutionStatus::MissingName ||
            resolution.Status == GeometryPropertyResolutionStatus::MissingProperty ||
            resolution.Status == GeometryPropertyResolutionStatus::UnsupportedDomain)
        {
            return resolution;
        }
        if (IsTopologyProperty(domain, propertyName) ||
            (!rule->ValidatedByVisualizationRecipe &&
             !RenderAttributeAcceptsValueKind(*rule, resolution.ResolvedValueKind)))
        {
            resolution.Status = GeometryPropertyResolutionStatus::ValueKindMismatch;
            return resolution;
        }
        if (resolution.Resolved() && rule->RequireFiniteValues)
        {
            const Geometry::PropertySet* properties =
                ResolveGeometryPropertySet(availability, domain);
            if (properties == nullptr ||
                !GeometryPropertyValuesAreFinite(*properties, propertyName))
            {
                resolution.Status = GeometryPropertyResolutionStatus::NonFiniteValues;
            }
        }
        return resolution;
    }

    DisplayedPositions ResolveDisplayedPositions(
        const Geometry::PropertySet& vertices,
        const GeometryElementDomain vertexDomain,
        const VertexChannelBindingSet* bindings) noexcept
    {
        const Geometry::ConstPropertySet properties{vertices};
        if (bindings != nullptr && IsVertexChannelBindingEnabled(bindings->Position) &&
            bindings->Position.Property.Domain == vertexDomain)
        {
            const std::string_view name = bindings->Position.Property.Name;
            const auto bound = properties.Get<glm::vec3>(name);
            if (bound.IsValid() && bound.Size() == vertices.Size() &&
                GeometryPropertyValuesAreFinite(vertices, name))
            {
                return DisplayedPositions{.Name = name, .Values = bound.Span(), .Bound = true};
            }
        }
        const auto canonical = properties.Get<glm::vec3>(kPosition);
        return DisplayedPositions{
            .Name = kPosition,
            .Values = canonical.IsValid() ? canonical.Span() : std::span<const glm::vec3>{},
        };
    }

    GeometryElementDomain DisplayedPositionDomainFor(
        const ECS::Components::GeometrySources::Domain provenance) noexcept
    {
        using SourceDomain = ECS::Components::GeometrySources::Domain;
        switch (provenance)
        {
        case SourceDomain::Mesh: return D::MeshVertex;
        case SourceDomain::Graph: return D::GraphNode;
        case SourceDomain::PointCloud: return D::PointCloudPoint;
        case SourceDomain::None:
        case SourceDomain::Unknown: break;
        }
        return D::Unknown;
    }

    EntityDisplayedPositions ResolveEntityDisplayedPositions(
        const entt::registry& registry, const entt::entity entity)
    {
        namespace GS = ECS::Components::GeometrySources;
        const auto* bindings = registry.try_get<VertexChannelBindingSet>(entity);
        if (bindings == nullptr)
            return {};
        const GS::ConstSourceView view = GS::BuildConstView(registry, entity);
        if (view.VertexSource == nullptr)
            return {};
        return EntityDisplayedPositions{
            .Positions = ResolveDisplayedPositions(
                view.VertexSource->Properties,
                DisplayedPositionDomainFor(GS::BuildSourceAvailability(view).ProvenanceDomain),
                bindings),
            .Vertices = &view.VertexSource->Properties};
    }

    std::optional<Geometry::AABB> ComputeEntityLocalDisplayBounds(const entt::registry& raw, entt::entity entity)
    {
        const auto displayed = ResolveEntityDisplayedPositions(raw, entity).Positions;
        if (displayed.Bound && !displayed.Values.empty())
        {
            Geometry::AABB box{};
            for (const auto& point : displayed.Values)
            {
                box.Min = glm::min(box.Min, point);
                box.Max = glm::max(box.Max, point);
            }
            return box;
        }
        if (const auto* bounds = raw.try_get<ECS::Components::Culling::Local::Bounds>(entity);
            bounds && bounds->LocalBoundingAABB.IsValid())
            return bounds->LocalBoundingAABB;
        return std::nullopt;
    }

    VertexChannelSourceBinding* FindVertexChannelSourceBinding(
        VertexChannelBindingSet& bindings, const RenderAttribute attribute) noexcept
    {
        switch (attribute)
        {
        case A::Position: return &bindings.Position;
        case A::Normal: return &bindings.Normal;
        case A::Texcoord: return &bindings.Texcoord;
        case A::Color:
        case A::PointSize:
        case A::LineWidth: break;
        }
        return nullptr;
    }

    const VertexChannelSourceBinding* FindVertexChannelSourceBinding(
        const VertexChannelBindingSet& bindings, const RenderAttribute attribute) noexcept
    {
        return FindVertexChannelSourceBinding(
            const_cast<VertexChannelBindingSet&>(bindings), attribute);
    }
}
