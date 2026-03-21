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

ivec2 SampleCoords(vec2 uv, ivec2 size)
{
    ivec2 maxCoord = max(size - ivec2(1), ivec2(0));
    vec2 clamped = clamp(uv * vec2(size), vec2(0.0), vec2(maxCoord));
    return ivec2(clamped);
}

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
        ivec2 size = textureSize(uSrcUint, 0);
        if (size.x <= 0 || size.y <= 0)
        {
            outColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }

        uint id = texelFetch(uSrcUint, SampleCoords(vUV, size), 0).x;
        vec3 c = (id == 0u) ? vec3(0.0) : HashColor(id);
        outColor = vec4(c, 1.0);
        return;
    }

    if (pc.Mode == 2)
    {
        ivec2 size = textureSize(uSrcDepth, 0);
        if (size.x <= 0 || size.y <= 0)
        {
            outColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }

        float z = texelFetch(uSrcDepth, SampleCoords(vUV, size), 0).r;
        float lin = LinearizeDepth(z);
        float g = clamp(lin / pc.DepthFar, 0.0, 1.0);
        outColor = vec4(vec3(g), 1.0);
        return;
    }

    ivec2 size = textureSize(uSrcFloat, 0);
    if (size.x <= 0 || size.y <= 0)
    {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 c = texelFetch(uSrcFloat, SampleCoords(vUV, size), 0).rgb;
    outColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}

