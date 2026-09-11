// Shared scalar BDA layout and helpers; field order matches the host records.
#ifndef INTRINSIC_PROGRESSIVE_POISSON_STATE_GLSL
#define INTRINSIC_PROGRESSIVE_POISSON_STATE_GLSL

const uint InvalidIndex = 0xffffffffu;

const uint64_t EmptyHashKey = uint64_t(0xfffffffffffffffful);

layout(buffer_reference, scalar) readonly buffer FloatArrayRef
{
    float Values[];
};

layout(buffer_reference, scalar) readonly buffer UIntArrayReadRef
{
    uint Values[];
};

layout(buffer_reference, scalar) buffer UIntArrayRef
{
    uint Values[];
};

layout(buffer_reference, scalar) readonly buffer ProgressivePoissonGpuStateRef
{
    uint64_t PositionXBDA;
    uint64_t PositionYBDA;
    uint64_t PositionZBDA;
    uint64_t RemainingKeysBDA;
    uint64_t NextRemainingKeysBDA;
    uint64_t AcceptedKeysBDA;
    uint64_t CellKeysBDA;
    uint64_t CellPhasesBDA;
    uint64_t AcceptFlagsBDA;
    uint64_t CarryFlagsBDA;
    uint64_t HashKeysBDA;
    uint64_t HashValuesBDA;
    uint64_t LevelOffsetsBDA;
    uint64_t SplatRadiiBDA;
    uint64_t OutputCountBDA;
    uint64_t CompactionScratchBDA;
};

layout(push_constant, scalar) uniform ProgressivePoissonPush
{
    uint64_t StateBDA;
    uint InputCount;
    uint RemainingCount;
    uint HashTableCapacity;
    uint Dimension;
    uint GridWidth;
    uint LevelIndex;
    uint PhaseIndex;
    uint PhaseCount;
    float InvCellSize;
    float RadiusSquared;
    float OriginX;
    float OriginY;
    float OriginZ;
    float Reserved0;
} pc;

uint64_t packCell2D(int ix, int iy)
{
    const uint ux = uint(ix) ^ 0x80000000u;
    const uint uy = uint(iy) ^ 0x80000000u;
    return (uint64_t(ux) << 32) | uint64_t(uy);
}

uint64_t packCell3D(int ix, int iy, int iz)
{
    const uint64_t ux = uint64_t((ix + (1 << 20)) & 0x1fffff);
    const uint64_t uy = uint64_t((iy + (1 << 20)) & 0x1fffff);
    const uint64_t uz = uint64_t((iz + (1 << 20)) & 0x1fffff);
    return (ux << 42) | (uy << 21) | uz;
}

#endif
