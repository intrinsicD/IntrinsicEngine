// line.vert — Screen-space thick line rendering via vertex-shader expansion.
//
// Technique: Each line segment (2 endpoints) is expanded into a screen-space
// quad (2 triangles, 6 vertices). The vertex shader generates all vertex
// positions procedurally from the SSBO line data using gl_VertexIndex.
//
// This avoids geometry shaders (slow, poorly supported on some mobile/tile-based
// GPUs) and avoids wide-line extensions (not guaranteed in Vulkan).
//
// References:
//   - Rougier, "Antialiased 2D Grid, Marker, and Arrow Shaders" (JCGT 2014)
//   - Cozzi & Ring, "3D Engine Design for Virtual Globes" (CRC Press 2011)
//   - Three.js LineMaterial / Cesium polyline rendering

#version 460
#extension GL_EXT_scalar_block_layout : require

// Camera UBO (shared across all passes)
layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
} camera;

// Line segment SSBO: one entry per line segment.
struct LineSegment {
    vec3 Start;
    uint ColorStart;
    vec3 End;
    uint ColorEnd;
};

layout(std430, set = 1, binding = 0) readonly buffer LineBuffer {
    LineSegment segments[];
} lines;

// Push constants: line width and viewport dimensions.
layout(push_constant) uniform PushConsts {
    float LineWidth;        // in pixels (screen-space half-width)
    float ViewportWidth;
    float ViewportHeight;
    float _pad;
} push;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out float fragDistanceToCenter; // signed distance for AA

void main()
{
    // Each line segment uses 6 vertices (2 triangles).
    // Vertex layout within a segment:
    //   0,1,2 = first triangle;  3,4,5 = second triangle
    //   Positions form a quad: [start-left, start-right, end-right, start-left, end-right, end-left]
    uint segmentIndex = gl_VertexIndex / 6;
    uint vertexInQuad = gl_VertexIndex % 6;

    LineSegment seg = lines.segments[segmentIndex];

    // Project endpoints to clip space
    vec4 clipA = camera.proj * camera.view * vec4(seg.Start, 1.0);
    vec4 clipB = camera.proj * camera.view * vec4(seg.End, 1.0);

    // Convert to NDC
    vec2 ndcA = clipA.xy / clipA.w;
    vec2 ndcB = clipB.xy / clipB.w;

    // Screen-space positions (pixels)
    vec2 viewport = vec2(push.ViewportWidth, push.ViewportHeight);
    vec2 screenA = (ndcA * 0.5 + 0.5) * viewport;
    vec2 screenB = (ndcB * 0.5 + 0.5) * viewport;

    // Screen-space direction and perpendicular
    vec2 dir = screenB - screenA;
    float len = length(dir);

    // Degenerate line: collapse to zero-area quad
    if (len < 0.001)
    {
        gl_Position = clipA;
        fragColor = unpackUnorm4x8(seg.ColorStart);
        fragDistanceToCenter = 0.0;
        return;
    }

    dir /= len; // normalize
    vec2 perp = vec2(-dir.y, dir.x); // perpendicular (90° CCW)

    // Half-width in pixels (push.LineWidth is the total width)
    float halfWidth = push.LineWidth * 0.5;

    // Expand the quad: 4 corner positions in screen space
    //  0 = start - perp * halfWidth  (start-left)
    //  1 = start + perp * halfWidth  (start-right)
    //  2 = end   + perp * halfWidth  (end-right)
    //  3 = end   - perp * halfWidth  (end-left)
    //
    // With small extension along direction for round caps:
    float capExtend = halfWidth; // extend by half-width for round/square caps

    vec2 screenPos;
    float side; // -1 = left, +1 = right (for distance-to-center)
    vec4 clipPos;
    vec4 color;

    // Map the 6 vertices to quad corners
    //  Triangle 1: 0, 1, 2  → start-left, start-right, end-right
    //  Triangle 2: 3, 4, 5  → start-left, end-right, end-left
    // Remapped: vertex 0,3 = corner 0; vertex 1 = corner 1; vertex 2,4 = corner 2; vertex 5 = corner 3
    uint cornerIndex;
    if      (vertexInQuad == 0 || vertexInQuad == 3) cornerIndex = 0;
    else if (vertexInQuad == 1)                      cornerIndex = 1;
    else if (vertexInQuad == 2 || vertexInQuad == 4) cornerIndex = 2;
    else                                             cornerIndex = 3; // vertexInQuad == 5

    switch (cornerIndex)
    {
    case 0: // start - left
        screenPos = screenA - dir * capExtend - perp * halfWidth;
        side = -1.0;
        clipPos = clipA;
        color = unpackUnorm4x8(seg.ColorStart);
        break;
    case 1: // start + right
        screenPos = screenA - dir * capExtend + perp * halfWidth;
        side = 1.0;
        clipPos = clipA;
        color = unpackUnorm4x8(seg.ColorStart);
        break;
    case 2: // end + right
        screenPos = screenB + dir * capExtend + perp * halfWidth;
        side = 1.0;
        clipPos = clipB;
        color = unpackUnorm4x8(seg.ColorEnd);
        break;
    case 3: // end - left
        screenPos = screenB + dir * capExtend - perp * halfWidth;
        side = -1.0;
        clipPos = clipB;
        color = unpackUnorm4x8(seg.ColorEnd);
        break;
    }

    // Convert screen position back to NDC
    vec2 ndc = (screenPos / viewport) * 2.0 - 1.0;

    // Reconstruct clip-space with original depth (w)
    gl_Position = vec4(ndc * clipPos.w, clipPos.z, clipPos.w);

    fragColor = color;
    fragDistanceToCenter = side * halfWidth; // pixel distance from center line
}
