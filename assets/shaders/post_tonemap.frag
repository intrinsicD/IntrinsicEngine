#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uSceneColor;

layout(push_constant) uniform Push
{
    float Exposure;    // EV-style exposure multiplier (default 1.0)
    int   Operator;    // 0 = ACES, 1 = Reinhard
} pc;

// ACES filmic tone mapping (Stephen Hill fit)
vec3 ACESFilm(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Reinhard tone mapping
vec3 Reinhard(vec3 x)
{
    return x / (x + vec3(1.0));
}

// Linear to sRGB gamma curve
vec3 LinearToSRGB(vec3 c)
{
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), c));
}

void main()
{
    vec3 hdr = texture(uSceneColor, vUV).rgb;

    // Apply exposure
    hdr *= pc.Exposure;

    // Tone mapping
    vec3 ldr;
    if (pc.Operator == 1)
        ldr = Reinhard(hdr);
    else
        ldr = ACESFilm(hdr);

    // Gamma correction (linear -> sRGB)
    ldr = LinearToSRGB(ldr);

    outColor = vec4(ldr, 1.0);
}
