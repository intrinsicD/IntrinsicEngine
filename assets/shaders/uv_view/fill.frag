#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "common.glsl"

layout(buffer_reference, scalar) readonly buffer UvViewDistortionBuffer
{
    float Values[];
};

layout(location = 0) out vec4 outColor;

const vec3 UV_FILL_BLUE = vec3(0.12, 0.45, 0.88);
const vec3 UV_INVALID_DISTORTION = vec3(0.48, 0.08, 0.62);

vec3 DistortionHeatmap(float distortion)
{
    // Conformal distortion is sigma_max / sigma_min: one is ideal and the
    // logarithmic ramp retains useful contrast for larger ratios.
    const float t = clamp(log2(max(distortion, 1.0)) / 3.0, 0.0, 1.0);
    const vec3 low = vec3(0.08, 0.38, 0.95);
    const vec3 middle = vec3(0.96, 0.88, 0.12);
    const vec3 high = vec3(0.88, 0.06, 0.04);
    return t < 0.5
        ? mix(low, middle, t * 2.0)
        : mix(middle, high, (t - 0.5) * 2.0);
}

void main()
{
    vec3 color = UV_FILL_BLUE;

    if (pc.ShowHeatmap != 0u &&
        pc.DistortionBDA != uint64_t(0))
    {
        color = UV_INVALID_DISTORTION;
        UvViewDistortionBuffer distortionValues =
            UvViewDistortionBuffer(pc.DistortionBDA);
        const float distortion = distortionValues.Values[uint(gl_PrimitiveID)];

        // Deleted, skipped, and invalid faces carry a quiet-NaN sentinel.
        if (!isnan(distortion) && !isinf(distortion) && distortion >= 1.0)
        {
            color = DistortionHeatmap(distortion);
        }
    }

    outColor = vec4(color, 1.0);
}
