#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "common.glsl"

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(0.98, 0.96, 0.30, 1.0);
}
