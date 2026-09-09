#include "point_lbvh.glsl"
layout(push_constant,scalar) uniform BuildPush
{
    uint64_t points; uint64_t keys; uint64_t nodes; uint64_t bounds;
    uint count; uint padded; uint stride; uint j; uint k; uint reserved;
    uint64_t objectIndices;
} pc;
layout(local_size_x=256) in;
