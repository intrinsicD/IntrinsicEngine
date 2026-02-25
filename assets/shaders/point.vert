#version 460
#extension GL_EXT_scalar_block_layout : require


layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
} camera;

struct PointData {
    vec4  PosSize;      // .xyz = Position, .w = Size
    vec4  NormCol;      // .xyz = Normal,   .w = bits of Color
};

layout(std430, set = 1, binding = 0) readonly buffer PointBuffer {
    PointData points[];
} pointCloud;

layout(push_constant) uniform PushConsts {
    float SizeMultiplier;
    float ViewportWidth;
    float ViewportHeight;
    uint  RenderMode;
} push;

layout(location = 0) out vec4  fragColor;
layout(location = 1) out vec2  fragUV;
layout(location = 2) flat out uint fragMode;
layout(location = 3) out vec3  fragNormalWorld;
layout(location = 4) out vec3  fragPosWorld;
layout(location = 5) out vec4  fragConic;
layout(location = 6) out vec2  fragPixelOffset;
layout(location = 7) out vec2  fragDiscUV;
layout(location = 8) out vec2  fragScreenCenterPx;

void main()
{
    uint pointIndex = gl_VertexIndex / 6;
    uint vertexInQuad = gl_VertexIndex % 6;

    PointData ptData = pointCloud.points[pointIndex];
    vec3 ptPosition = ptData.PosSize.xyz;
    float ptSize    = ptData.PosSize.w;
    vec3 ptNormal   = ptData.NormCol.xyz;
    uint ptColor    = floatBitsToUint(ptData.NormCol.w);

    fragColor = unpackUnorm4x8(ptColor);
    fragMode = push.RenderMode;
    fragPosWorld = ptPosition;

    // Normalize world normal (used for lighting in all modes).
    float n2 = dot(ptNormal, ptNormal);
    vec3 normalWorld = (n2 > 1e-12) ? (ptNormal * inversesqrt(n2)) : vec3(0.0, 0.0, 1.0);
    fragNormalWorld = normalWorld;

    float radiusWorld = ptSize * push.SizeMultiplier;
    vec4 viewPos = camera.view * vec4(ptPosition, 1.0);
    vec4 clipPos = camera.proj * viewPos;

    if (clipPos.w <= 0.0) {
        gl_Position = vec4(0.0, 0.0, -2.0, 1.0);
        return;
    }

    // Corner Logic
    uint cornerIdx = uint[](0, 1, 2, 0, 2, 3)[vertexInQuad];
    vec2 localOffset = vec2[](vec2(-1,-1), vec2(1,-1), vec2(1,1), vec2(-1,1))[cornerIdx];
    fragUV = localOffset;
    fragDiscUV = localOffset;

    // Viewport / projected center (needed by mode 2, safe to set for all).
    vec2 viewport  = vec2(push.ViewportWidth, push.ViewportHeight);
    vec2 ndcCenter = clipPos.xy / clipPos.w;
    vec2 screenCenterPx = (ndcCenter * 0.5 + 0.5) * viewport;
    fragScreenCenterPx = screenCenterPx;

    // --- Mode 0: Screen-aligned billboard ---
    // The quad always faces the camera: expand along view-space X and Y axes.
    if (push.RenderMode == 0) {
        vec3 cornerView = viewPos.xyz
            + vec3(localOffset.x, localOffset.y, 0.0) * radiusWorld;
        gl_Position = camera.proj * vec4(cornerView, 1.0);
        return;
    }

    // --- Surface-aligned tangent frame (shared by Mode 1 & 2) ---
    // camera.view is a world-to-view rigid transform; mat3 extracts the rotation.
    mat3 viewRot = mat3(camera.view);
    vec3 normalView = normalize(viewRot * normalWorld);

    // Screen-stable tangent frame: project view-space X onto the surfel plane, then
    // fall back to view-space Y if the normal is nearly aligned with view-X.
    // This guarantees Tview/Bview are always perpendicular to normalView, and ensures
    // the tangent has maximum screen-space coverage (never collapses along view-Z).
    // Note: the view-stable (view-direction-projected) tangent degenerates to view-Z
    // when the surfel normal is perpendicular to the view direction (e.g. a horizontal
    // surfel viewed from the side), causing Mode 1 to appear as a thin sliver and Mode 2
    // to produce a blown-up/invisible bounding quad.
    vec3 screenRef = (abs(normalView.x) < 0.9) ? vec3(1, 0, 0) : vec3(0, 1, 0);
    vec3 Tview = normalize(screenRef - normalView * dot(screenRef, normalView));
    vec3 Bview = cross(normalView, Tview);

    // --- Mode 1: Surfel — surface-aligned disc ---
    if (push.RenderMode == 1) {
        vec3 cornerView = viewPos.xyz + (Tview * localOffset.x + Bview * localOffset.y) * radiusWorld;
        gl_Position = camera.proj * vec4(cornerView, 1.0);
        return;
    }

    // --- Mode 3: GaussianSplat — isotropic Gaussian blob (screen-aligned, Gaussian alpha) ---
    // Same billboard as mode 0; the fragment shader applies a smooth Gaussian opacity falloff
    // instead of a hard disc discard.  Suitable for volumetric / density visualization.
    if (push.RenderMode == 3) {
        vec3 cornerView = viewPos.xyz
            + vec3(localOffset.x, localOffset.y, 0.0) * radiusWorld;
        gl_Position = camera.proj * vec4(cornerView, 1.0);
        return;
    }

    // --- Mode 2: EWA splatting — surface-aligned elliptical Gaussian splat ---
    {
        // Perspective Jacobian: maps view-space tangent directions to screen pixels.
        // Use abs(proj[1][1]) because glm::perspective with the Vulkan Y-flip sets
        // proj[1][1] = -f (negative), which would invert the Y focal length and
        // corrupt the conic for non-axis-aligned ellipses.
        float focalX = camera.proj[0][0] * (push.ViewportWidth  * 0.5);
        float focalY = abs(camera.proj[1][1]) * (push.ViewportHeight * 0.5);
        vec2  focal  = vec2(focalX, focalY);

        // Perspective Jacobian of a view-space direction d at view position p:
        //   screen_delta = focal * (d.xy / (-p.z) - d.z * p.xy / (p.z * p.z))
        // viewPos.z < 0 for visible objects; negate it for the division.
        float invNegZ  = 1.0 / (-viewPos.z);
        vec2 innerU = (Tview.xy * invNegZ) - (Tview.z * invNegZ * invNegZ) * viewPos.xy;
        vec2 innerV = (Bview.xy * invNegZ) - (Bview.z * invNegZ * invNegZ) * viewPos.xy;

        vec2 axisU_px = focal * innerU * radiusWorld;
        vec2 axisV_px = focal * innerV * radiusWorld;

        float lenU = length(axisU_px);
        float lenV = length(axisV_px);

        // Guard: if either projected axis is near-zero (surfel nearly edge-on to camera),
        // fall back to a screen-aligned isotropic Gaussian billboard.
        // This prevents planeScale from exploding to infinity and the quad from covering
        // the entire scene while producing no visible fragments.
        if (lenU < 0.5 || lenV < 0.5)
        {
            // Estimate screen-space pixel radius from the world radius.
            float pixelR  = radiusWorld * focalX * invNegZ;
            float safeR   = max(pixelR, 1.0);
            float invR2   = 1.0 / (safeR * safeR + 1.0);
            fragConic       = vec4(invR2, 0.0, 0.0, invR2);
            float extentPx  = sqrt(1.0 / invR2) * 3.0;
            fragPixelOffset = localOffset * vec2(extentPx);

            float planeScaleFallback = radiusWorld * (extentPx / max(safeR, 1e-6));
            vec3 cornerViewFallback  = viewPos.xyz + vec3(localOffset.x, localOffset.y, 0.0) * planeScaleFallback;
            gl_Position = camera.proj * vec4(cornerViewFallback, 1.0);
            return;
        }

        // Covariance matrix with 1-pixel low-pass filter (EWA anti-aliasing).
        float uu = dot(axisU_px, axisU_px) + 1.0;
        float vv = dot(axisV_px, axisV_px) + 1.0;
        float uv = dot(axisU_px, axisV_px);

        float det = max(uu * vv - uv * uv, 1e-6);
        float invDet = 1.0 / det;
        fragConic = vec4(vv * invDet, -uv * invDet, -uv * invDet, uu * invDet);

        // Conservative bounding box: 3σ extent in each principal screen axis.
        float extentU = sqrt(uu) * 3.0;
        float extentV = sqrt(vv) * 3.0;

        fragPixelOffset = localOffset * vec2(extentU, extentV);

        // Emit a tangent-plane quad sized to conservatively cover the ellipse.
        float extent = max(extentU, extentV);
        float planeScale = radiusWorld * (extent / max(max(lenU, lenV), 1e-6));

        vec3 cornerView = viewPos.xyz + (Tview * localOffset.x + Bview * localOffset.y) * planeScale;
        gl_Position = camera.proj * vec4(cornerView, 1.0);
        return;
    }
}
