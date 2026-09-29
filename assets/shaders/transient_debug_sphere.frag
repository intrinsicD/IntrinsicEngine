#version 460
// Sphere shading of transient debug points (see transient_debug_sphere.vert).
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#include "common/transient_debug_sphere.glsl"
#include "common/point_sphere_impostor.glsl"

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragViewCenter;
layout(location = 0) out vec4 outColor;

void main()
{
    vec3 color = fragColor.rgb;
    float depth;
    if (!ShadeSphereImpostor(fragUV, fragViewCenter, push.Radius, push.Projection, color, depth))
        discard;
    gl_FragDepth = depth;
    outColor = vec4(color, 1.0);
}
