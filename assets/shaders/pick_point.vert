// pick_point.vert — MRT picking vertex shader for points.
//
// Adapted from point_flatdisc.vert: expands each point into a camera-facing
// billboard quad (6 vertices per point) for disc-based picking. Outputs
// point index as PrimitiveBase + pointIndex for PrimitiveID MRT output.

#version 460
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
} camera;

layout(buffer_reference, scalar) readonly buffer PosBuf { vec3 v[]; };

layout(push_constant) uniform PickPushConsts {
    mat4     Model;
    uint64_t PtrPositions;
    uint64_t PtrAux;          // unused for points
    uint64_t PtrPrimitiveFaceIds;
    uint     EntityID;
    uint     PrimitiveBase;
    float    PickWidth;       // world-space point radius
    float    ViewportWidth;
    float    ViewportHeight;
    uint     _pad;
} push;

layout(location = 0) flat out uint vPointIndex;
layout(location = 1) out vec2 fragDiscUV;

void main()
{
    uint pointIndex = gl_VertexIndex / 6;
    uint vertexInQuad = gl_VertexIndex % 6;

    PosBuf posBuf = PosBuf(push.PtrPositions);
    vec3 localPos = posBuf.v[pointIndex];
    vec3 worldPos = vec3(push.Model * vec4(localPos, 1.0));

    vPointIndex = push.PrimitiveBase + pointIndex;

    // Quad vertex layout: 6 vertices → 4 corners.
    uint cornerIdx = uint[](0, 1, 2, 0, 2, 3)[vertexInQuad];
    vec2 localOffset = vec2[](vec2(-1,-1), vec2(1,-1), vec2(1,1), vec2(-1,1))[cornerIdx];
    fragDiscUV = localOffset;

    // World-space radius, clamped to safe range.
    float radiusWorld = clamp(push.PickWidth, 0.0001, 1.0);

    // Camera-facing billboard.
    vec4 viewPos = camera.view * vec4(worldPos, 1.0);
    vec3 cornerView = viewPos.xyz + vec3(localOffset.x, localOffset.y, 0.0) * radiusWorld;

    gl_Position = camera.proj * vec4(cornerView, 1.0);
}
