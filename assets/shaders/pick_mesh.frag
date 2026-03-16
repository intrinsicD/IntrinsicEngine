// pick_mesh.frag — MRT picking fragment shader for mesh triangles.
//
// Dual MRT output: EntityID (location 0) and PrimitiveID (location 1).
// PrimitiveID packs a 2-bit primitive-domain tag in the high bits and the
// zero-based primitive index in the low 30 bits:
//   00 = surface triangle, 01 = line segment, 10 = point.
// This keeps the pick payload self-describing for mixed surface/line/point
// entities while preserving UINT_MAX as the invalid sentinel. For surfaces,
// the low 30 bits are the authoritative mesh face ID when PtrPrimitiveFaceIds
// is bound, otherwise they fall back to the raw raster triangle index.

#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) out uint outEntityID;
layout(location = 1) out uint outPrimitiveID;

layout(buffer_reference, scalar) readonly buffer PrimitiveFaceIdBuffer {
    uint faceId[];
};

layout(push_constant) uniform PickPushConsts {
    mat4     Model;
    uint64_t PtrPositions;
    uint64_t PtrAux;
    uint64_t PtrPrimitiveFaceIds;
    uint     EntityID;
    uint     PrimitiveBase;
    float    PickWidth;
    float    ViewportWidth;
    float    ViewportHeight;
    uint     _pad;
} push;

const uint kPrimitiveDomainSurfaceTriangle = 0u;
const uint kPrimitiveDomainShift = 30u;
const uint kPrimitiveIndexMask = 0x3fffffffu;

void main() {
    outEntityID = push.EntityID;
    uint primitiveIndex = push.PrimitiveBase + uint(gl_PrimitiveID);
    if (push.PtrPrimitiveFaceIds != 0ul)
    {
        PrimitiveFaceIdBuffer faceIds = PrimitiveFaceIdBuffer(push.PtrPrimitiveFaceIds);
        primitiveIndex = faceIds.faceId[primitiveIndex];
    }
    outPrimitiveID = (kPrimitiveDomainSurfaceTriangle << kPrimitiveDomainShift) |
                     (primitiveIndex & kPrimitiveIndexMask);
}
