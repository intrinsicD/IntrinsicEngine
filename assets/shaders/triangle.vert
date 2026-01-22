//triangle.vert
#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// Set 0, Binding 0 is Camera UBO
layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
} camera;

layout(buffer_reference, scalar) readonly buffer PosBuf  { vec3 v[]; };
layout(buffer_reference, scalar) readonly buffer NormBuf { vec3 v[]; };
layout(buffer_reference, scalar) readonly buffer AuxBuf  { vec4 v[]; };

layout(push_constant) uniform PushConsts {
    mat4 model;
    uint64_t ptrPos;
    uint64_t ptrNorm;
    uint64_t ptrAux;
    uint textureID;
} push;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;

void main() {
    PosBuf  pBuf = PosBuf(push.ptrPos);
    NormBuf nBuf = NormBuf(push.ptrNorm);
    AuxBuf  aBuf = AuxBuf(push.ptrAux);

    // Read SoA
    vec3 inPos = pBuf.v[gl_VertexIndex];
    vec3 inNorm = nBuf.v[gl_VertexIndex];
    vec2 inUV = aBuf.v[gl_VertexIndex].xy;

    gl_Position = camera.proj * camera.view * push.model * vec4(inPos, 1.0);
    fragNormal = mat3(push.model) * inNorm;
    fragTexCoord = inUV;
}