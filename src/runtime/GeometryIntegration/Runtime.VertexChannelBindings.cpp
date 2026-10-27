module;

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <glm/glm.hpp>

#include <entt/entity/registry.hpp>

module Extrinsic.Runtime.VertexChannelBindings;

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.Graphics.Component.RenderGeometry;
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
        // (surface normals +Z, point normals zero, texcoords zero), so only
        // positions and pixel sizes require finite sources.
        constexpr std::array kRules{
            Vec3Row(A::Position, D::MeshVertex, true, kPosition, kPosition),
            Vec3Row(A::Position, D::GraphNode, true, kPosition, kPosition),
            Vec3Row(A::Position, D::PointCloudPoint, true, kPosition, kPosition),
            Vec3Row(A::Normal, D::MeshVertex, false, "v:normal",
                    "h:normal corners, else v:normal, else +Z"),
            Vec3Row(A::Normal, D::MeshHalfedge, false, "h:normal",
                    "h:normal corners, else v:normal, else +Z"),
            Vec3Row(A::Normal, D::GraphNode, false, "", "none: points face the camera"),
            Vec3Row(A::Normal, D::PointCloudPoint, false, "p:normal", "p:normal, else points face the camera"),
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
        constexpr std::array kLaneRows{
            RenderLaneAttributeRow{L::Surface, A::Position, kMeshVertex, kPosition},
            RenderLaneAttributeRow{L::Surface, A::Normal, kSurfaceCorner, "h:normal corners, else v:normal, else +Z"},
            RenderLaneAttributeRow{
                L::Surface, A::Texcoord, kSurfaceCorner, "h:texcoord corners, else v:texcoord, else zero"},
            RenderLaneAttributeRow{
                L::Surface, A::Color, kSurfaceColor, "material or texture, v:color when present, else grey-white"},
            RenderLaneAttributeRow{L::Edges, A::Position, kEdgeVertices, kPosition},
            RenderLaneAttributeRow{L::Edges, A::Color, kEdgeColor, "v:color when present, else dark lane color"},
            RenderLaneAttributeRow{L::Edges, A::LineWidth, kEdgeElements, "uniform line width (px)"},
            RenderLaneAttributeRow{L::Points, A::Position, kPointVertices, kPosition},
            RenderLaneAttributeRow{L::Points,
                A::Normal,
                kPointVertices,
                "mesh v:normal, point cloud p:normal, graph none; without one the disc faces the camera; "
                "only Surfel points read it"},
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

    VertexChannelLaneBindings& LaneBindings(VertexChannelBindingSet& bindings, const GeometryRenderLane lane) noexcept
    {
        switch (lane)
        {
        case GeometryRenderLane::Surface: return bindings.Surface;
        case GeometryRenderLane::Edges:   return bindings.Edges;
        case GeometryRenderLane::Points:  return bindings.Points;
        }
        return bindings.Surface;
    }

    const VertexChannelLaneBindings& LaneBindings(
        const VertexChannelBindingSet& bindings, const GeometryRenderLane lane) noexcept
    {
        return LaneBindings(const_cast<VertexChannelBindingSet&>(bindings), lane);
    }

    DisplayedPositions ResolveDisplayedPositions(const Geometry::PropertySet& vertices,
        const GeometryElementDomain vertexDomain,
        const VertexChannelBindingSet* bindings,
        const GeometryRenderLane lane) noexcept
    {
        const Geometry::ConstPropertySet properties{vertices};
        const VertexChannelSourceBinding* position =
            bindings != nullptr ? &LaneBindings(*bindings, lane).Position : nullptr;
        if (position != nullptr && IsVertexChannelBindingEnabled(*position) &&
            position->Property.Domain == vertexDomain)
        {
            const std::string_view name = position->Property.Name;
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

    std::string_view ResolvePointNormalSource(const Geometry::PropertySet& vertices,
        const GeometryElementDomain vertexDomain,
        const VertexChannelBindingSet* bindings) noexcept
    {
        const Geometry::ConstPropertySet properties{vertices};
        const auto usable = [&](const std::string_view name)
        {
            const auto values = properties.Get<glm::vec3>(name);
            return values.IsValid() && values.Size() == vertices.Size();
        };
        if (bindings != nullptr && IsVertexChannelBindingEnabled(bindings->Points.Normal) &&
            bindings->Points.Normal.Property.Domain == vertexDomain && usable(bindings->Points.Normal.Property.Name))
        {
            return bindings->Points.Normal.Property.Name;
        }
        const RenderAttributeRule* rule = FindRenderAttributeRule(A::Normal, vertexDomain);
        return rule != nullptr && usable(rule->CanonicalProperty) ? rule->CanonicalProperty : std::string_view{};
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

    std::optional<GeometryRenderLane> PrimaryLaneFor(
        const ECS::Components::GeometrySources::Domain provenance, const RenderAttribute attribute) noexcept
    {
        using SourceDomain = ECS::Components::GeometrySources::Domain;
        const GeometryElementDomain vertexDomain = DisplayedPositionDomainFor(provenance);
        if (provenance != SourceDomain::Mesh && provenance != SourceDomain::Graph &&
            provenance != SourceDomain::PointCloud)
            return std::nullopt;
        for (const GeometryRenderLane lane :
            {GeometryRenderLane::Surface, GeometryRenderLane::Edges, GeometryRenderLane::Points})
        {
            if (RenderLaneReadsDomain(lane, attribute, vertexDomain))
                return lane;
        }
        return std::nullopt;
    }

    GeometryRenderLane SelectionLaneFor(
        const entt::registry& registry, const entt::entity entity, const GeometryElementDomain domain)
    {
        namespace G = Graphics::Components;
        namespace GS = ECS::Components::GeometrySources;
        if (domain == D::MeshFace)
            return GeometryRenderLane::Surface;
        if (domain == D::MeshEdge || domain == D::GraphEdge)
            return registry.all_of<G::RenderEdges>(entity) || domain == D::GraphEdge ? GeometryRenderLane::Edges
                                                                                     : GeometryRenderLane::Surface;
        if (registry.all_of<G::RenderPoints>(entity))
            return GeometryRenderLane::Points;
        return PrimaryLaneFor(
            GS::BuildSourceAvailability(GS::BuildConstView(registry, entity)).ProvenanceDomain, A::Position)
            .value_or(GeometryRenderLane::Points);
    }

    DisplayedLaneMask DisplayedPositionLanes(const entt::registry& registry,
        const entt::entity entity,
        const ECS::Components::GeometrySources::Domain provenance)
    {
        namespace G = Graphics::Components;
        using SourceDomain = ECS::Components::GeometrySources::Domain;
        DisplayedLaneMask lanes{};
        const std::optional<GeometryRenderLane> primary = PrimaryLaneFor(provenance, A::Position);
        if (!primary.has_value())
            return lanes;
        lanes[std::size_t(GeometryRenderLane::Surface)] =
            provenance == SourceDomain::Mesh && registry.all_of<G::RenderSurface>(entity);
        lanes[std::size_t(GeometryRenderLane::Edges)] =
            provenance != SourceDomain::PointCloud && registry.all_of<G::RenderEdges>(entity);
        lanes[std::size_t(GeometryRenderLane::Points)] = registry.all_of<G::RenderPoints>(entity);
        if (lanes == DisplayedLaneMask{})
            lanes[std::size_t(*primary)] = true;  // drawing nothing, it is still framed by its primary lane
        return lanes;
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
        const GS::Domain provenance = GS::BuildSourceAvailability(view).ProvenanceDomain;
        const GeometryElementDomain vertexDomain = DisplayedPositionDomainFor(provenance);
        return EntityDisplayedPositions{
            .Sources = ResolveDisplayedPositionSources(view.VertexSource->Properties,
                vertexDomain,
                bindings,
                DisplayedPositionLanes(registry, entity, provenance)),
            .Vertices = &view.VertexSource->Properties,
        };
    }

    std::vector<DisplayedPositions> ResolveDisplayedPositionSources(const Geometry::PropertySet& vertices,
        const GeometryElementDomain vertexDomain,
        const VertexChannelBindingSet* bindings,
        const DisplayedLaneMask& lanes)
    {
        std::vector<DisplayedPositions> sources;
        for (const GeometryRenderLane lane :
            {GeometryRenderLane::Surface, GeometryRenderLane::Edges, GeometryRenderLane::Points})
        {
            if (!lanes[std::size_t(lane)])
                continue;
            const DisplayedPositions positions = ResolveDisplayedPositions(vertices, vertexDomain, bindings, lane);
            if (std::ranges::none_of(sources,
                    [&](const DisplayedPositions& source)
                    {
                        return source.Name == positions.Name;
                    }))
                sources.push_back(positions);
        }
        return sources;
    }

    std::string DisplayedPositionsSignature(const EntityDisplayedPositions& displayed)
    {
        std::string signature;
        for (const DisplayedPositions& source : displayed.Sources)
        {
            const Geometry::PropertyRevision revision =
                displayed.Vertices != nullptr ? displayed.Vertices->FindPropertyRevision(source.Name).value_or(0u) : 0u;
            if (revision == 0u)
                return {};
            signature += source.Name;
            signature += '#';
            signature += std::to_string(revision);
            signature += ';';
        }
        return signature;
    }

    std::optional<Geometry::AABB> ComputeEntityLocalDisplayBounds(const entt::registry& raw, entt::entity entity)
    {
        const EntityDisplayedPositions displayed = ResolveEntityDisplayedPositions(raw, entity);
        if (displayed.AnyBound())
        {
            Geometry::AABB box{};
            for (const DisplayedPositions& source : displayed.Sources)
            {
                for (const auto& point : source.Values)
                {
                    box.Min = glm::min(box.Min, point);
                    box.Max = glm::max(box.Max, point);
                }
            }
            if (box.IsValid())
                return box;
        }
        if (const auto* bounds = raw.try_get<ECS::Components::Culling::Local::Bounds>(entity);
            bounds && bounds->LocalBoundingAABB.IsValid())
            return bounds->LocalBoundingAABB;
        return std::nullopt;
    }

    VertexChannelSourceBinding* FindVertexChannelSourceBinding(
        VertexChannelBindingSet& bindings, const GeometryRenderLane lane, const RenderAttribute attribute) noexcept
    {
        if (FindRenderLaneAttributeRow(lane, attribute) == nullptr)
            return nullptr;
        VertexChannelLaneBindings& slots = LaneBindings(bindings, lane);
        switch (attribute)
        {
        case A::Position:  return &slots.Position;
        case A::Normal:    return &slots.Normal;
        case A::Texcoord:  return &slots.Texcoord;
        case A::Color:
        case A::PointSize:
        case A::LineWidth: break;
        }
        return nullptr;
    }

    const VertexChannelSourceBinding* FindVertexChannelSourceBinding(const VertexChannelBindingSet& bindings,
        const GeometryRenderLane lane,
        const RenderAttribute attribute) noexcept
    {
        return FindVertexChannelSourceBinding(const_cast<VertexChannelBindingSet&>(bindings), lane, attribute);
    }
}
