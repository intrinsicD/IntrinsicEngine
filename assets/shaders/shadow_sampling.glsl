// shadow_sampling.glsl — Shared PCF shadow sampling for forward and deferred paths.
//
// Requires the caller to define:
//   - CameraBuffer with shadowCascadeMatrices[4], shadowCascadeSplitsAndCount,
//     shadowBiasAndFilter fields
//   - sampler2DShadow bound to the shadow atlas
//
// The shadow atlas is a horizontal strip: cascade i occupies columns
// [i*2048, (i+1)*2048] in a (8192 x 2048) depth texture.

const float kShadowAtlasWidth  = 8192.0;
const float kShadowCascadeSize = 2048.0;

// Select the cascade index for a given view-space depth.
// Split distances are in [0,1], representing normalized depth in [near, far].
uint SelectCascade(float viewDepth, float nearPlane, float farPlane,
                   vec4 splits, uint cascadeCount)
{
    // Normalize depth to [0,1] range matching the split scheme.
    float normalizedDepth = clamp((viewDepth - nearPlane) / (farPlane - nearPlane), 0.0, 1.0);

    // Linear search through cascade split distances.
    for (uint i = 0u; i < cascadeCount - 1u; ++i)
    {
        if (normalizedDepth < splits[i])
            return i;
    }
    return cascadeCount - 1u;
}

// Compute shadow coordinates for a world-space position in a given cascade.
// Returns (u, v, depth) in the cascade's atlas viewport.
vec3 ComputeShadowCoord(vec3 worldPos, uint cascade, mat4 cascadeMatrices[4])
{
    vec4 lightSpacePos = cascadeMatrices[cascade] * vec4(worldPos, 1.0);
    // Perspective divide (orthographic: w == 1, but safe for future perspective shadows).
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    // Map from NDC [-1,1] to atlas UV [0,1], then offset by cascade index.
    float u = projCoords.x * 0.5 + 0.5;
    float v = projCoords.y * 0.5 + 0.5;

    // Remap u to the cascade's horizontal slice in the atlas.
    float cascadeOffset = float(cascade) * kShadowCascadeSize / kShadowAtlasWidth;
    float cascadeScale  = kShadowCascadeSize / kShadowAtlasWidth;
    u = u * cascadeScale + cascadeOffset;

    return vec3(u, v, projCoords.z);
}

// PCF shadow sampling with a configurable kernel.
// Uses the comparison sampler for hardware-accelerated bilinear depth comparison.
// filterRadius is in texels of a single cascade (2048x2048).
float SampleShadowPCF(sampler2DShadow shadowMap, vec3 shadowCoord, float filterRadius)
{
    if (shadowCoord.z < 0.0 || shadowCoord.z > 1.0)
        return 1.0; // Outside shadow frustum = fully lit.

    // Texel size in atlas UV space (horizontal = per-cascade, vertical = full height).
    float texelU = 1.0 / kShadowAtlasWidth;
    float texelV = 1.0 / kShadowCascadeSize;

    float shadow = 0.0;

    // 3x3 PCF kernel (9 taps) — good quality/performance balance.
    // Hardware comparison sampler gives free bilinear filtering per tap,
    // effectively producing a 4x4 filtered result.
    const int kHalfKernel = 1;
    const float kSampleCount = float((2 * kHalfKernel + 1) * (2 * kHalfKernel + 1));

    for (int y = -kHalfKernel; y <= kHalfKernel; ++y)
    {
        for (int x = -kHalfKernel; x <= kHalfKernel; ++x)
        {
            vec2 offset = vec2(float(x) * texelU * filterRadius,
                               float(y) * texelV * filterRadius);
            shadow += texture(shadowMap, vec3(shadowCoord.xy + offset, shadowCoord.z));
        }
    }

    return shadow / kSampleCount;
}

// Full shadow factor computation: cascade selection + bias + PCF sampling.
// Returns 0.0 (fully shadowed) to 1.0 (fully lit).
float ComputeShadowFactor(sampler2DShadow shadowMap,
                          vec3 worldPos, vec3 worldNormal,
                          mat4 viewMatrix,
                          mat4 projMatrix,
                          mat4 cascadeMatrices[4],
                          vec4 splitsAndCount,
                          vec4 biasAndFilter)
{
    float depthBias     = biasAndFilter.x;
    float normalBias    = biasAndFilter.y;
    float filterRadius  = biasAndFilter.z;
    uint  cascadeCount  = uint(biasAndFilter.w);

    if (cascadeCount == 0u)
        return 1.0; // Shadows disabled.

    // View-space depth for cascade selection.
    vec4 viewPos = viewMatrix * vec4(worldPos, 1.0);
    float viewDepth = -viewPos.z; // Camera looks along -Z in view space.

    // Extract near/far from the projection matrix (Vulkan [0,1] depth convention).
    // For Vulkan reversed or standard depth: n = P[3][2] / P[2][2], f = P[3][2] / (P[2][2] + 1.0).
    float nearPlane = projMatrix[3][2] / projMatrix[2][2];
    float farPlane  = projMatrix[3][2] / (projMatrix[2][2] + 1.0);

    uint cascade = SelectCascade(viewDepth, nearPlane, farPlane, splitsAndCount, cascadeCount);

    // Apply normal bias: offset the world position along the surface normal
    // to reduce shadow acne on surfaces nearly parallel to the light.
    vec3 biasedPos = worldPos + worldNormal * normalBias;

    vec3 shadowCoord = ComputeShadowCoord(biasedPos, cascade, cascadeMatrices);

    // Apply depth bias.
    shadowCoord.z -= depthBias;

    return SampleShadowPCF(shadowMap, shadowCoord, max(filterRadius, 1.0));
}
