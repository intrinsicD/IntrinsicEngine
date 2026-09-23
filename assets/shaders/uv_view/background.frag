#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2D globalTextures[];

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColor;

bool IsValidTextureIndex(uint index)
{
    // Slot zero is the reserved fallback/frame bridge; UINT_MAX is invalid.
    return index != 0u && index != 0xFFFFFFFFu;
}

vec2 ViewUv()
{
    const vec2 halfExtent = max(abs(pc.UvHalfExtent), vec2(1.0e-6));
    return pc.UvCenter + vNdc * halfExtent;
}

float GridLine(vec2 coordinate)
{
    const vec2 footprint = max(fwidth(coordinate), vec2(1.0e-5));
    const vec2 distanceToLine =
        abs(fract(coordinate - vec2(0.5)) - vec2(0.5)) / footprint;
    return 1.0 - min(min(distanceToLine.x, distanceToLine.y), 1.0);
}

vec3 GridBackground(vec2 uv)
{
    const float minorLine = GridLine(uv * 10.0);
    const float majorLine = GridLine(uv);
    vec3 color = vec3(0.055, 0.065, 0.085);
    color = mix(color, vec3(0.15, 0.17, 0.21), minorLine * 0.75);
    color = mix(color, vec3(0.42, 0.46, 0.54), majorLine * 0.9);
    return color;
}

vec3 CheckerBackground(vec2 uv)
{
    const vec2 cell = floor(uv * 8.0);
    const float checker = mod(cell.x + cell.y, 2.0);
    return mix(vec3(0.12, 0.13, 0.16), vec3(0.28, 0.30, 0.34), checker);
}

vec3 TexelDensityBackground(vec2 uv)
{
    // A dense, colored reference pattern makes local UV scale and orientation
    // visible without requiring scene geometry in the background pass.
    const vec2 texel = uv * 32.0;
    const vec2 cell = floor(texel);
    const float checker = mod(cell.x + cell.y, 2.0);
    const float cellLine = GridLine(texel);
    vec3 color = mix(vec3(0.06, 0.28, 0.48), vec3(0.86, 0.36, 0.10), checker);
    color = mix(color, vec3(0.94), cellLine * 0.35);
    return color;
}

float RangeParameter(float value)
{
    const float span = pc.TextureRangeMax - pc.TextureRangeMin;
    // A zero-width range still separates below/at/above instead of dividing
    // by zero; negative and zero values keep their place in the range.
    if (span <= 0.0)
        return value < pc.TextureRangeMin ? 0.0 : (value > pc.TextureRangeMax ? 1.0 : 0.5);
    return clamp((value - pc.TextureRangeMin) / span, 0.0, 1.0);
}

vec3 BakedTexel(vec2 uv, vec3 fallback)
{
    // Texel-exact display: fetch the stored texel under this UV instead of
    // filtering, so the pane shows actual baked values, never interpolation.
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThanEqual(uv, vec2(1.0))))
        return fallback * 0.55;
    const ivec2 size = textureSize(globalTextures[nonuniformEXT(pc.BackgroundTextureBindlessIndex)], 0);
    // Texture row 0 is v = 0; UV-view V grows upward like the CPU pane.
    const ivec2 texel = clamp(ivec2(floor(uv * vec2(size))), ivec2(0), size - ivec2(1));
    const float coverage = texelFetch(globalTextures[nonuniformEXT(pc.CoverageTextureBindlessIndex)], texel, 0).r;
    if (coverage == 0.0) return fallback;
    if (isnan(coverage) || isinf(coverage)) return vec3(1.0, 0.0, 1.0);
    const vec4 stored = texelFetch(globalTextures[nonuniformEXT(pc.BackgroundTextureBindlessIndex)], texel, 0);
    if (any(isnan(stored)) || any(isinf(stored)))
        return vec3(1.0, 0.0, 1.0);
    if (pc.TextureDisplayMode == UV_VIEW_TEXTURE_SCALAR_COLORMAP)
    {
        const float t = RangeParameter(stored.r);
        return texture(globalTextures[nonuniformEXT(pc.ColormapBindlessIndex)], vec2(t, 0.5)).rgb;
    }
    if (pc.TextureDisplayMode == UV_VIEW_TEXTURE_VECTOR_RANGE)
        return vec3(RangeParameter(stored.r), RangeParameter(stored.g), RangeParameter(stored.b));
    // Data alpha is independent of coverage, including a legitimate zero.
    return stored.rgb;
}

void main()
{
    const vec2 uv = ViewUv();
    vec3 color = GridBackground(uv);

    if (pc.BackgroundMode == UV_VIEW_BACKGROUND_CHECKER)
    {
        color = CheckerBackground(uv);
    }
    else if (pc.BackgroundMode == UV_VIEW_BACKGROUND_TEXEL_DENSITY)
    {
        color = TexelDensityBackground(uv);
    }
    else if (pc.BackgroundMode == UV_VIEW_BACKGROUND_BAKED_TEXTURE)
    {
        // The host validated both indices before selecting this mode.
        color = BakedTexel(uv, CheckerBackground(uv));
    }
    else if (pc.BackgroundMode == UV_VIEW_BACKGROUND_TEXTURE)
    {
        // Never dynamically index the bindless array with either invalid
        // sentinel. A missing/non-resident material texture gets a checker.
        if (IsValidTextureIndex(pc.BackgroundTextureBindlessIndex))
        {
            color = texture(
                globalTextures[nonuniformEXT(pc.BackgroundTextureBindlessIndex)],
                uv).rgb;
        }
        else
        {
            color = CheckerBackground(uv);
        }
    }

    outColor = vec4(color, 1.0);
}
