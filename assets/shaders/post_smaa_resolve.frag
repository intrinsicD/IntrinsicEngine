#version 450

// SMAA Neighborhood Blending (Jimenez et al. 2012).
// Applies the computed blend weights to produce the final anti-aliased output.
// Reads the original scene color and blends neighboring pixels according to
// the SMAA blend weight texture.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uInput;       // Original LDR color
layout(set = 0, binding = 1) uniform sampler2D uBlendWeights; // SMAA blend weights

layout(push_constant) uniform Push
{
    vec2  InvResolution;
    float _pad0;
    float _pad1;
} pc;

void main()
{
    vec2 uv  = vUV;
    vec2 rcp = pc.InvResolution;

    // Fetch blend weights for this pixel and neighbors.
    vec4 a;
    a.x = texture(uBlendWeights, uv + vec2( 0.0, -1.0) * rcp).a; // top neighbor's bottom weight
    a.y = texture(uBlendWeights, uv + vec2(-1.0,  0.0) * rcp).g; // left neighbor's right weight
    a.wz = texture(uBlendWeights, uv).xz; // current pixel: (right weight, bottom weight)

    // If no blending is needed, output the center pixel.
    if (dot(a, vec4(1.0)) < 1e-5)
    {
        outColor = vec4(texture(uInput, uv).rgb, 1.0);
        return;
    }

    // Determine dominant blending direction by comparing horizontal vs vertical.
    bool h = max(a.x, a.z) > max(a.y, a.w);

    // Compute blending offsets for the dominant direction.
    vec4 blendingOffset = vec4(0.0, a.x, 0.0, a.z);
    vec2 blendingWeight = vec2(a.x, a.z);

    if (!h)
    {
        blendingOffset = vec4(a.y, 0.0, a.w, 0.0);
        blendingWeight = vec2(a.y, a.w);
    }

    blendingWeight /= dot(blendingWeight, vec2(1.0));

    // Compute the two UVs to sample from.
    vec2 blendingCoord = blendingOffset.xy * vec2(-1.0, 1.0) * rcp + uv;
    vec2 blendingCoord2 = blendingOffset.zw * vec2(1.0, -1.0) * rcp + uv;

    // Blend the two neighbors.
    vec3 color = blendingWeight.x * texture(uInput, blendingCoord).rgb
               + blendingWeight.y * texture(uInput, blendingCoord2).rgb;

    outColor = vec4(color, 1.0);
}
