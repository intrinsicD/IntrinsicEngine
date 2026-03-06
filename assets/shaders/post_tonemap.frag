#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uSceneColor;
layout(set = 0, binding = 1) uniform sampler2D uBloomColor;

layout(push_constant) uniform Push
{
    float Exposure;        // EV-style exposure multiplier (default 1.0)
    int   Operator;        // 0 = ACES, 1 = Reinhard, 2 = Uncharted2
    float BloomIntensity;  // Bloom mix strength (0 = off)
    float _pad0;
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

// Uncharted 2 filmic tone mapping (Hable 2010)
vec3 Uncharted2Helper(vec3 x)
{
    const float A = 0.15; // Shoulder strength
    const float B = 0.50; // Linear strength
    const float C = 0.10; // Linear angle
    const float D = 0.20; // Toe strength
    const float E = 0.02; // Toe numerator
    const float F = 0.30; // Toe denominator
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 Uncharted2(vec3 x)
{
    const float W = 11.2; // Linear white point
    vec3 curr = Uncharted2Helper(x);
    vec3 whiteScale = vec3(1.0) / Uncharted2Helper(vec3(W));
    return curr * whiteScale;
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

    // Composite bloom (additive, in HDR space before tone mapping)
    if (pc.BloomIntensity > 0.0)
    {
        vec3 bloom = texture(uBloomColor, vUV).rgb;
        hdr += bloom * pc.BloomIntensity;
    }

    // Apply exposure
    hdr *= pc.Exposure;

    // Tone mapping
    vec3 ldr;
    if (pc.Operator == 1)
        ldr = Reinhard(hdr);
    else if (pc.Operator == 2)
        ldr = Uncharted2(hdr);
    else
        ldr = ACESFilm(hdr);

    // Gamma correction (linear -> sRGB)
    ldr = LinearToSRGB(ldr);

    outColor = vec4(ldr, 1.0);
}
