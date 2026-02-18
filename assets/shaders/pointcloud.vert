// pointcloud.vert — Point cloud billboard/surfel rendering via vertex-shader expansion.
//
// Technique: Each point is expanded into a screen-space quad (2 triangles,
// 6 vertices) in the vertex shader. Three rendering modes are supported:
//
//   Mode 0 (Flat Disc):
//     Screen-aligned billboard with constant pixel radius. Fast baseline.
//
//   Mode 1 (Surfel / Oriented Disc):
//     World-space oriented disc aligned to the surface normal. Produces
//     perspective-correct ellipses that close holes at grazing angles.
//     Based on Botsch et al., "Point-Based Surface Representation" (2005).
//
//   Mode 2 (EWA Splatting):
//     Elliptical Weighted Average splatting (Zwicker et al., "Surface
//     Splatting", SIGGRAPH 2001). Uses the Jacobian of the projective
//     mapping to transform the world-space reconstruction kernel into
//     screen space, producing anisotropic, perspective-correct splats
//     that minimize aliasing at all viewing angles.
//
// GPU data layout: each point is 32 bytes (2 x vec4):
//   struct PointData {
//       vec3  Position;    // 12 bytes
//       float Size;        //  4 bytes (world-space radius)
//       vec3  Normal;      // 12 bytes
//       uint  Color;       //  4 bytes (packed RGBA8)
//   };
//
// Architecture:
//   - Reads from SSBO (set 1, binding 0), same pattern as LineRenderPass.
//   - Camera UBO at set 0, binding 0 (shared global descriptor).
//   - Push constants for viewport dimensions, global size multiplier, and render mode.
//   - No geometry shader needed — all expansion is vertex-shader-only.
//
// References:
//   - Zwicker et al., "Surface Splatting" (SIGGRAPH 2001)
//   - Botsch et al., "Point-Based Surface Representation" (IEEE CG&A 2005)
//   - Ren, Pfister & Zwicker, "Object Space EWA Surface Splatting" (EG 2002)
//   - Gross & Pfister, "Point-Based Graphics" (Morgan Kaufmann 2007)

#version 460
#extension GL_EXT_scalar_block_layout : require

// ---- Descriptor Bindings ----

// Set 0: Global camera UBO (shared across all passes).
layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
} camera;

// Set 1: Point cloud SSBO.
struct PointData {
    vec3  Position;
    float Size;         // world-space radius
    vec3  Normal;
    uint  Color;        // packed RGBA8 (R in low bits)
};

layout(std430, set = 1, binding = 0) readonly buffer PointBuffer {
    PointData points[];
} pointCloud;

// ---- Push Constants ----
layout(push_constant) uniform PushConsts {
    float SizeMultiplier;   // Global point size multiplier.
    float ViewportWidth;
    float ViewportHeight;
    uint  RenderMode;       // 0 = flat disc, 1 = surfel, 2 = EWA
} push;

// ---- Outputs ----
layout(location = 0) out vec4  fragColor;
layout(location = 1) out vec2  fragUV;           // [-1,+1] within quad
layout(location = 2) flat out uint fragMode;     // rendering mode for fragment shader
layout(location = 3) out vec3  fragNormalWorld;  // world-space normal (for surfel lighting)
layout(location = 4) out vec3  fragPosWorld;     // world-space position

void main()
{
    // Each point uses 6 vertices (2 triangles forming a quad).
    uint pointIndex = gl_VertexIndex / 6;
    uint vertexInQuad = gl_VertexIndex % 6;

    PointData pt = pointCloud.points[pointIndex];

    // Unpack color.
    fragColor = unpackUnorm4x8(pt.Color);
    fragMode = push.RenderMode;
    fragNormalWorld = pt.Normal;
    fragPosWorld = pt.Position;

    // World-space radius with global multiplier.
    float radius = pt.Size * push.SizeMultiplier;

    // View-space position.
    vec4 viewPos = camera.view * vec4(pt.Position, 1.0);

    // Clip-space position.
    vec4 clipPos = camera.proj * viewPos;

    // Behind camera — collapse quad.
    if (clipPos.w <= 0.0)
    {
        gl_Position = vec4(0.0, 0.0, -2.0, 1.0);
        fragUV = vec2(0.0);
        return;
    }

    // Map 6-vertex quad corners.
    // Triangle 1: 0,1,2 → corners 0,1,2
    // Triangle 2: 3,4,5 → corners 0,2,3
    uint cornerIndex;
    if      (vertexInQuad == 0 || vertexInQuad == 3) cornerIndex = 0;
    else if (vertexInQuad == 1)                      cornerIndex = 1;
    else if (vertexInQuad == 2 || vertexInQuad == 4) cornerIndex = 2;
    else                                             cornerIndex = 3;

    // Corner offsets in local 2D space: [-1,-1], [+1,-1], [+1,+1], [-1,+1].
    vec2 offsets[4] = vec2[4](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0,  1.0)
    );
    vec2 localOffset = offsets[cornerIndex];
    fragUV = localOffset;

    // ------------------------------------------------------------------
    // Mode 0: Flat Disc — screen-aligned billboard, constant pixel radius
    // ------------------------------------------------------------------
    if (push.RenderMode == 0)
    {
        // Project center to screen space.
        vec2 ndc = clipPos.xy / clipPos.w;
        vec2 viewport = vec2(push.ViewportWidth, push.ViewportHeight);
        vec2 screenCenter = (ndc * 0.5 + 0.5) * viewport;

        // Compute screen-space radius from world-space radius.
        // Project a point offset by `radius` in view-space X to get pixel size.
        vec4 offsetClip = camera.proj * (viewPos + vec4(radius, 0.0, 0.0, 0.0));
        vec2 offsetNDC = offsetClip.xy / offsetClip.w;
        vec2 offsetScreen = (offsetNDC * 0.5 + 0.5) * viewport;
        float screenRadius = max(length(offsetScreen - screenCenter), 1.0); // min 1 pixel

        // Expand quad in screen space.
        vec2 screenPos = screenCenter + localOffset * (screenRadius + 1.0); // +1 for AA margin

        // Back to NDC.
        vec2 finalNDC = (screenPos / viewport) * 2.0 - 1.0;
        gl_Position = vec4(finalNDC * clipPos.w, clipPos.z, clipPos.w);
        return;
    }

    // ------------------------------------------------------------------
    // Mode 1: Surfel — oriented disc in world space
    // ------------------------------------------------------------------
    if (push.RenderMode == 1)
    {
        vec3 normal = normalize(pt.Normal);

        // Construct tangent frame from normal.
        vec3 tangent;
        if (abs(normal.y) < 0.99)
            tangent = normalize(cross(normal, vec3(0.0, 1.0, 0.0)));
        else
            tangent = normalize(cross(normal, vec3(1.0, 0.0, 0.0)));
        vec3 bitangent = cross(normal, tangent);

        // World-space disc corner.
        vec3 worldPos = pt.Position
                      + tangent * localOffset.x * radius
                      + bitangent * localOffset.y * radius;

        gl_Position = camera.proj * camera.view * vec4(worldPos, 1.0);
        return;
    }

    // ------------------------------------------------------------------
    // Mode 2: EWA Splatting — perspective-correct elliptical splats
    // ------------------------------------------------------------------
    // (Zwicker et al. 2001)
    //
    // The screen-space footprint of a world-space disc is an ellipse
    // determined by the Jacobian of the perspective projection applied
    // to the surfel tangent frame. We compute the ellipse axes and use
    // them to orient and scale the billboard quad.
    {
        vec3 normal = normalize(pt.Normal);

        // Tangent frame.
        vec3 tangent;
        if (abs(normal.y) < 0.99)
            tangent = normalize(cross(normal, vec3(0.0, 1.0, 0.0)));
        else
            tangent = normalize(cross(normal, vec3(1.0, 0.0, 0.0)));
        vec3 bitangent = cross(normal, tangent);

        // View-space tangent frame vectors (direction only, length = radius).
        vec3 viewT = mat3(camera.view) * (tangent * radius);
        vec3 viewB = mat3(camera.view) * (bitangent * radius);

        // Approximate screen-space Jacobian: project tangent/bitangent
        // endpoints to get screen-space ellipse axes.
        float projScale = camera.proj[0][0]; // Assumes symmetric projection
        float invZ = -1.0 / viewPos.z;       // Perspective divide factor

        vec2 viewport = vec2(push.ViewportWidth, push.ViewportHeight);

        // Screen-space axis vectors (in pixels).
        vec2 axisU = vec2(viewT.x * invZ, viewT.y * invZ) * projScale * viewport * 0.5;
        vec2 axisV = vec2(viewB.x * invZ, viewB.y * invZ) * projScale * viewport * 0.5;

        // To avoid degenerate splats at grazing angles, clamp minimum size.
        float axisULen = length(axisU);
        float axisVLen = length(axisV);
        float minPixels = 1.0;
        if (axisULen < minPixels) axisU = vec2(minPixels, 0.0);
        if (axisVLen < minPixels) axisV = vec2(0.0, minPixels);

        // Expand quad using ellipse axes with AA margin.
        vec2 ndc = clipPos.xy / clipPos.w;
        vec2 screenCenter = (ndc * 0.5 + 0.5) * viewport;
        vec2 screenPos = screenCenter + axisU * localOffset.x + axisV * localOffset.y;

        // Small expansion for anti-aliasing.
        vec2 expandDir = normalize(screenPos - screenCenter + vec2(0.001));
        screenPos += expandDir * 1.0;

        vec2 finalNDC = (screenPos / viewport) * 2.0 - 1.0;
        gl_Position = vec4(finalNDC * clipPos.w, clipPos.z, clipPos.w);
    }
}
