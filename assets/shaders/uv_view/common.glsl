#ifndef INTRINSIC_UV_VIEW_COMMON_GLSL
#define INTRINSIC_UV_VIEW_COMMON_GLSL

// Keep this scalar-layout block byte-compatible across every UV-view stage.
// The corresponding host record is 64 bytes with no implicit padding.
layout(push_constant, scalar) uniform UvViewPushConstants
{
    uint64_t TexcoordBDA;
    uint64_t DistortionBDA;
    vec2 UvCenter;
    vec2 UvHalfExtent;
    uint BackgroundMode;
    uint BackgroundTextureBindlessIndex;
    uint ShowHeatmap;
    uint TextureDisplayMode;
    float TextureRangeMin;
    float TextureRangeMax;
    uint ColormapBindlessIndex;
    uint CoverageTextureBindlessIndex;
} pc;

const uint UV_VIEW_BACKGROUND_GRID = 0u;
const uint UV_VIEW_BACKGROUND_CHECKER = 1u;
const uint UV_VIEW_BACKGROUND_TEXEL_DENSITY = 2u;
const uint UV_VIEW_BACKGROUND_TEXTURE = 3u;
const uint UV_VIEW_BACKGROUND_BAKED_TEXTURE = 4u;

const uint UV_VIEW_TEXTURE_COLOR = 0u;
const uint UV_VIEW_TEXTURE_SCALAR_COLORMAP = 1u;
const uint UV_VIEW_TEXTURE_VECTOR_RANGE = 2u;

#endif
