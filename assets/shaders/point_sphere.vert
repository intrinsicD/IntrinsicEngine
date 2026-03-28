// point_sphere.vert — Impostor sphere rendering via BDA.
//
// Sphere mode: each point is expanded into a camera-facing billboard quad
// (2 triangles, 6 vertices). The fragment shader ray-traces a sphere against
// the billboard to compute correct gl_FragDepth and per-pixel normals.
//
// Part of the PointPass pipeline array in the three-pass rendering architecture.

#version 460
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
    uint     Flags;             // bit 0: per-point colors, bit 2: per-point radii
    uint64_t PtrRadii;          // per-point float radii (0 = uniform PointSize)
} push;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragDiscUV;
layout(location = 2) out vec3 fragViewCenter;   // view-space sphere center
layout(location = 3) out float fragRadiusView;  // view-space sphere radius

void main()
{
    uint pointIndex = gl_VertexIndex / 6;
    uint vertexInQuad = gl_VertexIndex % 6;

    // Read position via BDA.
    PosBuf posBuf = PosBuf(push.PtrPositions);
    vec3 localPos = posBuf.v[pointIndex];
    vec3 worldPos = vec3(push.Model * vec4(localPos, 1.0));

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

    // View-space sphere center and radius for fragment depth computation.
    vec4 viewPos = camera.view * vec4(worldPos, 1.0);
    fragViewCenter = viewPos.xyz;
    fragRadiusView = radiusWorld;

    // Camera-facing billboard (same as FlatDisc).
    vec3 cornerView = viewPos.xyz + vec3(localOffset.x, localOffset.y, 0.0) * radiusWorld;
    gl_Position = camera.proj * vec4(cornerView, 1.0);
}
