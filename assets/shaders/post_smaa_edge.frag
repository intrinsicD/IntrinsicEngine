#version 450

// SMAA Edge Detection — Luma-based (Jimenez et al. 2012).
// Detects edges by comparing luma differences against a threshold.
// Outputs a 2-channel edge mask (horizontal, vertical) in RG.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outEdges;

layout(set = 0, binding = 0) uniform sampler2D uInput;

layout(push_constant) uniform Push
{
    vec2  InvResolution;
    float EdgeThreshold;    // Luma contrast threshold (default 0.1)
    float _pad0;
} pc;

float Luma(vec3 c)
{
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main()
{
    vec2 uv = vUV;
    vec2 rcp = pc.InvResolution;
    float threshold = pc.EdgeThreshold;

    // Sample center and neighbors.
    float L       = Luma(texture(uInput, uv).rgb);
    float Lleft   = Luma(texture(uInput, uv + vec2(-1.0, 0.0) * rcp).rgb);
    float Ltop    = Luma(texture(uInput, uv + vec2(0.0, -1.0) * rcp).rgb);
    float Lright  = Luma(texture(uInput, uv + vec2(1.0, 0.0) * rcp).rgb);
    float Lbottom = Luma(texture(uInput, uv + vec2(0.0, 1.0) * rcp).rgb);

    // Compute deltas.
    vec4 delta = abs(vec4(L) - vec4(Lleft, Ltop, Lright, Lbottom));

    // Determine edges (left and top).
    vec2 edges = step(vec2(threshold), delta.xy);

    // Discard pixels with no edges for early out.
    if (dot(edges, vec2(1.0)) == 0.0)
        discard;

    // Local contrast adaptation: compare against opposite neighbors to reduce
    // false positives from gradients.
    float LleftLeft   = Luma(texture(uInput, uv + vec2(-2.0, 0.0) * rcp).rgb);
    float LtopTop     = Luma(texture(uInput, uv + vec2(0.0, -2.0) * rcp).rgb);

    float maxDeltaLL = max(delta.z, abs(Lleft - LleftLeft));
    float maxDeltaTT = max(delta.w, abs(Ltop  - LtopTop));

    // Apply local contrast adaptation (2x factor).
    edges.x *= step(0.5 * maxDeltaLL, delta.x);
    edges.y *= step(0.5 * maxDeltaTT, delta.y);

    outEdges = vec4(edges, 0.0, 0.0);
}
