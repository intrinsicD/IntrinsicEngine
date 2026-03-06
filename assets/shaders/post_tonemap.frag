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
    int   ColorGradingOn;  // 1 = apply color grading, 0 = bypass

    float Saturation;      // 0 = grayscale, 1 = neutral
    float Contrast;        // Midtone contrast (1 = neutral)
    float ColorTempOffset; // Negative = cool/blue, positive = warm/orange
    float TintOffset;      // Negative = green, positive = magenta

    vec3  Lift;            // Shadow offset (additive)
    float _pad0;
    vec3  Gamma;           // Midtone power curve
    float _pad1;
    vec3  Gain;            // Highlight multiplier
    float _pad2;
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

// ---------------------------------------------------------------------------
// Color grading utilities
// ---------------------------------------------------------------------------

// Attempt a simple Planckian locus approximation for white-balance shift.
// ColorTempOffset in [-1, 1] maps to a cool-to-warm color multiplier.
vec3 WhiteBalanceShift(vec3 color, float tempOffset, float tintOffset)
{
    // Warm: boost red, reduce blue.  Cool: boost blue, reduce red.
    // Tint: positive = magenta (boost red+blue), negative = green (boost green).
    vec3 warmCool = vec3(1.0 + tempOffset * 0.1, 1.0, 1.0 - tempOffset * 0.1);
    vec3 tint     = vec3(1.0 + tintOffset * 0.05, 1.0 - tintOffset * 0.1, 1.0 + tintOffset * 0.05);
    return color * warmCool * tint;
}

// Lift/Gamma/Gain (ASC CDL-inspired, in linear [0,1] space after tone mapping)
vec3 LiftGammaGain(vec3 color, vec3 lift, vec3 gamma, vec3 gain)
{
    // Gain (highlight multiplier)
    color = color * gain;
    // Lift (shadow offset — additive bias that mostly affects darks)
    color = color + lift * (vec3(1.0) - color);
    // Gamma (midtone curve)
    // Protect against negative or zero values before pow
    color = max(color, vec3(0.0));
    color = pow(color, vec3(1.0) / max(gamma, vec3(0.001)));
    return color;
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

    // Color grading (operates on tone-mapped [0,1] linear color)
    if (pc.ColorGradingOn != 0)
    {
        // White balance
        ldr = WhiteBalanceShift(ldr, pc.ColorTempOffset, pc.TintOffset);

        // Saturation (luminance-preserving)
        float luma = dot(ldr, vec3(0.2126, 0.7152, 0.0722));
        ldr = mix(vec3(luma), ldr, pc.Saturation);

        // Contrast (pivot at mid-gray 0.18)
        ldr = max(ldr, vec3(0.0));
        ldr = pow(ldr / 0.18, vec3(pc.Contrast)) * 0.18;

        // Lift / Gamma / Gain
        ldr = LiftGammaGain(ldr, pc.Lift, pc.Gamma, pc.Gain);

        // Clamp to valid range
        ldr = clamp(ldr, 0.0, 1.0);
    }

    // Gamma correction (linear -> sRGB)
    ldr = LinearToSRGB(ldr);

    outColor = vec4(ldr, 1.0);
}
