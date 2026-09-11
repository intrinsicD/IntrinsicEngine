// point_surfel.vert — Normal-oriented point rendering via BDA.
//
// Surfel and EWA modes: each point is expanded into a normal-oriented disc
// or perspective-correct elliptical Gaussian splat (6 vertices).
//
// Render modes (push.Flags bits 1-2):
//   Surfel (default): normal-oriented disc with Lambertian shading.
//   EWA (Flags bit 1): perspective-correct elliptical Gaussian splats (Zwicker et al. 2001).
//
// Part of the PointPass pipeline array in the three-pass rendering architecture.

#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
    vec4 lightDirAndIntensity;
    vec4 lightColor;
    vec4 ambientColorAndIntensity;
} camera;

layout(buffer_reference, scalar) readonly buffer PosBuf   { vec3  v[]; };
layout(buffer_reference, scalar) readonly buffer NormBuf  { vec3  v[]; };
layout(buffer_reference, scalar) readonly buffer AttrBuf   { uint  v[]; };
layout(buffer_reference, scalar) readonly buffer RadiiBuf { float v[]; };

layout(push_constant) uniform PushConsts {
    mat4     Model;
    uint64_t PtrPositions;
    uint64_t PtrNormals;
    uint64_t PtrAttr;            // per-point packed ABGR colors (0 = uniform Color)
    float    PointSize;         // world-space radius
    float    SizeMultiplier;
    float    ViewportWidth;
    float    ViewportHeight;
    uint     Color;             // packed ABGR (uniform color)
    uint     Flags;             // bit 0: per-point colors, bit 1: EWA mode, bit 2: per-point radii
    uint64_t PtrRadii;          // per-point float radii (0 = uniform PointSize)
} push;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragDiscUV;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragWorldPos;
layout(location = 4) flat out vec3 fragEwaCovInv;  // UV-space inverse covariance (symmetric 2x2: xx, xy, yy)

#include "common/point_splat.glsl"

void main()
{
    uint pointIndex = gl_VertexIndex / 6;
    uint vertexInQuad = gl_VertexIndex % 6;

    // Read position via BDA.
    PosBuf posBuf = PosBuf(push.PtrPositions);
    vec3 localPos = posBuf.v[pointIndex];
    vec3 worldPos = vec3(push.Model * vec4(localPos, 1.0));

    // Read normal via BDA (if available).
    // Fallback to camera-facing basis for degenerate or missing normals.
    vec3 cameraFwd = -vec3(camera.view[0][2], camera.view[1][2], camera.view[2][2]);
    vec3 worldNorm = cameraFwd;
    if (push.PtrNormals != 0ul)
    {
        NormBuf normBuf = NormBuf(push.PtrNormals);
        vec3 localNorm = normBuf.v[pointIndex];

        // Correct normal transform under non-uniform scale / shear.
        mat3 normalMatrix = transpose(inverse(mat3(push.Model)));
        vec3 transformed = normalMatrix * localNorm;
        float nLen = length(transformed);
        worldNorm = (nLen > 1e-6) ? (transformed / nLen) : cameraFwd;
    }

    // Resolve color: per-point attr or uniform.
    if (push.PtrAttr != 0ul && (push.Flags & 1u) != 0u)
    {
        AttrBuf attrBuf = AttrBuf(push.PtrAttr);
        fragColor = unpackUnorm4x8(attrBuf.v[pointIndex]);
    }
    else
    {
        fragColor = unpackUnorm4x8(push.Color);
    }

    // Quad vertex layout.
    uint cornerIdx = uint[](0, 1, 2, 0, 2, 3)[vertexInQuad];
    vec2 localOffset = vec2[](vec2(-1,-1), vec2(1,-1), vec2(1,1), vec2(-1,1))[cornerIdx];
    fragDiscUV = localOffset;

    // Resolve point radius: per-point or uniform.
    float baseRadius = push.PointSize;
    if (push.PtrRadii != 0ul && (push.Flags & 4u) != 0u)
    {
        RadiiBuf radiiBuf = RadiiBuf(push.PtrRadii);
        baseRadius = radiiBuf.v[pointIndex];
    }
    // Clamp point radius to safe world-space range [0.0001, 1.0].
    float radiusWorld = clamp(baseRadius, 0.0001, 1.0) * push.SizeMultiplier;

    // Default: no EWA covariance.
    fragEwaCovInv = vec3(0.0);

    bool isEWA = (push.Flags & 2u) != 0u;

    if (isEWA)
    {
        // ---- EWA Splatting: perspective-correct elliptical Gaussian splats ----
        // Zwicker et al. 2001, "EWA Splatting"
        vec3 N = worldNorm;
        vec3 T;
        vec3 B;
        PointTangentFrame(N, T, B);

        // Transform to view space.
        vec4 viewPos4 = camera.view * vec4(worldPos, 1.0);
        vec3 viewPos = viewPos4.xyz;
        mat3 viewRot = mat3(camera.view);
        vec3 viewT = viewRot * (T * radiusWorld);
        vec3 viewB = viewRot * (B * radiusWorld);

        // Perspective Jacobian.
        vec2 scrT;
        vec2 scrB;
        ProjectPointTangents(camera.proj, vec2(push.ViewportWidth, push.ViewportHeight),
                             viewPos, viewT, viewB, scrT, scrB);

        float c00 = scrT.x * scrT.x + scrB.x * scrB.x + 1.0;
        float c01 = scrT.x * scrT.y + scrB.x * scrB.y;
        float c11 = scrT.y * scrT.y + scrB.y * scrB.y + 1.0;

        // Eigenvalue floor: ensure minimum splat size to avoid degenerate ellipses.
        // Eigenvalues of 2x2 symmetric: lambda = 0.5*(tr +/- sqrt(tr^2 - 4*det))
        float tr = c00 + c11;
        float det = c00 * c11 - c01 * c01;
        float disc = max(tr * tr - 4.0 * det, 0.0);
        float sqrtDisc = sqrt(disc);
        float lambdaMin = 0.5 * (tr - sqrtDisc);

        // If the smaller eigenvalue is below the floor, the covariance is
        // ill-conditioned — fall back to isotropic FlatDisc rendering.
        const float EIGENVALUE_FLOOR = 0.25; // quarter-pixel^2
        bool illConditioned = (lambdaMin < EIGENVALUE_FLOOR) || (det < 1e-6);

        if (illConditioned)
        {
            // Fallback: camera-facing billboard (FlatDisc behavior).
            fragEwaCovInv = vec3(0.0); // zero covariance → fragment shader uses disc test
            vec4 viewPosFB = camera.view * vec4(worldPos, 1.0);
            vec3 cornerView = viewPosFB.xyz + vec3(localOffset.x, localOffset.y, 0.0) * radiusWorld;
            gl_Position = camera.proj * vec4(cornerView, 1.0);
            fragNormal = N;
        }
        else
        {
        // Invert covariance.
        vec3 covarianceInverse = PointInverseCovariance(vec3(c00, c01, c11), det);

        // Billboard extent: 3-sigma cutoff.
        float extX;
        float extY;
        PointSplatExtent(c00, c11, vec2(push.ViewportWidth, push.ViewportHeight), extX, extY);

        fragEwaCovInv = vec3(
            covarianceInverse.x * extX * extX,
            covarianceInverse.y * extX * extY,
            covarianceInverse.z * extY * extY
        );

        // Expand billboard in clip space.
        vec4 clipCenter = camera.proj * viewPos4;
        float pixToClipX = 2.0 * clipCenter.w / push.ViewportWidth;
        float pixToClipY = 2.0 * clipCenter.w / push.ViewportHeight;

        gl_Position = clipCenter;
        gl_Position.x += localOffset.x * extX * pixToClipX;
        gl_Position.y += localOffset.y * extY * pixToClipY;

        fragNormal = N;
        }
        fragWorldPos = worldPos;
    }
    else
    {
        // Surfel mode: normal-oriented disc.
        vec3 N = worldNorm;
        vec3 T;
        vec3 B;
        PointTangentFrame(N, T, B);

        vec3 offset = (T * localOffset.x + B * localOffset.y) * radiusWorld;
        vec3 expandedPos = worldPos + offset;

        fragNormal = N;
        fragWorldPos = expandedPos;
        gl_Position = camera.proj * camera.view * vec4(expandedPos, 1.0);
    }
}
