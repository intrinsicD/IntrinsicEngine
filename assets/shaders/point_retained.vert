// point_retained.vert — Retained-mode point cloud rendering via BDA vertex pulling.
//
// Reads positions and normals from a shared vertex buffer via buffer device
// address (BDA). This enables retained-mode rendering where vertex data is
// uploaded once to device-local memory and shared across mesh surface,
// wireframe, and point visualization.
//
// Each point is expanded into a billboard quad (2 triangles, 6 vertices).
//
// Render modes (push.RenderMode):
//   0 = FlatDisc:  camera-facing billboard, constant world-space radius.
//   1 = Surfel:    normal-oriented disc with Lambertian shading.
//   2 = EWA:       perspective-correct elliptical Gaussian splats (Zwicker et al. 2001).

#version 460
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
} camera;

layout(buffer_reference, scalar) readonly buffer PosBuf  { vec3 v[]; };
layout(buffer_reference, scalar) readonly buffer NormBuf { vec3 v[]; };

layout(push_constant) uniform PushConsts {
    mat4     Model;
    uint64_t PtrPositions;
    uint64_t PtrNormals;
    float    PointSize;         // world-space radius
    float    SizeMultiplier;
    float    ViewportWidth;
    float    ViewportHeight;
    uint     RenderMode;        // 0 = FlatDisc, 1 = Surfel, 2 = EWA
    uint     Color;             // packed ABGR (uniform color)
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
    vec3 worldNorm = vec3(0.0, 1.0, 0.0);
    if (push.PtrNormals != 0ul)
    {
        NormBuf normBuf = NormBuf(push.PtrNormals);
        vec3 localNorm = normBuf.v[pointIndex];

        // Correct normal transform under non-uniform scale / shear:
        // N_world = normalize((M^{-1})^T * N_local)
        mat3 normalMatrix = transpose(inverse(mat3(push.Model)));
        vec3 transformed = normalMatrix * localNorm;
        float nLen = length(transformed);
        worldNorm = (nLen > 1e-6) ? (transformed / nLen) : vec3(0.0, 1.0, 0.0);
    }

    fragColor = unpackUnorm4x8(push.Color);

    // Quad vertex layout.
    uint cornerIdx = uint[](0, 1, 2, 0, 2, 3)[vertexInQuad];
    vec2 localOffset = vec2[](vec2(-1,-1), vec2(1,-1), vec2(1,1), vec2(-1,1))[cornerIdx];
    fragDiscUV = localOffset;

    float radiusWorld = push.PointSize * push.SizeMultiplier;

    // Default: no EWA covariance (unused by FlatDisc/Surfel fragment paths).
    fragEwaCovInv = vec3(0.0);

    if (push.RenderMode == 2u)
    {
        // ---- EWA Splatting: perspective-correct elliptical Gaussian splats ----
        // Zwicker et al. 2001, "EWA Splatting"
        //
        // Each surfel defines a 2D Gaussian in its local tangent plane. When
        // projected to screen space via the perspective Jacobian, the circular
        // Gaussian becomes an ellipse. The billboard is sized to bound the
        // 3-sigma ellipse, and the fragment shader evaluates the Gaussian weight.

        vec3 N = worldNorm;
        vec3 ref = (abs(N.y) < 0.99) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        vec3 T = normalize(cross(N, ref));
        vec3 B = cross(N, T);

        // Transform to view space.
        vec4 viewPos4 = camera.view * vec4(worldPos, 1.0);
        vec3 viewPos = viewPos4.xyz;
        mat3 viewRot = mat3(camera.view);
        vec3 viewT = viewRot * (T * radiusWorld);  // tangent scaled by radius
        vec3 viewB = viewRot * (B * radiusWorld);  // bitangent scaled by radius

        // Perspective Jacobian: maps view-space tangent/bitangent to screen pixels.
        // NDC: ndc.x = fx * v.x / z,  ndc.y = fy * v.y / z  (z = -v.z > 0)
        // Pixels: pix = (ndc * 0.5 + 0.5) * viewport
        float z = max(-viewPos.z, 1e-4);
        float invZ2 = 1.0 / (z * z);
        float fx = camera.proj[0][0];
        float fy = camera.proj[1][1];
        float sx = 0.5 * push.ViewportWidth;
        float sy = 0.5 * push.ViewportHeight;

        // Screen-space images of the tangent and bitangent (in pixels).
        vec2 scrT = vec2(
            sx * fx * (viewT.x * z + viewT.z * viewPos.x) * invZ2,
            sy * fy * (viewT.y * z + viewT.z * viewPos.y) * invZ2
        );
        vec2 scrB = vec2(
            sx * fx * (viewB.x * z + viewB.z * viewPos.x) * invZ2,
            sy * fy * (viewB.y * z + viewB.z * viewPos.y) * invZ2
        );

        // Screen-space covariance: C = J * J^T (Jacobian columns = scrT, scrB).
        float c00 = scrT.x * scrT.x + scrB.x * scrB.x;
        float c01 = scrT.x * scrT.y + scrB.x * scrB.y;
        float c11 = scrT.y * scrT.y + scrB.y * scrB.y;

        // Low-pass filter: add 1 pixel^2 variance to prevent aliasing
        // when the projected splat is smaller than a pixel.
        c00 += 1.0;
        c11 += 1.0;

        // Invert the covariance.
        float det = c00 * c11 - c01 * c01;
        float invDet = 1.0 / max(det, 1e-6);
        float ci00 =  c11 * invDet;
        float ci01 = -c01 * invDet;
        float ci11 =  c00 * invDet;

        // Billboard extent: 3-sigma cutoff for the ellipse bounding box.
        const float CUTOFF = 3.0;
        float extX = CUTOFF * sqrt(max(c00, 0.0));
        float extY = CUTOFF * sqrt(max(c11, 0.0));

        // Clamp to prevent extremely large billboards.
        extX = min(extX, push.ViewportWidth * 0.5);
        extY = min(extY, push.ViewportHeight * 0.5);

        // UV-scaled inverse covariance so that the fragment shader evaluates
        // exp(-0.5 * uv^T * Q * uv) where uv = fragDiscUV in [-1,1].
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
        fragWorldPos = worldPos;
    }
    else if (push.RenderMode == 1u)
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
    else
    {
        // FlatDisc mode: camera-facing billboard.
        vec4 viewPos = camera.view * vec4(worldPos, 1.0);
        vec3 cornerView = viewPos.xyz + vec3(localOffset.x, localOffset.y, 0.0) * radiusWorld;

        fragNormal = vec3(0.0, 0.0, 1.0);
        fragWorldPos = worldPos;
        gl_Position = camera.proj * vec4(cornerView, 1.0);
    }
}
