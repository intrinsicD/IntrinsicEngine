// line_retained.frag — Anti-aliased line fragment shader for retained-mode BDA lines.
//
// Push constant layout matches line_retained.vert. The fragment shader only
// reads LineWidth for the anti-aliased edge falloff calculation.

#version 460
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) in vec4 fragColor;
layout(location = 1) in float fragDistanceToCenter; // pixel distance from center

// Push constant layout must match line_retained.vert exactly.
layout(push_constant) uniform PushConsts {
    mat4     Model;
    uint64_t PtrPositions;
    uint64_t PtrEdges;
    float    LineWidth;
    float    ViewportWidth;
    float    ViewportHeight;
    uint     Color;
} push;

layout(location = 0) out vec4 outColor;

void main()
{
    float halfWidth = push.LineWidth * 0.5;

    // Signed distance from center of line, in pixels.
    float dist = abs(fragDistanceToCenter);

    // Anti-aliasing: smooth falloff over ~1.5 pixels at the edge.
    float alpha = 1.0 - smoothstep(halfWidth - 1.0, halfWidth + 0.5, dist);

    if (alpha < 0.004) discard;

    outColor = vec4(fragColor.rgb, fragColor.a * alpha);
}
