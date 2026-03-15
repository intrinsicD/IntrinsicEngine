// pick_line.vert — MRT picking vertex shader for line segments.
//
// Adapted from line.vert: expands each edge into a screen-space quad
// (6 vertices per segment) for thick-line picking. Outputs segment index
// as PrimitiveBase + segmentIndex for PrimitiveID MRT output.

#version 460
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
} camera;

layout(buffer_reference, scalar) readonly buffer PosBuf { vec3 v[]; };

struct EdgePair {
    uint i0;
    uint i1;
};
layout(buffer_reference, scalar) readonly buffer EdgeBuf { EdgePair e[]; };

layout(push_constant) uniform PickPushConsts {
    mat4     Model;
    uint64_t PtrPositions;
    uint64_t PtrEdges;        // BDA to edge pair buffer
    uint     EntityID;
    uint     PrimitiveBase;
    float    PickWidth;       // line width in pixels
    float    ViewportWidth;
    float    ViewportHeight;
    uint     _pad;
} push;

layout(location = 0) flat out uint vSegmentIndex;

void main()
{
    uint segmentIndex = gl_VertexIndex / 6;
    uint vertexInQuad = gl_VertexIndex % 6;

    EdgeBuf eBuf = EdgeBuf(push.PtrEdges);
    EdgePair edge = eBuf.e[segmentIndex];

    PosBuf posBuf = PosBuf(push.PtrPositions);
    vec3 posA = posBuf.v[edge.i0];
    vec3 posB = posBuf.v[edge.i1];

    vec3 worldA = vec3(push.Model * vec4(posA, 1.0));
    vec3 worldB = vec3(push.Model * vec4(posB, 1.0));

    vec4 clipA = camera.proj * camera.view * vec4(worldA, 1.0);
    vec4 clipB = camera.proj * camera.view * vec4(worldB, 1.0);

    vec2 ndcA = clipA.xy / clipA.w;
    vec2 ndcB = clipB.xy / clipB.w;

    vec2 viewport = vec2(push.ViewportWidth, push.ViewportHeight);
    vec2 screenA = (ndcA * 0.5 + 0.5) * viewport;
    vec2 screenB = (ndcB * 0.5 + 0.5) * viewport;

    vec2 dir = screenB - screenA;
    float len = length(dir);

    vSegmentIndex = push.PrimitiveBase + segmentIndex;

    if (len < 0.001)
    {
        gl_Position = clipA;
        return;
    }

    dir /= len;
    vec2 perp = vec2(-dir.y, dir.x);

    float clampedWidth = clamp(push.PickWidth, 0.5, 32.0);
    float halfWidth = clampedWidth * 0.5;
    float capExtend = halfWidth;

    uint cornerIndex;
    if      (vertexInQuad == 0 || vertexInQuad == 3) cornerIndex = 0;
    else if (vertexInQuad == 1)                      cornerIndex = 1;
    else if (vertexInQuad == 2 || vertexInQuad == 4) cornerIndex = 2;
    else                                             cornerIndex = 3;

    vec2 screenPos;
    vec4 clipPos;

    switch (cornerIndex)
    {
    case 0:
        screenPos = screenA - dir * capExtend - perp * halfWidth;
        clipPos = clipA;
        break;
    case 1:
        screenPos = screenA - dir * capExtend + perp * halfWidth;
        clipPos = clipA;
        break;
    case 2:
        screenPos = screenB + dir * capExtend + perp * halfWidth;
        clipPos = clipB;
        break;
    case 3:
        screenPos = screenB + dir * capExtend - perp * halfWidth;
        clipPos = clipB;
        break;
    }

    vec2 ndc = (screenPos / viewport) * 2.0 - 1.0;
    gl_Position = vec4(ndc * clipPos.w, clipPos.z, clipPos.w);
}
