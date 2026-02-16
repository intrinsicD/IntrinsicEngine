// line.frag — Anti-aliased line fragment shader.
//
// Uses the signed distance from the fragment to the center line (interpolated
// from the vertex shader) to produce smooth anti-aliased edges via smoothstep.
// This gives sub-pixel quality at no additional geometry cost.

#version 460

layout(location = 0) in vec4 fragColor;
layout(location = 1) in float fragDistanceToCenter; // pixel distance from center

layout(push_constant) uniform PushConsts {
    float LineWidth;
    float ViewportWidth;
    float ViewportHeight;
    float _pad;
} push;

layout(location = 0) out vec4 outColor;

void main()
{
    float halfWidth = push.LineWidth * 0.5;

    // Signed distance from center of line, in pixels
    float dist = abs(fragDistanceToCenter);

    // Anti-aliasing: smooth falloff over ~1.5 pixels at the edge.
    // The AA band sits at [halfWidth - 1.0, halfWidth + 0.5].
    float alpha = 1.0 - smoothstep(halfWidth - 1.0, halfWidth + 0.5, dist);

    if (alpha < 0.004) discard; // early out for fully transparent fragments

    outColor = vec4(fragColor.rgb, fragColor.a * alpha);
}
