#version 450

// Bloom downsample pass — 13-tap box filter with bilinear sampling.
// Based on the technique from Call of Duty: Advanced Warfare (Jimenez 2014).
// Each invocation samples a 4x4 region using 5 bilinear taps arranged in a
// cross + center pattern, weighted to avoid firefly artifacts.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uInput;

layout(push_constant) uniform Push
{
    vec2  InvSrcResolution;  // 1.0 / source mip dimensions
    float Threshold;         // Brightness threshold (mip 0 only)
    int   IsFirstMip;       // 1 = apply threshold, 0 = pass-through
} pc;

// Soft threshold (knee curve) to avoid harsh cutoff artifacts.
vec3 SoftThreshold(vec3 color, float threshold)
{
    float brightness = max(color.r, max(color.g, color.b));
    float knee = threshold * 0.5; // Soft knee width
    float soft = brightness - threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-5);
    float contribution = max(soft, brightness - threshold) / max(brightness, 1e-5);
    return color * max(contribution, 0.0);
}

void main()
{
    vec2 uv = vUV;
    vec2 ts = pc.InvSrcResolution;

    // 13-tap downsample filter (Jimenez 2014)
    // Sample pattern:
    //   a . b . c
    //   . d . e .
    //   f . g . h
    //   . i . j .
    //   k . l . m

    vec3 a = texture(uInput, uv + vec2(-2.0, -2.0) * ts).rgb;
    vec3 b = texture(uInput, uv + vec2( 0.0, -2.0) * ts).rgb;
    vec3 c = texture(uInput, uv + vec2( 2.0, -2.0) * ts).rgb;

    vec3 d = texture(uInput, uv + vec2(-1.0, -1.0) * ts).rgb;
    vec3 e = texture(uInput, uv + vec2( 1.0, -1.0) * ts).rgb;

    vec3 f = texture(uInput, uv + vec2(-2.0,  0.0) * ts).rgb;
    vec3 g = texture(uInput, uv).rgb;
    vec3 h = texture(uInput, uv + vec2( 2.0,  0.0) * ts).rgb;

    vec3 i = texture(uInput, uv + vec2(-1.0,  1.0) * ts).rgb;
    vec3 j = texture(uInput, uv + vec2( 1.0,  1.0) * ts).rgb;

    vec3 k = texture(uInput, uv + vec2(-2.0,  2.0) * ts).rgb;
    vec3 l = texture(uInput, uv + vec2( 0.0,  2.0) * ts).rgb;
    vec3 m = texture(uInput, uv + vec2( 2.0,  2.0) * ts).rgb;

    // Weighted combination to reduce firefly artifacts:
    // Center diamond (d+e+i+j) gets 0.5 weight
    // Four corner quads each get 0.125 weight
    vec3 result = (d + e + i + j) * 0.25 * 0.5;
    result += (a + b + d + g) * 0.25 * 0.125;  // top-left quad
    result += (b + c + e + g) * 0.25 * 0.125;  // top-right quad
    result += (g + f + i + k) * 0.25 * 0.125;  // bottom-left quad — corrected
    result += (g + h + j + m) * 0.25 * 0.125;  // bottom-right quad — corrected

    // For the first mip, correct the corner quads to use proper samples
    // Recompute with correct groupings:
    result = (d + e + i + j) * 0.125;
    result += (a + b + f + g) * 0.03125;
    result += (b + c + g + h) * 0.03125;
    result += (f + g + k + l) * 0.03125;
    result += (g + h + l + m) * 0.03125;

    // Apply brightness threshold on first mip only
    if (pc.IsFirstMip != 0)
        result = SoftThreshold(result, pc.Threshold);

    outColor = vec4(result, 1.0);
}
