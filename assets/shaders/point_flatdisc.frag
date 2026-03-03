// point_flatdisc.frag — FlatDisc point cloud fragment shader.
//
// Unlit circular disc with anti-aliased edge.
// Part of the PointPass pipeline array (PLAN.md Phase 4).

#version 460
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragDiscUV;

layout(location = 0) out vec4 outColor;

void main()
{
    // Disc test: discard fragments outside the unit circle.
    float r2 = dot(fragDiscUV, fragDiscUV);
    if (r2 > 1.0) discard;

    // Anti-aliased edge via smoothstep.
    float alpha = 1.0 - smoothstep(0.85, 1.0, sqrt(r2));

    outColor = vec4(fragColor.rgb, fragColor.a * alpha);
}
