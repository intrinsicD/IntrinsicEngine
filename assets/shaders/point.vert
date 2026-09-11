// point.vert — Billboard / surfel / EWA point cloud rendering via vertex-shader expansion.
//
// Render modes (selected via push.RenderMode):
//   0 = FlatDisc:  camera-facing billboard, constant world-space radius.
//   1 = Surfel:    normal-oriented disc with Lambertian shading.
//   2 = EWA:       perspective-correct elliptical Gaussian splats (Zwicker et al. 2001).
//
// Technique: Each point is expanded into a screen-space billboard quad
// (2 triangles, 6 vertices). No geometry shader required.
//
// For surfel mode, the quad is oriented perpendicular to the surface normal
// and expanded in world space using a tangent frame derived from the normal.
// The fragment shader applies Lambertian + ambient lighting.
//
// For EWA mode, the quad is expanded in clip space to bound the 3-sigma
// elliptical Gaussian footprint. The perspective Jacobian maps the surfel's
// tangent-plane Gaussian to a screen-space ellipse.

#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require

layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
    vec4 lightDirAndIntensity;
    vec4 lightColor;
    vec4 ambientColorAndIntensity;
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
    uint  RenderMode;       // 0 = FlatDisc, 1 = Surfel, 2 = EWA
} push;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragDiscUV;
layout(location = 2) out vec3 fragNormal;       // world-space normal (surfel/EWA mode)
layout(location = 3) out vec3 fragWorldPos;     // world-space position
layout(location = 4) flat out vec3 fragEwaCovInv;  // UV-space inverse covariance (xx, xy, yy)

#include "common/point_splat.glsl"

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

    // Clamp point radius to safe world-space range [0.0001, 1.0].
    float radiusWorld = clamp(ptSize, 0.0001, 1.0) * push.SizeMultiplier;

    // Default: no EWA covariance.
    fragEwaCovInv = vec3(0.0);

    // Camera-facing fallback for degenerate or missing normals.
    vec3 cameraFwd = -vec3(camera.view[0][2], camera.view[1][2], camera.view[2][2]);

    if (push.RenderMode == 2u)
    {
        // ---- EWA Splatting mode ----
        float nLen = length(ptNormal);
        vec3 N = (nLen > 1e-6) ? (ptNormal / nLen) : cameraFwd;

        vec3 T;
        vec3 B;
        PointTangentFrame(N, T, B);

        // Transform to view space (points are in world space for SSBO path).
        vec4 viewPos4 = camera.view * vec4(ptPosition, 1.0);
        vec3 viewPos = viewPos4.xyz;
        mat3 viewRot = mat3(camera.view);
        vec3 viewT = viewRot * (T * radiusWorld);
        vec3 viewB = viewRot * (B * radiusWorld);

        vec2 scrT;
        vec2 scrB;
        ProjectPointTangents(camera.proj, vec2(push.ViewportWidth, push.ViewportHeight),
                             viewPos, viewT, viewB, scrT, scrB);

        float c00 = scrT.x * scrT.x + scrB.x * scrB.x;
        float c01 = scrT.x * scrT.y + scrB.x * scrB.y;
        float c11 = scrT.y * scrT.y + scrB.y * scrB.y;

        // Low-pass filter.
        c00 += 1.0;
        c11 += 1.0;

        float det = c00 * c11 - c01 * c01;
        vec3 covarianceInverse = PointInverseCovariance(vec3(c00, c01, c11), det);

        float extX;
        float extY;
        PointSplatExtent(c00, c11, vec2(push.ViewportWidth, push.ViewportHeight), extX, extY);

        fragEwaCovInv = vec3(
            covarianceInverse.x * extX * extX,
            covarianceInverse.y * extX * extY,
            covarianceInverse.z * extY * extY
        );

        vec4 clipCenter = camera.proj * viewPos4;
        float pixToClipX = 2.0 * clipCenter.w / push.ViewportWidth;
        float pixToClipY = 2.0 * clipCenter.w / push.ViewportHeight;

        gl_Position = clipCenter;
        gl_Position.x += localOffset.x * extX * pixToClipX;
        gl_Position.y += localOffset.y * extY * pixToClipY;

        fragNormal = N;
        fragWorldPos = ptPosition;
    }
    else if (push.RenderMode == 1u)
    {
        // ---- Surfel mode: normal-oriented disc in world space ----
        // Build a tangent frame from the surface normal.
        float nLen = length(ptNormal);
        vec3 N = (nLen > 1e-6) ? (ptNormal / nLen) : cameraFwd;

        // Choose a reference vector not parallel to N for cross product.
        vec3 T;
        vec3 B;
        PointTangentFrame(N, T, B);

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
