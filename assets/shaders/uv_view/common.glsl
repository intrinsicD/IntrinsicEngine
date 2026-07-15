#ifndef INTRINSIC_UV_VIEW_COMMON_GLSL
#define INTRINSIC_UV_VIEW_COMMON_GLSL

// Keep this scalar-layout block byte-compatible across every UV-view stage.
// The corresponding host record is 48 bytes with no implicit padding.
layout(push_constant, scalar) uniform UvViewPushConstants
{
    uint64_t TexcoordBDA;
    uint64_t DistortionBDA;
    vec2 UvCenter;
    vec2 UvHalfExtent;
    uint BackgroundMode;
    uint BackgroundTextureBindlessIndex;
    uint ShowHeatmap;
    uint Reserved;
} pc;

const uint UV_VIEW_BACKGROUND_GRID = 0u;
const uint UV_VIEW_BACKGROUND_CHECKER = 1u;
const uint UV_VIEW_BACKGROUND_TEXEL_DENSITY = 2u;
const uint UV_VIEW_BACKGROUND_TEXTURE = 3u;

#endif
