#version 450

// Bloom upsample pass — 9-tap tent filter with additive composition.
// Reads a coarser bloom mip via tent filter and adds the current
// (finer) downsample mip to accumulate the bloom pyramid.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uTextures[];

layout(push_constant) uniform Push
{
    vec2  InvCoarserResolution;  // 1.0 / coarser mip dimensions
    float FilterRadius;          // Tent filter radius multiplier (default 1.0)
    float _pad0;
} pc;

void main()
{
    vec2 uv = vUV;
    vec2 ts = pc.InvCoarserResolution * pc.FilterRadius;

    // 9-tap tent filter on the coarser mip (3x3 weighted)
    //   1  2  1
    //   2  4  2
    //   1  2  1
    // Total weight = 16
    vec3 result = vec3(0.0);
    result += texture(uTextures[0], uv + vec2(-1.0, -1.0) * ts).rgb * 1.0;
    result += texture(uTextures[0], uv + vec2( 0.0, -1.0) * ts).rgb * 2.0;
    result += texture(uTextures[0], uv + vec2( 1.0, -1.0) * ts).rgb * 1.0;
    result += texture(uTextures[0], uv + vec2(-1.0,  0.0) * ts).rgb * 2.0;
    result += texture(uTextures[0], uv                          ).rgb * 4.0;
    result += texture(uTextures[0], uv + vec2( 1.0,  0.0) * ts).rgb * 2.0;
    result += texture(uTextures[0], uv + vec2(-1.0,  1.0) * ts).rgb * 1.0;
    result += texture(uTextures[0], uv + vec2( 0.0,  1.0) * ts).rgb * 2.0;
    result += texture(uTextures[0], uv + vec2( 1.0,  1.0) * ts).rgb * 1.0;
    result /= 16.0;

    // Accumulate: upsampled coarser mip + current downsample level
    vec3 current = texture(uTextures[0], uv).rgb;
    outColor = vec4(current + result, 1.0);
}
