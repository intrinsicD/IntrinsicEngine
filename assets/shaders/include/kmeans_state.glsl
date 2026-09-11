// Shared scalar BDA layout and helpers; field order matches the host records.
#ifndef INTRINSIC_KMEANS_STATE_GLSL
#define INTRINSIC_KMEANS_STATE_GLSL

layout(buffer_reference, scalar) buffer KMeansReductionRef
{
    uint64_t PackedMaxDistIndex; // (floatBitsToUint(dist) << 32) | pointIndex
    float InertiaSum;            // sum of assigned squared distances
    uint ChangedCount;           // number of points whose label changed
    uint MaxShiftBits;           // floatBitsToUint(max squared centroid shift)
    uint Converged;              // set by the host loop once tolerance is met
};

layout(buffer_reference, scalar) readonly buffer KMeansStateRef
{
    uint64_t PositionXBDA;
    uint64_t PositionYBDA;
    uint64_t PositionZBDA;
    uint64_t CentroidsBDA;
    uint64_t NextCentroidsBDA;
    uint64_t SumXBDA;
    uint64_t SumYBDA;
    uint64_t SumZBDA;
    uint64_t CountsBDA;
    uint64_t LabelsBDA;
    uint64_t SquaredDistancesBDA;
    uint64_t ReductionBDA;
};

layout(push_constant, scalar) uniform KMeansPush
{
    uint64_t StateBDA;
    uint PointCount;
    uint ClusterCount;
    uint GroupSize;
    uint Iteration;
    float ConvergenceTolSquared;
    float Reserved0;
    uint64_t NodesBDA;
} pc;

#endif
