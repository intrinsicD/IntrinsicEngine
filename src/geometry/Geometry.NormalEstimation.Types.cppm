// Normal orientation, mesh weighting and copied diagnostics without algorithm dependencies.
module;
#include <cstddef>
#include <cstdint>

export module Geometry.NormalEstimation.Types;

export namespace Geometry::PointCloud::Normals
{
    enum class OrientationMode : std::uint8_t
    {
        None,
        MinimumSpanningTree,
    };

    struct Diagnostics
    {
        std::size_t PointSlotCount{0};
        std::size_t FinitePointCount{0};
        std::size_t WrittenCount{0};
        std::size_t ValidNormalPointCount{0};
        std::size_t FallbackPointCount{0};
        std::size_t DegenerateNeighborhoodCount{0};
        std::size_t TooFewNeighborCount{0};
        std::size_t CollinearNeighborhoodCount{0};
        std::size_t DuplicatePositionCount{0};
        std::size_t NonFinitePointCount{0};
        std::size_t SkippedDeletedPointCount{0};
        std::size_t SpatialQueryFailureCount{0};
        std::size_t FlippedOrientationCount{0};
        std::size_t KNNVisitedNodeCount{0};
        std::size_t KNNDistanceEvaluationCount{0};
        bool FallbackNormalWasRepaired{false};
    };
}

export namespace Geometry::HalfedgeMesh::VertexNormals
{
    enum class AveragingMode : std::uint8_t
    {
        UniformFace,
        AreaWeighted,
        AngleWeighted,
        AreaAngleWeighted,
        MaxWeighted,
    };
}
