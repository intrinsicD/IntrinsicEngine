#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "common.glsl"

layout(buffer_reference, scalar) readonly buffer UvViewTexcoordBuffer
{
    vec2 Values[];
};

void main()
{
    if (pc.TexcoordBDA == uint64_t(0))
    {
        // The host normally suppresses this draw. Keep an invalid request from
        // dereferencing address zero if it reaches the shader nonetheless.
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
        return;
    }

    UvViewTexcoordBuffer texcoords = UvViewTexcoordBuffer(pc.TexcoordBDA);
    const vec2 uv = texcoords.Values[uint(gl_VertexIndex)];
    const vec2 halfExtent = max(abs(pc.UvHalfExtent), vec2(1.0e-6));
    const vec2 fittedUv = (uv - pc.UvCenter) / halfExtent;

    // VulkanCommandContext's negative-height viewport already maps positive
    // clip-space Y toward the top of the target, matching the CPU UV pane.
    gl_Position = vec4(fittedUv, 0.0, 1.0);
}
