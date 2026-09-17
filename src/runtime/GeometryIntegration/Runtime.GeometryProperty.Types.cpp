module;
#include <optional>
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

    bool MatchesGeometryPropertyValueKind(
        const GeometryPropertyValueKindFilter expected,
        const Geometry::PropertyValueKind actual) noexcept
    {
        return !expected.has_value() || *expected == actual;
    }
}
