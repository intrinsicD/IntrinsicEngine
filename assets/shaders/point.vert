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
    fragNormalWorld = ptNormal;
    fragPosWorld = ptPosition;

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

    // NOTE: In shaders we transform world -> view.
    // The view matrix maps view->world? In this engine, camera.view is used as proj*view*worldPos,
    // so it's a world->view transform. For normals we need its inverse-transpose rotation.
    // Since it's a rigid transform, inverse rotation = transpose(rotation).
    mat3 viewRot = mat3(camera.view);

    float n2 = dot(ptNormal, ptNormal);
    vec3 normalWorld = (n2 > 1e-12) ? (ptNormal * inversesqrt(n2)) : vec3(0.0, 0.0, 1.0);
    vec3 normalView  = normalize(viewRot * normalWorld);

    fragNormalWorld = normalWorld;

    // --- View-stable tangent frame in view space ---
    // Build T as the projection of the view direction onto the surfel plane.
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

    vec2 viewport  = vec2(push.ViewportWidth, push.ViewportHeight);
    vec2 ndcCenter = clipPos.xy / clipPos.w;
    vec2 screenCenterPx = (ndcCenter * 0.5 + 0.5) * viewport;
    fragScreenCenterPx = screenCenterPx;

    // Mode 0: Surface-aligned disc (true tilted geometry)
    if (push.RenderMode == 0) {
        vec3 cornerView = viewPos.xyz + (Tview * localOffset.x + Bview * localOffset.y) * radiusWorld;
        gl_Position = camera.proj * vec4(cornerView, 1.0);
        return;
    }

    // Mode 1: Surfel (same as 0, but shaded/filtered differently in FS)
    if (push.RenderMode == 1) {
        vec3 cornerView = viewPos.xyz + (Tview * localOffset.x + Bview * localOffset.y) * radiusWorld;
        gl_Position = camera.proj * vec4(cornerView, 1.0);
        return;
    }

    // Mode 2: Surface-aligned EWA ellipse embedded in the tangent plane.
    {
        // Project tangent basis to screen to get ellipse covariance in pixel space.
        float focalX = camera.proj[0][0] * (push.ViewportWidth * 0.5);
        float focalY = camera.proj[1][1] * (push.ViewportHeight * 0.5);

        // d(screen)/d(view) for a direction v is approx: f * (v.xy - (v.z / z) * p.xy) / z
        vec2 axisU_px = (focalX * (Tview.xy - (Tview.z / viewPos.z) * viewPos.xy)) / viewPos.z;
        vec2 axisV_px = (focalY * (Bview.xy - (Bview.z / viewPos.z) * viewPos.xy)) / viewPos.z;

        axisU_px *= radiusWorld;
        axisV_px *= radiusWorld;

        float uu = dot(axisU_px, axisU_px) + 1.0;
        float vv = dot(axisV_px, axisV_px) + 1.0;
        float uv = dot(axisU_px, axisV_px);

        float det = max(uu * vv - uv * uv, 1e-6);
        float invDet = 1.0 / det;
        fragConic = vec4(vv * invDet, -uv * invDet, -uv * invDet, uu * invDet);

        // Build a conservative bounding box in *plane space* using principal axes in screen.
        // We map localOffset in [-1,1]^2 into the plane using the same radiusWorld (keeps it stable).
        // The discard/conic in FS will handle the true ellipse.
        float extentU = sqrt(uu) * 3.0;
        float extentV = sqrt(vv) * 3.0;

        // Per-vertex pixel offset from the projected center.
        fragPixelOffset = localOffset * vec2(extentU, extentV);

        // Geometry: emit a plane-aligned quad sized to conservatively cover the ellipse.
        // Use the larger extent as a scale factor.
        float extent = max(extentU, extentV);
        float planeScale = radiusWorld * (extent / max(max(length(axisU_px), length(axisV_px)), 1e-6));

        vec3 cornerView = viewPos.xyz + (Tview * localOffset.x + Bview * localOffset.y) * planeScale;
        gl_Position = camera.proj * vec4(cornerView, 1.0);
        return;
    }
}
