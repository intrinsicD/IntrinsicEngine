module;

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

module Extrinsic.Runtime.VertexChannelBindings;

import Extrinsic.Runtime.GeometryAvailability;

namespace Extrinsic::Runtime
{
    namespace
    {
        using D = GeometryElementDomain;
        using A = RenderAttribute;

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
        // colormapped, vectors are component colors. Infinity is a legal
        // scalar sentinel there, so finiteness is the overlay's concern.
        constexpr RenderAttributeRule ColorRow(
            const D domain, const std::string_view description) noexcept
        {
            return RenderAttributeRule{
                .Attribute = A::Color, .Domain = domain, .AcceptsFloat = true,
                .AcceptsVec3 = true, .AcceptsVec4 = true,
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
            Vec3Row(A::Position, D::MeshVertex, true, "v:position", "v:position"),
            Vec3Row(A::Position, D::GraphNode, true, "v:position", "v:position"),
            Vec3Row(A::Position, D::PointCloudPoint, true, "v:position", "v:position"),
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
    } // namespace

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
        if (rule.AcceptsFloat && rule.AcceptsVec3 && rule.AcceptsVec4)
            return "float, vec3 or vec4";
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
        if (!RenderAttributeAcceptsValueKind(*rule, resolution.ResolvedValueKind) ||
            IsTopologyProperty(domain, propertyName))
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
