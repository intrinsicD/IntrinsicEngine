#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uSrcFloat;
layout(set = 0, binding = 1) uniform usampler2D uSrcUint;
layout(set = 0, binding = 2) uniform sampler2D uSrcDepth;

layout(push_constant) uniform Push
{
    int Mode; // 0=float, 1=uint, 2=depth
    float DepthNear;
    float DepthFar;
} pc;

vec3 HashColor(uint v)
{
    v ^= v >> 16;
    v *= 0x7feb352du;
    v ^= v >> 15;
    v *= 0x846ca68bu;
    v ^= v >> 16;

    uint r = v;
    uint g = v * 1664525u + 1013904223u;
    uint b = v * 22695477u + 1u;

    return vec3(float(r & 255u), float(g & 255u), float(b & 255u)) / 255.0;
}

float LinearizeDepth(float z)
{
    float n = max(pc.DepthNear, 1e-6);
    float f = max(pc.DepthFar, n + 1e-6);
    return (2.0 * n) / (f + n - z * (f - n));
}

void main()
{
    if (pc.Mode == 1)
    {
        uint id = texture(uSrcUint, vUV).x;
        vec3 c = (id == 0u) ? vec3(0.0) : HashColor(id);
        outColor = vec4(c, 1.0);
        return;
    }

    if (pc.Mode == 2)
    {
        float z = texture(uSrcDepth, vUV).r;
        float lin = LinearizeDepth(z);
        float g = clamp(lin / pc.DepthFar, 0.0, 1.0);
        outColor = vec4(vec3(g), 1.0);
        return;
    }

    vec3 c = texture(uSrcFloat, vUV).rgb;
    outColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}

