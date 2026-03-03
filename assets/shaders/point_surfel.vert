// point_surfel.vert — Surfel point rendering: normal-oriented disc.
//
// Part of the PointPass pipeline family (PLAN.md Phase 4).
// Each point is expanded into a normal-oriented disc (2 triangles, 6 vertices).
// The disc is oriented perpendicular to the point's normal vector.
//
// Push constant layout shared across all PointPass shader variants.

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
layout(buffer_reference, scalar) readonly buffer AuxBuf  { uint v[]; };

layout(push_constant) uniform PushConsts {
    mat4     Model;
    uint64_t PtrPositions;
    uint64_t PtrNormals;
    uint64_t PtrAux;
    float    PointSize;
    float    SizeMultiplier;
    float    ViewportWidth;
    float    ViewportHeight;
    uint     Color;
    uint     Flags;
} push;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragDiscUV;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragWorldPos;

void main()
{
    uint pointIndex = gl_VertexIndex / 6;
    uint vertexInQuad = gl_VertexIndex % 6;

    // Read position via BDA.
    PosBuf posBuf = PosBuf(push.PtrPositions);
    vec3 localPos = posBuf.v[pointIndex];
    vec3 worldPos = vec3(push.Model * vec4(localPos, 1.0));

    // Read normal via BDA (fallback to up vector).
    vec3 worldNorm = vec3(0.0, 1.0, 0.0);
    if (push.PtrNormals != 0ul)
    {
        NormBuf normBuf = NormBuf(push.PtrNormals);
        vec3 localNorm = normBuf.v[pointIndex];

        // Correct normal transform under non-uniform scale / shear.
        mat3 normalMatrix = transpose(inverse(mat3(push.Model)));
        vec3 transformed = normalMatrix * localNorm;
        float nLen = length(transformed);
        worldNorm = (nLen > 1e-6) ? (transformed / nLen) : vec3(0.0, 1.0, 0.0);
    }

    // Determine color: per-point attribute or uniform.
    if ((push.Flags & 1u) != 0u && push.PtrAux != 0ul)
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
