// point.vert — Billboard / surfel point cloud rendering via vertex-shader expansion.
//
// Render modes (selected via push.RenderMode):
//   0 = FlatDisc:  camera-facing billboard, constant world-space radius.
//   1 = Surfel:    normal-oriented disc with Lambertian shading.
//
// Technique: Each point is expanded into a screen-space billboard quad
// (2 triangles, 6 vertices). No geometry shader required.
//
// For surfel mode, the quad is oriented perpendicular to the surface normal
// and expanded in world space using a tangent frame derived from the normal.
// The fragment shader applies Lambertian + ambient lighting.

#version 460
#extension GL_EXT_scalar_block_layout : require

layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
} camera;

struct PointData {
    vec4  PosSize;      // .xyz = Position, .w = Size (world-space radius)
    vec4  NormCol;      // .xyz = Normal,   .w = bits of Color (packed ABGR)
};

layout(std430, set = 1, binding = 0) readonly buffer PointBuffer {
    PointData points[];
} pointCloud;

layout(push_constant) uniform PushConsts {
    float SizeMultiplier;
    float ViewportWidth;
    float ViewportHeight;
    uint  RenderMode;       // 0 = FlatDisc, 1 = Surfel
} push;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragDiscUV;
layout(location = 2) out vec3 fragNormal;       // world-space normal (surfel mode)
layout(location = 3) out vec3 fragWorldPos;     // world-space position (surfel mode)

void main()
{
    uint pointIndex = gl_VertexIndex / 6;
    uint vertexInQuad = gl_VertexIndex % 6;

    PointData ptData = pointCloud.points[pointIndex];
    vec3 ptPosition = ptData.PosSize.xyz;
    float ptSize = ptData.PosSize.w;
    vec3 ptNormal = ptData.NormCol.xyz;
    uint ptColor = floatBitsToUint(ptData.NormCol.w);

    fragColor = unpackUnorm4x8(ptColor);

    // Quad vertex layout: 2 triangles (6 vertices) forming a [-1,1]x[-1,1] quad.
    uint cornerIdx = uint[](0, 1, 2, 0, 2, 3)[vertexInQuad];
    vec2 localOffset = vec2[](vec2(-1,-1), vec2(1,-1), vec2(1,1), vec2(-1,1))[cornerIdx];
    fragDiscUV = localOffset;

    float radiusWorld = ptSize * push.SizeMultiplier;

    if (push.RenderMode == 1u)
    {
        // ---- Surfel mode: normal-oriented disc in world space ----
        // Build a tangent frame from the surface normal.
        float nLen = length(ptNormal);
        vec3 N = (nLen > 1e-6) ? (ptNormal / nLen) : vec3(0.0, 1.0, 0.0);

        // Choose a reference vector not parallel to N for cross product.
        vec3 ref = (abs(N.y) < 0.99) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        vec3 T = normalize(cross(N, ref));
        vec3 B = cross(N, T);

        // Expand quad in the tangent plane.
        vec3 worldOffset = (T * localOffset.x + B * localOffset.y) * radiusWorld;
        vec3 worldPos = ptPosition + worldOffset;

        fragNormal = N;
        fragWorldPos = worldPos;

        gl_Position = camera.proj * camera.view * vec4(worldPos, 1.0);
    }
    else
    {
        // ---- FlatDisc mode: camera-facing billboard in view space ----
        vec4 viewPos = camera.view * vec4(ptPosition, 1.0);
        vec3 cornerView = viewPos.xyz + vec3(localOffset.x, localOffset.y, 0.0) * radiusWorld;

        fragNormal = vec3(0.0, 0.0, 1.0); // view-facing (unused in flat mode)
        fragWorldPos = ptPosition;

        gl_Position = camera.proj * vec4(cornerView, 1.0);
    }
}
