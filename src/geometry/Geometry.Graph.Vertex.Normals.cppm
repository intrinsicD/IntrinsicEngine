module;

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <glm/glm.hpp>

export module Geometry.Graph.Vertex.Normals;

import Geometry.Graph;
import Geometry.Properties;

export namespace Geometry::Graph::VertexNormals
{
    inline constexpr std::string_view kDefaultPositionProperty = "v:point";
    inline constexpr std::string_view kDefaultOutputProperty = "v:normal";

    enum class RecomputeStatus : std::uint8_t
    {
        Success,
        EmptyGraph,
        InvalidPositionProperty,
        InvalidTopologyProperty,
        InvalidOutputProperty,
        PropertyTypeConflict,
        CountMismatch,
    };

    struct Params
    {
        std::string_view PositionProperty{kDefaultPositionProperty};
        std::string_view OutputProperty{kDefaultOutputProperty};
        glm::vec3 FallbackNormal{0.0f, 0.0f, 1.0f};
        double DegenerateNormalLengthEpsilon{1.0e-12};
        double CollinearEigenvalueRatioEpsilon{1.0e-5};
        bool SkipDeleted{true};
        bool OrientTowardFallback{true};
    };

    struct Diagnostics
    {
        std::size_t VertexSlotCount{0};
        std::size_t EdgeSlotCount{0};
        std::size_t WrittenCount{0};
        std::size_t ValidNormalVertexCount{0};
        std::size_t FallbackVertexCount{0};
        std::size_t IsolatedVertexCount{0};
        std::size_t DegreeOneVertexCount{0};
        std::size_t CollinearNeighborhoodCount{0};
        std::size_t DuplicatePositionCount{0};
        std::size_t NonFinitePositionCount{0};
        std::size_t InvalidEdgeCount{0};
        std::size_t SkippedDeletedVertexCount{0};
        std::size_t SkippedDeletedEdgeCount{0};
        bool FallbackNormalWasRepaired{false};
    };

    struct PropertySetResult
    {
        RecomputeStatus Status{RecomputeStatus::Success};
        Property<glm::vec3> Normals{};
        Diagnostics Diagnostics{};
    };

    struct Result
    {
        RecomputeStatus Status{RecomputeStatus::Success};
        VertexProperty<glm::vec3> Normals{};
        Diagnostics Diagnostics{};
    };

    [[nodiscard]] std::string_view DebugName(RecomputeStatus status) noexcept;

    [[nodiscard]] PropertySetResult Recompute(Vertices& vertices,
                                              ConstProperty<glm::vec3> positions,
                                              ConstProperty<HalfedgeConnectivity> halfedgeConnectivity,
                                              std::size_t edgeSlotCount,
                                              const Params& params = {},
                                              ConstProperty<bool> vertexDeleted = {},
                                              ConstProperty<bool> edgeDeleted = {});

    [[nodiscard]] Result Recompute(Graph& graph,
                                   const Params& params = {});
} // namespace Geometry::Graph::VertexNormals
