// point_flatdisc.frag — FlatDisc point fragment shader: unlit circular disc.
//
// Part of the PointPass pipeline family (PLAN.md Phase 4).
// Discards fragments outside the unit circle with an anti-aliased edge.

#version 460

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
