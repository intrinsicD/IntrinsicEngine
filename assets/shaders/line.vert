// line.vert — Unified thick line rendering via BDA vertex pulling.
//
// Both positions and edge indices are read via buffer device address (BDA).
// Supports both retained-mode (persistent device-local edge buffers uploaded
// once) and transient (per-frame DebugDraw line uploads).
//
// Each line segment is expanded into a screen-space quad (2 triangles, 6 vertices)
// for thick-line rendering with anti-aliased edges.
//
// Integration:
//   - Push constants carry BDA pointers to position + edge buffers + line config.
//   - Set 0: Camera UBO (shared across all passes).
//   - Per-entity transform via push constants (model matrix).

#version 460
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// Camera UBO (shared across all passes).
layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
    vec4 lightDirAndIntensity;
    vec4 lightColor;
    vec4 ambientColorAndIntensity;
} camera;

// Buffer device address references for shared position and edge buffers.
layout(buffer_reference, scalar) readonly buffer PosBuf { vec3 v[]; };

struct EdgePair {
    uint i0;
    uint i1;
};
layout(buffer_reference, scalar) readonly buffer EdgeBuf { EdgePair e[]; };

// Per-edge color attribute buffer (optional BDA — when PtrEdgeAttr != 0).
layout(buffer_reference, scalar) readonly buffer EdgeAttrBuf { uint color[]; };

// Push constants.
layout(push_constant) uniform PushConsts {
    mat4     Model;           // per-entity world transform
    uint64_t PtrPositions;    // BDA to shared position buffer
    uint64_t PtrEdges;        // BDA to persistent edge pair buffer
    float    LineWidth;       // in pixels
    float    ViewportWidth;
    float    ViewportHeight;
    uint     Color;           // packed ABGR (uniform color for all edges)
    uint64_t PtrEdgeAttr;      // BDA to per-edge packed ABGR colors (0 = use uniform Color)
} push;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out float fragDistanceToCenter;

void main()
{
    uint segmentIndex = gl_VertexIndex / 6;
    uint vertexInQuad = gl_VertexIndex % 6;

    // Read edge indices via BDA from the persistent edge buffer.
    EdgeBuf eBuf = EdgeBuf(push.PtrEdges);
    EdgePair edge = eBuf.e[segmentIndex];

    // Read positions via BDA from the shared vertex buffer.
    PosBuf posBuf = PosBuf(push.PtrPositions);
    vec3 posA = posBuf.v[edge.i0];

    vec3 posB = posBuf.v[edge.i1];

    // Transform to world space.
    vec3 worldA = vec3(push.Model * vec4(posA, 1.0));
    vec3 worldB = vec3(push.Model * vec4(posB, 1.0));

    // Project endpoints to clip space.
    vec4 clipA = camera.proj * camera.view * vec4(worldA, 1.0);
    vec4 clipB = camera.proj * camera.view * vec4(worldB, 1.0);

    // Convert to NDC.
    vec2 ndcA = clipA.xy / clipA.w;
    vec2 ndcB = clipB.xy / clipB.w;

    // Screen-space positions (pixels).
    vec2 viewport = vec2(push.ViewportWidth, push.ViewportHeight);
    vec2 screenA = (ndcA * 0.5 + 0.5) * viewport;
    vec2 screenB = (ndcB * 0.5 + 0.5) * viewport;

    // Screen-space direction and perpendicular.
    vec2 dir = screenB - screenA;
    float len = length(dir);

    // Per-edge color: read from BDA buffer when PtrEdgeAttr != 0, otherwise use uniform Color.
    if (push.PtrEdgeAttr != 0ul)
    {
        EdgeAttrBuf attrBuf = EdgeAttrBuf(push.PtrEdgeAttr);
        fragColor = unpackUnorm4x8(attrBuf.color[segmentIndex]);
    }
    else
    {
        fragColor = unpackUnorm4x8(push.Color);
    }

    if (len < 0.001)
    {
        gl_Position = clipA;
        fragDistanceToCenter = 0.0;
        return;
    }

    dir /= len;
    vec2 perp = vec2(-dir.y, dir.x);

    // Clamp line width to safe pixel range [0.5, 32.0].
    float clampedWidth = clamp(push.LineWidth, 0.5, 32.0);
    float halfWidth = clampedWidth * 0.5;
    float capExtend = halfWidth;

    // Map 6 vertices to 4 quad corners.
    uint cornerIndex;
    if      (vertexInQuad == 0 || vertexInQuad == 3) cornerIndex = 0;
    else if (vertexInQuad == 1)                      cornerIndex = 1;
    else if (vertexInQuad == 2 || vertexInQuad == 4) cornerIndex = 2;
    else                                             cornerIndex = 3;

    vec2 screenPos;
    float side;
    vec4 clipPos;

    switch (cornerIndex)
    {
    case 0:
        screenPos = screenA - dir * capExtend - perp * halfWidth;
        side = -1.0;
        clipPos = clipA;
        break;
    case 1:
        screenPos = screenA - dir * capExtend + perp * halfWidth;
        side = 1.0;
        clipPos = clipA;
        break;
    case 2:
        screenPos = screenB + dir * capExtend + perp * halfWidth;
        side = 1.0;
        clipPos = clipB;
        break;
    case 3:
        screenPos = screenB + dir * capExtend - perp * halfWidth;
        side = -1.0;
        clipPos = clipB;
        break;
    }

    vec2 ndc = (screenPos / viewport) * 2.0 - 1.0;
    gl_Position = vec4(ndc * clipPos.w, clipPos.z, clipPos.w);
    fragDistanceToCenter = side * halfWidth;
}
