// pick_mesh.frag — MRT picking fragment shader for mesh triangles.
//
// Dual MRT output: EntityID (location 0) and PrimitiveID (location 1).
// PrimitiveID = PrimitiveBase + gl_PrimitiveID, allowing per-entity
// base offsets for future batched draws.

#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) out uint outEntityID;
layout(location = 1) out uint outPrimitiveID;

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
    outEntityID = push.EntityID;
    outPrimitiveID = push.PrimitiveBase + uint(gl_PrimitiveID);
}
