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
    uint  RenderMode;       // 0 = FlatDisc, 1 = Surfel, 2 = EWA
} push;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragDiscUV;
layout(location = 2) out vec3 fragNormal;       // world-space normal (surfel/EWA mode)
layout(location = 3) out vec3 fragWorldPos;     // world-space position
layout(location = 4) flat out vec3 fragEwaCovInv;  // UV-space inverse covariance (xx, xy, yy)

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

        vec3 ref = (abs(N.y) < 0.99) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        vec3 T = normalize(cross(N, ref));
        vec3 B = cross(N, T);

        // Transform to view space (points are in world space for SSBO path).
        vec4 viewPos4 = camera.view * vec4(ptPosition, 1.0);
        vec3 viewPos = viewPos4.xyz;
        mat3 viewRot = mat3(camera.view);
        vec3 viewT = viewRot * (T * radiusWorld);
        vec3 viewB = viewRot * (B * radiusWorld);

        float z = max(-viewPos.z, 1e-4);
        float invZ2 = 1.0 / (z * z);
        float fx = camera.proj[0][0];
        float fy = camera.proj[1][1];
        float sx = 0.5 * push.ViewportWidth;
        float sy = 0.5 * push.ViewportHeight;

        vec2 scrT = vec2(
            sx * fx * (viewT.x * z + viewT.z * viewPos.x) * invZ2,
            sy * fy * (viewT.y * z + viewT.z * viewPos.y) * invZ2
        );
        vec2 scrB = vec2(
            sx * fx * (viewB.x * z + viewB.z * viewPos.x) * invZ2,
            sy * fy * (viewB.y * z + viewB.z * viewPos.y) * invZ2
        );

        float c00 = scrT.x * scrT.x + scrB.x * scrB.x;
        float c01 = scrT.x * scrT.y + scrB.x * scrB.y;
        float c11 = scrT.y * scrT.y + scrB.y * scrB.y;

        // Low-pass filter.
        c00 += 1.0;
        c11 += 1.0;

        float det = c00 * c11 - c01 * c01;
        float invDet = 1.0 / max(det, 1e-6);
        float ci00 =  c11 * invDet;
        float ci01 = -c01 * invDet;
        float ci11 =  c00 * invDet;

        const float CUTOFF = 3.0;
        float extX = CUTOFF * sqrt(max(c00, 0.0));
        float extY = CUTOFF * sqrt(max(c11, 0.0));
        extX = min(extX, push.ViewportWidth * 0.5);
        extY = min(extY, push.ViewportHeight * 0.5);

        fragEwaCovInv = vec3(
            ci00 * extX * extX,
            ci01 * extX * extY,
            ci11 * extY * extY
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
