#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "common.glsl"

// Fullscreen triangle; vNdc interpolates to the covered pixel's NDC position.
const vec2 POSITIONS[3] = vec2[3](
    vec2(-1.0, -1.0),
    vec2(3.0, -1.0),
    vec2(-1.0, 3.0));

layout(location = 0) out vec2 vNdc;

void main()
{
    vNdc = POSITIONS[gl_VertexIndex];
    gl_Position = vec4(vNdc, 0.0, 1.0);
}
