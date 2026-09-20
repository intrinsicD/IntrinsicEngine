// Canonical property references and kind filters for config and copied runtime data.
module;
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
export module Extrinsic.Runtime.GeometryProperty.Types;
export import Geometry.Properties.Types;

export namespace Extrinsic::Runtime
{
    enum class GeometryElementDomain : std::uint8_t
    {
        Unknown,
        MeshVertex,
        MeshEdge,
        MeshHalfedge,
        MeshFace,
        GraphNode,
        GraphHalfedge,
        GraphEdge,
        PointCloudPoint,
    };

    [[nodiscard]] std::string_view ToString(GeometryElementDomain domain) noexcept;

    // nullopt accepts any kind; it is a query constraint, not a stored value kind.
    using GeometryPropertyValueKindFilter = std::optional<Geometry::PropertyValueKind>;

    // Authoring identity only: no entity, borrowed storage or generation state.
    struct GeometryPropertyRef
    {
        GeometryElementDomain       Domain{GeometryElementDomain::Unknown};
        std::string                 Name{};
        Geometry::PropertyValueKind ValueKind{
            Geometry::PropertyValueKind::Unknown};

        [[nodiscard]] bool HasName() const noexcept { return !Name.empty(); }
    };

    [[nodiscard]] bool operator==(const GeometryPropertyRef& lhs,
                                  const GeometryPropertyRef& rhs) noexcept;
    [[nodiscard]] bool operator!=(const GeometryPropertyRef& lhs,
                                  const GeometryPropertyRef& rhs) noexcept;

    [[nodiscard]] const char* DebugNameForGeometryPropertyValueKind(
        Geometry::PropertyValueKind kind) noexcept;
    [[nodiscard]] const char* DebugNameForGeometryPropertyValueKindFilter(
        GeometryPropertyValueKindFilter filter) noexcept;
    [[nodiscard]] bool IsTopologyProperty(GeometryElementDomain domain, std::string_view name) noexcept;

    // Canonical vertex storage belongs to topology/geometry authoring, not field outputs.
    [[nodiscard]] bool IsStructuralVertexProperty(std::string_view name) noexcept;

    // Shape only; storage conversion and method-specific validity are separate checks.
    [[nodiscard]] std::uint32_t GeometryPropertyComponentCount(
        Geometry::PropertyValueKind kind) noexcept;

    [[nodiscard]] bool MatchesGeometryPropertyValueKind(
        GeometryPropertyValueKindFilter expected,
        Geometry::PropertyValueKind actual) noexcept;
}
