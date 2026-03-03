// point_flatdisc.vert — Camera-facing billboard point rendering via BDA.
//
// FlatDisc mode: each point is expanded into a camera-facing billboard quad
// (2 triangles, 6 vertices) with constant world-space radius.
//
// Part of the PointPass pipeline array (PLAN.md Phase 4).

#version 460
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
} camera;

layout(buffer_reference, scalar) readonly buffer PosBuf  { vec3 v[]; };
layout(buffer_reference, scalar) readonly buffer AuxBuf  { uint v[]; };

layout(push_constant) uniform PushConsts {
    mat4     Model;
    uint64_t PtrPositions;
    uint64_t PtrNormals;
    uint64_t PtrAux;            // per-point packed ABGR colors (0 = uniform Color)
    float    PointSize;         // world-space radius
    float    SizeMultiplier;
    float    ViewportWidth;
    float    ViewportHeight;
    uint     Color;             // packed ABGR (uniform color)
    uint     Flags;             // bit 0: has per-point colors
    uint     _pad0;
    uint     _pad1;
} push;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragDiscUV;

void main()
{
    uint pointIndex = gl_VertexIndex / 6;
    uint vertexInQuad = gl_VertexIndex % 6;

    // Read position via BDA.
    PosBuf posBuf = PosBuf(push.PtrPositions);
    vec3 localPos = posBuf.v[pointIndex];
    vec3 worldPos = vec3(push.Model * vec4(localPos, 1.0));

    // Resolve color: per-point aux or uniform.
    if (push.PtrAux != 0ul && (push.Flags & 1u) != 0u)
    {
        AuxBuf auxBuf = AuxBuf(push.PtrAux);
        fragColor = unpackUnorm4x8(auxBuf.v[pointIndex]);
    }
    else
    {
        fragColor = unpackUnorm4x8(push.Color);
    }

    // Quad vertex layout.
    uint cornerIdx = uint[](0, 1, 2, 0, 2, 3)[vertexInQuad];
    vec2 localOffset = vec2[](vec2(-1,-1), vec2(1,-1), vec2(1,1), vec2(-1,1))[cornerIdx];
    fragDiscUV = localOffset;

    float radiusWorld = push.PointSize * push.SizeMultiplier;

    // FlatDisc mode: camera-facing billboard.
    vec4 viewPos = camera.view * vec4(worldPos, 1.0);
    vec3 cornerView = viewPos.xyz + vec3(localOffset.x, localOffset.y, 0.0) * radiusWorld;

    gl_Position = camera.proj * vec4(cornerView, 1.0);
}
