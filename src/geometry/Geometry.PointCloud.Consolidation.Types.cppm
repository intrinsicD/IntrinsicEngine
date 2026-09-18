// Consolidation status shared by algorithms and copied runtime results without
// importing point storage, spatial indexes or projection implementations.
module;
#include <cstdint>

export module Geometry.PointCloud.Consolidation.Types;

export namespace Geometry::PointCloud::Consolidation
{
    enum class Status : std::uint8_t
    {
        Success = 0,
        EmptyInput,
        TooFewPoints,
        InvalidCloud,
        NonFiniteInput,
        InvalidSupportRadius,
        InvalidRepulsionWeight,
        InvalidIterationLimit,
        InvalidConvergenceTolerance,
        InvalidTargetCount,
        InvalidMixtureComponentCount,
        InvalidMixtureParameters,
        InvalidNormalAngle,
        InvalidEdgeSensitivity,
        InvalidNormalRefinementRounds,
        NormalsRequired,
        InvalidNormals,
        NormalEstimationFailed,
        ResourceLimit,
        SpatialIndexBuildFailed,
        SpatialQueryFailed,
        EmptyNeighborhood,
        DensityEstimationFailed,
        MixtureFitFailed,
        MixtureNotConverged,
        EmptyContinuousAttraction,
        UpsamplingFailed,
        NumericalFailure,
        NotConverged,
        UnsupportedStrategy,
        InvalidNeighborhoods,
        InvalidProjectionState,
    };
}
