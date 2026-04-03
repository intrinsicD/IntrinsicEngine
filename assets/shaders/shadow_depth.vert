#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
    vec4 lightDirAndIntensity;
    vec4 lightColor;
    vec4 ambientColorAndIntensity;
    mat4 shadowCascadeMatrices[4];
    vec4 shadowCascadeSplitsAndCount;
    vec4 shadowBiasAndFilter;
} camera;

layout(buffer_reference, scalar) readonly buffer PosBuf { vec3 v[]; };

layout(push_constant) uniform ShadowPushConsts {
    mat4 model;
    uint64_t ptrPositions;
    uint cascadeIndex;
    uint _pad;
} push;

void main()
{
    PosBuf pBuf = PosBuf(push.ptrPositions);
    vec3 pos = pBuf.v[gl_VertexIndex];

    const uint cascade = min(push.cascadeIndex, 3u);
    gl_Position = camera.shadowCascadeMatrices[cascade] * push.model * vec4(pos, 1.0);
}
