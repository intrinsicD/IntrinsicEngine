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
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
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
        vec3 ref = (abs(N.y) < 0.99) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        vec3 T = normalize(cross(N, ref));
        vec3 B = cross(N, T);

        // Transform to view space.
        vec4 viewPos4 = camera.view * vec4(worldPos, 1.0);
        vec3 viewPos = viewPos4.xyz;
        mat3 viewRot = mat3(camera.view);
        vec3 viewT = viewRot * (T * radiusWorld);
        vec3 viewB = viewRot * (B * radiusWorld);

        // Perspective Jacobian.
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

        // Screen-space covariance with low-pass filter.
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
        float invDet = 1.0 / max(det, 1e-6);
        float ci00 =  c11 * invDet;
        float ci01 = -c01 * invDet;
        float ci11 =  c00 * invDet;

        // Billboard extent: 3-sigma cutoff.
        const float CUTOFF = 3.0;
        float extX = min(CUTOFF * sqrt(max(c00, 0.0)), push.ViewportWidth * 0.5);
        float extY = min(CUTOFF * sqrt(max(c11, 0.0)), push.ViewportHeight * 0.5);

        // UV-scaled inverse covariance.
        fragEwaCovInv = vec3(
            ci00 * extX * extX,
            ci01 * extX * extY,
            ci11 * extY * extY
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
        vec3 ref = (abs(N.y) < 0.99) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        vec3 T = normalize(cross(N, ref));
        vec3 B = cross(N, T);

        vec3 offset = (T * localOffset.x + B * localOffset.y) * radiusWorld;
        vec3 expandedPos = worldPos + offset;

        fragNormal = N;
        fragWorldPos = expandedPos;
        gl_Position = camera.proj * camera.view * vec4(expandedPos, 1.0);
    }
}
