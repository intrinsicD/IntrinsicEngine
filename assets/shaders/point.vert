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

    // View-stable tangent: project the view direction onto the surfel plane.
    // This avoids arbitrary helper-axis selection (which can cause visible 90° flips).
    vec3 Vview = normalize(-viewPos.xyz); // camera at origin in view space
    vec3 Tview = Vview - normalView * dot(Vview, normalView);
    float t2 = dot(Tview, Tview);
    if (t2 < 1e-10)
    {
        // Degenerate when looking straight down the normal; fall back to a deterministic axis.
        vec3 a = (abs(normalView.y) < 0.9) ? vec3(0, 1, 0) : vec3(1, 0, 0);
        Tview = normalize(cross(a, normalView));
    }
    else
    {
        Tview *= inversesqrt(t2);
    }
    vec3 Bview = cross(normalView, Tview);

    // --- Mode 1: Surfel — surface-aligned disc ---
    if (push.RenderMode == 1) {
        vec3 cornerView = viewPos.xyz + (Tview * localOffset.x + Bview * localOffset.y) * radiusWorld;
        gl_Position = camera.proj * vec4(cornerView, 1.0);
        return;
    }

    // --- Mode 2: EWA splatting — surface-aligned elliptical Gaussian splat ---
    {
        // Perspective Jacobian: maps view-space tangent directions to screen pixels.
        // Each screen axis has its own focal length from the projection matrix.
        float focalX = camera.proj[0][0] * (push.ViewportWidth * 0.5);
        float focalY = camera.proj[1][1] * (push.ViewportHeight * 0.5);
        vec2  focal  = vec2(focalX, focalY);

        // d(screen)/d(view) for direction d:
        //   [focalX * (d.x - d.z * p.x / p.z) / p.z,
        //    focalY * (d.y - d.z * p.y / p.z) / p.z]
        vec2 innerU = (Tview.xy - (Tview.z / viewPos.z) * viewPos.xy) / viewPos.z;
        vec2 innerV = (Bview.xy - (Bview.z / viewPos.z) * viewPos.xy) / viewPos.z;

        vec2 axisU_px = focal * innerU * radiusWorld;
        vec2 axisV_px = focal * innerV * radiusWorld;

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
        float planeScale = radiusWorld * (extent / max(max(length(axisU_px), length(axisV_px)), 1e-6));

        vec3 cornerView = viewPos.xyz + (Tview * localOffset.x + Bview * localOffset.y) * planeScale;
        gl_Position = camera.proj * vec4(cornerView, 1.0);
        return;
    }
}
