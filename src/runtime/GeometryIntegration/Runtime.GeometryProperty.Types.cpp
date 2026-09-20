module;
#include <optional>
#include <cstdint>
#include <string>
#include <string_view>
module Extrinsic.Runtime.GeometryProperty.Types;

namespace Extrinsic::Runtime
{
    std::string_view ToString(const GeometryElementDomain domain) noexcept
    {
        switch (domain)
        {
        case GeometryElementDomain::Unknown:         return "Unknown";
        case GeometryElementDomain::MeshVertex:      return "MeshVertex";
        case GeometryElementDomain::MeshEdge:        return "MeshEdge";
        case GeometryElementDomain::MeshHalfedge:    return "MeshHalfedge";
        case GeometryElementDomain::MeshFace:        return "MeshFace";
        case GeometryElementDomain::GraphNode:       return "GraphNode";
        case GeometryElementDomain::GraphHalfedge:   return "GraphHalfedge";
        case GeometryElementDomain::GraphEdge:       return "GraphEdge";
        case GeometryElementDomain::PointCloudPoint: return "PointCloudPoint";
        }
        return "Unknown";
    }

    bool operator==(const GeometryPropertyRef& lhs,
                    const GeometryPropertyRef& rhs) noexcept
    {
        return lhs.Domain == rhs.Domain
            && lhs.ValueKind == rhs.ValueKind
            && lhs.Name == rhs.Name;
    }

    bool operator!=(const GeometryPropertyRef& lhs,
                    const GeometryPropertyRef& rhs) noexcept
    {
        return !(lhs == rhs);
    }

    const char* DebugNameForGeometryPropertyValueKind(
        const Geometry::PropertyValueKind kind) noexcept
    {
        switch (kind)
        {
        case Geometry::PropertyValueKind::Unknown: return "Unknown";
        case Geometry::PropertyValueKind::Bool:    return "Bool";
        case Geometry::PropertyValueKind::Int32:   return "Int32";
        case Geometry::PropertyValueKind::UInt32:  return "UInt32";
        case Geometry::PropertyValueKind::UInt64:  return "UInt64";
        case Geometry::PropertyValueKind::Float:   return "Float";
        case Geometry::PropertyValueKind::Double:  return "Double";
        case Geometry::PropertyValueKind::Vec2:    return "Vec2";
        case Geometry::PropertyValueKind::Vec3:    return "Vec3";
        case Geometry::PropertyValueKind::Vec4:    return "Vec4";
        }
        return "Unknown";
    }

    const char* DebugNameForGeometryPropertyValueKindFilter(
        const GeometryPropertyValueKindFilter filter) noexcept
    {
        return filter.has_value()
                   ? DebugNameForGeometryPropertyValueKind(*filter)
                   : "Any";
    }

    bool IsTopologyProperty(const GeometryElementDomain domain, const std::string_view name) noexcept
    {
        using D = GeometryElementDomain;
        switch (domain)
        {
        case D::MeshVertex: case D::GraphNode:
            return name == "v:deleted" || name == "v:connectivity" || name == "v:halfedge";
        case D::PointCloudPoint:
            return name == "v:deleted";
        case D::MeshEdge: case D::GraphEdge:
            return name == "e:deleted" || name == "e:v0" || name == "e:v1" || name == "e:connectivity";
        case D::MeshHalfedge: case D::GraphHalfedge:
            return name == "h:deleted" || name == "h:to_vertex" || name == "h:next" ||
                   name == "h:prev" || name == "h:opposite" || name == "h:face" || name == "h:connectivity";
        case D::MeshFace:
            return name == "f:deleted" || name == "f:halfedge" || name == "f:connectivity";
        case D::Unknown: return false;
        }
        return false;
    }

    bool IsStructuralVertexProperty(const std::string_view name) noexcept
    {
        return name == "v:position" || IsTopologyProperty(GeometryElementDomain::MeshVertex, name);
    }

    std::uint32_t GeometryPropertyComponentCount(const Geometry::PropertyValueKind kind) noexcept
    {
        using K = Geometry::PropertyValueKind;
        switch (kind)
        {
        case K::Bool: case K::Int32: case K::UInt32: case K::UInt64:
        case K::Float: case K::Double: return 1u;
        case K::Vec2: return 2u;
        case K::Vec3: return 3u;
        case K::Vec4: return 4u;
        case K::Unknown: return 0u;
        }
        return 0u;
    }

    bool MatchesGeometryPropertyValueKind(
        const GeometryPropertyValueKindFilter expected,
        const Geometry::PropertyValueKind actual) noexcept
    {
        return !expected.has_value() || *expected == actual;
    }
}
