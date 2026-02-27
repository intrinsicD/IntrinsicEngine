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
    uint     RenderMode;        // 0 = FlatDisc, 1 = Surfel
    uint     Color;             // packed ABGR (uniform color)
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

    if (push.RenderMode == 1u)
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
