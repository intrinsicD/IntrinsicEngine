// pick_point.frag — MRT picking fragment shader for points.
//
// Dual MRT output: EntityID (location 0) and PrimitiveID (location 1).
// Disc discard test: fragments outside the unit circle are discarded
// to produce clean circular pick targets.

#version 460
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) out uint outEntityID;
layout(location = 1) out uint outPrimitiveID;

layout(location = 0) flat in uint vPointIndex;
layout(location = 1) in vec2 fragDiscUV;

layout(push_constant) uniform PickPushConsts {
    mat4     Model;
    uint64_t PtrPositions;
    uint64_t PtrAux;
    uint     EntityID;
    uint     PrimitiveBase;
    float    PickWidth;
    float    ViewportWidth;
    float    ViewportHeight;
    uint     _pad;
} push;

void main() {
    // Disc discard: reject fragments outside the unit circle.
    if (dot(fragDiscUV, fragDiscUV) > 1.0)
        discard;

    outEntityID = push.EntityID;
    outPrimitiveID = vPointIndex;
}
