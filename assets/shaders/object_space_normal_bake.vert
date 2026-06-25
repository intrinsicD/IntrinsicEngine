#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(buffer_reference, scalar) readonly buffer TexcoordBuffer { vec2 uv[]; };
layout(buffer_reference, scalar) readonly buffer NormalBuffer { vec3 normal[]; };

layout(push_constant) uniform PushConstants {
    uint64_t TexcoordBDA;
    uint64_t NormalBDA;
} push;

layout(location = 0) out vec3 fragObjectNormal;

void main()
{
    TexcoordBuffer texcoords = TexcoordBuffer(push.TexcoordBDA);
    NormalBuffer normals = NormalBuffer(push.NormalBDA);

    vec2 uv = texcoords.uv[gl_VertexIndex];
    fragObjectNormal = normals.normal[gl_VertexIndex];

    vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
