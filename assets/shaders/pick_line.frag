// pick_line.frag — MRT picking fragment shader for line segments.
//
// Dual MRT output: EntityID (location 0) and PrimitiveID (location 1).
// PrimitiveID = segment index (passed from vertex shader as flat varying).

#version 460
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) out uint outEntityID;
layout(location = 1) out uint outPrimitiveID;

layout(location = 0) flat in uint vSegmentIndex;

layout(push_constant) uniform PickPushConsts {
    mat4     Model;
    uint64_t PtrPositions;
    uint64_t PtrEdges;
    uint     EntityID;
    uint     PrimitiveBase;
    float    PickWidth;
    float    ViewportWidth;
    float    ViewportHeight;
    uint     _pad;
} push;

void main() {
    outEntityID = push.EntityID;
    outPrimitiveID = vSegmentIndex;
}
