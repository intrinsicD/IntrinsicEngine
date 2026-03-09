#pragma once

// =============================================================================
// SMAA Lookup Texture Generator — Area and Search textures.
// =============================================================================
// Generates the two lookup textures required by SMAA blend weight calculation:
//
//   Area Texture (160x560, RG8):
//     Maps edge pattern (crossing pair distances) to blend weights.
//     Based on Jimenez et al. 2012 "SMAA: Enhanced Subpixel Morphological
//     Antialiasing" — analytical solution for coverage integration.
//
//   Search Texture (66x33, R8):
//     Encodes edge-end-search offsets for bilinear acceleration.
//
// These are generated once at initialization and uploaded as GPU textures.
//
// Reference: https://github.com/iryoku/smaa (Jimenez et al. 2012)
// =============================================================================

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
#include <array>
#include <utility>

namespace Graphics::SMAA
{
    // Area texture dimensions (from SMAA reference).
    inline constexpr int kAreaTexWidth  = 160;
    inline constexpr int kAreaTexHeight = 560;
    inline constexpr int kAreaTexMaxDist = 16; // Subtexture side length
    inline constexpr int kAreaTexSubtexels = 7; // 1 + 6 subpixel offsets

    // Search texture dimensions (from SMAA reference).
    inline constexpr int kSearchTexWidth  = 66;
    inline constexpr int kSearchTexHeight = 33;

    // Number of orthogonal edge patterns: 4-bit = 16 patterns.
    inline constexpr int kNumOrthoPatterns = 16;

    // Smooth area max distance for U-patterns (reference: SMOOTH_MAX_DISTANCE).
    inline constexpr float kSmoothMaxDist = 32.0f;

    // -------------------------------------------------------------------------
    // Internal helpers.
    // -------------------------------------------------------------------------
    namespace Internal
    {
        // Saturate to [0, 1].
        inline float Saturate(float x) { return std::clamp(x, 0.0f, 1.0f); }

        // Linear interpolation.
        inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

        // 2D vector for area computation.
        struct Vec2 { float x, y; };

        // Compute the signed area contribution of a line segment from p1 to p2
        // within the pixel column [x, x+1]. Returns (area_below, area_above)
        // where "below" means the edge line is below y=0 and "above" is above.
        // This is the core of the SMAA reference area computation.
        inline Vec2 AreaUnderLine(Vec2 p1, Vec2 p2, float x)
        {
            // Line direction.
            float dx = p2.x - p1.x;
            float dy = p2.y - p1.y;

            // y values at left and right pixel boundaries.
            float x0 = x;       // Left pixel edge
            float x1 = x + 1.0f; // Right pixel edge

            // Clamp to line segment range.
            if (x0 < p1.x) x0 = p1.x;
            if (x1 > p2.x) x1 = p2.x;
            if (x0 >= x1) return {0.0f, 0.0f};

            // y values at clamped boundaries via linear interpolation.
            float t0 = (std::abs(dx) > 1e-9f) ? (x0 - p1.x) / dx : 0.0f;
            float t1 = (std::abs(dx) > 1e-9f) ? (x1 - p1.x) / dx : 0.0f;
            float y0 = p1.y + dy * t0;
            float y1 = p1.y + dy * t1;

            // Width of the covered portion of this pixel column.
            float width = x1 - x0;

            // If both y values have the same sign, it's a simple trapezoid.
            if (y0 >= 0.0f && y1 >= 0.0f)
            {
                // Entirely above y=0.
                float area = (y0 + y1) * 0.5f * width;
                return {0.0f, area};
            }
            if (y0 <= 0.0f && y1 <= 0.0f)
            {
                // Entirely below y=0.
                float area = -(y0 + y1) * 0.5f * width;
                return {area, 0.0f};
            }

            // The line crosses y=0 within this pixel. Split into two triangles.
            float xCross = x0 + (-y0 / (y1 - y0)) * width;
            float wLeft  = xCross - x0;
            float wRight = x1 - xCross;

            float areaLeft  = std::abs(y0) * wLeft * 0.5f;
            float areaRight = std::abs(y1) * wRight * 0.5f;

            if (y0 < 0.0f)
                return {areaLeft, areaRight};  // Below first, then above
            else
                return {areaRight, areaLeft};  // Above first, then below
        }

        // Smooth area adjustment for U-shaped patterns (patterns 3, 12).
        // Reduces artifacts at short distances by replacing the raw area with
        // a sqrt-based approximation, blending back to the raw area at larger
        // distances. Reference: smootharea() in AreaTex.py.
        inline Vec2 SmoothArea(float d, Vec2 area)
        {
            Vec2 smoothed;
            smoothed.x = std::sqrt(area.x * 2.0f) * 0.5f;
            smoothed.y = std::sqrt(area.y * 2.0f) * 0.5f;
            float t = Saturate(d / kSmoothMaxDist);
            return {Lerp(smoothed.x, area.x, t), Lerp(smoothed.y, area.y, t)};
        }

        // Compute the orthogonal area for a given 4-bit edge pattern.
        // Pattern bits:
        //   bit 0: left-bottom edge
        //   bit 1: right-bottom edge
        //   bit 2: left-top edge
        //   bit 3: right-top edge
        // d1 = distance to left end, d2 = distance to right end.
        // offset = subpixel vertical offset.
        inline Vec2 AreaOrthoPattern(int pattern, float left, float right, float offset)
        {
            float d = left + right + 1.0f;

            float o1 = 0.5f + offset;        // Top edge y-offset
            float o2 = 0.5f + offset - 1.0f; // Bottom edge y-offset

            Vec2 result = {0.0f, 0.0f};

            switch (pattern)
            {
            case 0:  // No edges
                break;

            case 1:  // Bottom-left L
            {
                if (left <= right)
                {
                    Vec2 p1 = {0.0f, o2};
                    Vec2 p2 = {d * 0.5f, 0.0f};
                    auto a = AreaUnderLine(p1, p2, left);
                    result = {a.x, a.y};
                }
                break;
            }
            case 2:  // Bottom-right L
            {
                if (left >= right)
                {
                    Vec2 p1 = {d * 0.5f, 0.0f};
                    Vec2 p2 = {d, o2};
                    auto a = AreaUnderLine(p1, p2, left);
                    result = {a.x, a.y};
                }
                break;
            }
            case 3:  // U-shape bottom
            {
                Vec2 p1 = {0.0f, o2};
                Vec2 mid = {d * 0.5f, 0.0f};
                Vec2 p2 = {d, o2};
                auto a1 = AreaUnderLine(p1, mid, left);
                auto a2 = AreaUnderLine(mid, p2, left);
                Vec2 combined = {a1.x + a2.x, a1.y + a2.y};
                result = SmoothArea(d, combined);
                break;
            }
            case 4:  // Top-left L
            {
                if (left <= right)
                {
                    Vec2 p1 = {0.0f, o1};
                    Vec2 p2 = {d * 0.5f, 0.0f};
                    auto a = AreaUnderLine(p1, p2, left);
                    result = {a.x, a.y};
                }
                break;
            }
            case 5:  // Straight-through horizontal (no blend needed)
                break;

            case 6:  // Z-shape (top-left + bottom-right)
            {
                Vec2 p1 = {0.0f, o1};
                Vec2 p2 = {d, o2};
                auto aFull = AreaUnderLine(p1, p2, left);

                if (std::abs(offset) > 1e-6f)
                {
                    // Blend with L decomposition for subpixel accuracy.
                    Vec2 mid = {d * 0.5f, 0.0f};
                    auto aL1 = AreaUnderLine(p1, mid, left);
                    auto aL2 = AreaUnderLine(mid, p2, left);
                    Vec2 aL = {aL1.x + aL2.x, aL1.y + aL2.y};
                    result = {(aFull.x + aL.x) * 0.5f, (aFull.y + aL.y) * 0.5f};
                }
                else
                {
                    result = aFull;
                }
                break;
            }
            case 7:  // T-shape (bottom-left + bottom-right + top-left)
            {
                Vec2 p1 = {0.0f, o1};
                Vec2 p2 = {d, o2};
                auto a = AreaUnderLine(p1, p2, left);
                result = {a.x, a.y};
                break;
            }
            case 8:  // Top-right L
            {
                if (left >= right)
                {
                    Vec2 p1 = {d * 0.5f, 0.0f};
                    Vec2 p2 = {d, o1};
                    auto a = AreaUnderLine(p1, p2, left);
                    result = {a.x, a.y};
                }
                break;
            }
            case 9:  // Z-shape (bottom-left + top-right)
            {
                Vec2 p1 = {0.0f, o2};
                Vec2 p2 = {d, o1};
                auto aFull = AreaUnderLine(p1, p2, left);

                if (std::abs(offset) > 1e-6f)
                {
                    Vec2 mid = {d * 0.5f, 0.0f};
                    auto aL1 = AreaUnderLine(p1, mid, left);
                    auto aL2 = AreaUnderLine(mid, p2, left);
                    Vec2 aL = {aL1.x + aL2.x, aL1.y + aL2.y};
                    result = {(aFull.x + aL.x) * 0.5f, (aFull.y + aL.y) * 0.5f};
                }
                else
                {
                    result = aFull;
                }
                break;
            }
            case 10: // Straight-through vertical (no blend needed)
                break;

            case 11: // T-shape
            {
                Vec2 p1 = {0.0f, o2};
                Vec2 p2 = {d, o1};
                auto a = AreaUnderLine(p1, p2, left);
                result = {a.x, a.y};
                break;
            }
            case 12: // U-shape top
            {
                Vec2 p1 = {0.0f, o1};
                Vec2 mid = {d * 0.5f, 0.0f};
                Vec2 p2 = {d, o1};
                auto a1 = AreaUnderLine(p1, mid, left);
                auto a2 = AreaUnderLine(mid, p2, left);
                Vec2 combined = {a1.x + a2.x, a1.y + a2.y};
                result = SmoothArea(d, combined);
                break;
            }
            case 13: // T-shape
            {
                Vec2 p1 = {0.0f, o2};
                Vec2 p2 = {d, o1};
                auto a = AreaUnderLine(p1, p2, left);
                result = {a.x, a.y};
                break;
            }
            case 14: // T-shape
            {
                Vec2 p1 = {0.0f, o1};
                Vec2 p2 = {d, o2};
                auto a = AreaUnderLine(p1, p2, left);
                result = {a.x, a.y};
                break;
            }
            case 15: // All four edges — fully covered (no blend needed)
                break;
            }

            return result;
        }

        // Pattern tile positions in the area texture atlas.
        // Maps 4-bit pattern index to (col, row) tile position in units of
        // kAreaTexMaxDist. Reference: edgesortho[] in AreaTex.py.
        inline constexpr std::array<std::pair<int,int>, 16> kOrthoTilePos = {{
            {0, 0}, {3, 0}, {0, 3}, {3, 3},
            {1, 0}, {4, 0}, {1, 3}, {4, 3},
            {0, 1}, {3, 1}, {0, 4}, {3, 4},
            {1, 1}, {4, 1}, {1, 4}, {4, 4}
        }};

        // Subpixel offsets for area texture subtexel rows.
        // Reference: subsample_offsets_ortho in AreaTex.py.
        inline constexpr std::array<float, 7> kOrthoSubpixelOffsets = {
            0.0f, -0.25f, 0.25f, -0.125f, 0.125f, -0.375f, 0.375f
        };

        // -------------------------------------------------------------------------
        // Search texture bilinear decode helpers.
        // -------------------------------------------------------------------------

        // The search texture exploits hardware bilinear filtering to decode a 2x2
        // edge neighborhood from a single texture fetch at (-0.25, -0.125) relative
        // to the pixel. The bilinear function computes the expected fetch result for
        // binary edge inputs e[0..3].
        inline float BilinearDecode(float e0, float e1, float e2, float e3)
        {
            float a = Lerp(e0, e1, 0.75f);     // = e0*0.25 + e1*0.75
            float b = Lerp(e2, e3, 0.75f);     // = e2*0.25 + e3*0.75
            return Lerp(a, b, 0.875f);          // = a*0.125 + b*0.875
        }

        // Delta-left: how many extra pixels to advance searching left.
        inline int DeltaLeft(const float left[4], const float top[4])
        {
            int delta = 0;
            if (top[3] == 1.0f) delta = 1;
            if (delta == 1 && top[2] == 1.0f &&
                left[1] != 1.0f && left[3] != 1.0f)
                delta = 2;
            return delta;
        }

        // Delta-right: how many extra pixels to advance searching right.
        inline int DeltaRight(const float left[4], const float top[4])
        {
            int delta = 0;
            if (top[3] == 1.0f &&
                left[1] != 1.0f && left[3] != 1.0f)
                delta = 1;
            if (delta == 1 && top[2] == 1.0f &&
                left[0] != 1.0f && left[2] != 1.0f)
                delta = 2;
            return delta;
        }

    } // namespace Internal

    // =========================================================================
    // Area texture generation.
    // =========================================================================

    // Generate the area texture data (160x560 pixels, 2 bytes per pixel: RG8_UNORM).
    // The texture is an atlas of 16×16 subtextures, one per edge pattern per
    // subpixel offset. Distances are quadratically compressed: the texel at
    // (d1, d2) stores the area for actual distances (d1², d2²), and the shader
    // applies sqrt() before lookup.
    inline std::vector<uint8_t> GenerateAreaTexture()
    {
        std::vector<uint8_t> data(kAreaTexWidth * kAreaTexHeight * 2, 0);

        int subtexSize = kAreaTexMaxDist; // 16 — each subtexture is 16x16

        for (int subtex = 0; subtex < kAreaTexSubtexels; ++subtex)
        {
            float offset = Internal::kOrthoSubpixelOffsets[subtex];

            for (int pattern = 0; pattern < kNumOrthoPatterns; ++pattern)
            {
                auto [tileCol, tileRow] = Internal::kOrthoTilePos[pattern];

                // Fill the subtexture for this (pattern, subpixel offset).
                for (int y = 0; y < subtexSize; ++y)
                {
                    for (int x = 0; x < subtexSize; ++x)
                    {
                        // Quadratic distance compression: actual distances are
                        // d1 = y², d2 = x². This allows reaching distance 15²=225
                        // with only 16 texels.
                        float d1 = static_cast<float>(y * y);
                        float d2 = static_cast<float>(x * x);

                        auto area = Internal::AreaOrthoPattern(
                            pattern, d1, d2, offset);

                        // Pixel coordinates in the full atlas.
                        // X: d2_texel + tileCol * subtexSize
                        // Y: d1_texel + (tileRow + subtex * 5) * subtexSize
                        int px = x + tileCol * subtexSize;
                        int py = y + (tileRow + subtex * 5) * subtexSize;

                        if (px >= 0 && px < kAreaTexWidth &&
                            py >= 0 && py < kAreaTexHeight)
                        {
                            int idx = (py * kAreaTexWidth + px) * 2;
                            data[idx + 0] = static_cast<uint8_t>(
                                std::clamp(area.x * 255.0f, 0.0f, 255.0f));
                            data[idx + 1] = static_cast<uint8_t>(
                                std::clamp(area.y * 255.0f, 0.0f, 255.0f));
                        }
                    }
                }
            }
        }

        // The right half (columns 80-159) is for diagonal patterns.
        // The current SMAA shader does not use diagonal pattern search,
        // so the right half remains zeroed (no diagonal blend weights).

        return data;
    }

    // =========================================================================
    // Search texture generation.
    // =========================================================================

    // Generate the search texture data (66x33 pixels, 1 byte per pixel: R8_UNORM).
    // The texture encodes delta distances (0, 1, or 2 extra pixels) for the final
    // step of the SMAA edge search algorithm. Values are stored as 127 * delta
    // to maximize dynamic range.
    //
    // Layout: left half (cols 0-32) = delta-left, right half (cols 33-65) = delta-right.
    // X axis: quantized bilinear fetch for "left" edge neighborhood.
    // Y axis: quantized bilinear fetch for "top" edge neighborhood.
    inline std::vector<uint8_t> GenerateSearchTexture()
    {
        std::vector<uint8_t> data(kSearchTexWidth * kSearchTexHeight, 0);

        // Build the reverse lookup: bilinear value → edge[4] configuration.
        // There are 16 possible 4-bit edge configurations.
        struct EdgeConfig
        {
            float e[4];      // The 4 edge values
            float bilinear;  // Expected bilinear fetch result
        };

        std::array<EdgeConfig, 16> configs;
        for (int i = 0; i < 16; ++i)
        {
            float e0 = (i & 1) ? 1.0f : 0.0f;
            float e1 = (i & 2) ? 1.0f : 0.0f;
            float e2 = (i & 4) ? 1.0f : 0.0f;
            float e3 = (i & 8) ? 1.0f : 0.0f;
            configs[i] = {{e0, e1, e2, e3},
                          Internal::BilinearDecode(e0, e1, e2, e3)};
        }

        // For each (x, y) texel, quantize to find the matching edge configs
        // and compute the delta distance.
        float quantStep = 1.0f / 32.0f; // 0.03125

        for (int y = 0; y < kSearchTexHeight; ++y)
        {
            float topBilinear = static_cast<float>(y) * quantStep;

            for (int x = 0; x < kSearchTexWidth; ++x)
            {
                bool isRight = (x >= 33);
                int localX = isRight ? (x - 33) : x;
                float leftBilinear = static_cast<float>(localX) * quantStep;

                // Find the closest matching edge configuration for each axis.
                auto findClosest = [&](float target) -> int {
                    int best = 0;
                    float bestDist = std::abs(configs[0].bilinear - target);
                    for (int i = 1; i < 16; ++i)
                    {
                        float dist = std::abs(configs[i].bilinear - target);
                        if (dist < bestDist)
                        {
                            bestDist = dist;
                            best = i;
                        }
                    }
                    return best;
                };

                int leftIdx = findClosest(leftBilinear);
                int topIdx  = findClosest(topBilinear);

                const float* leftEdges = configs[leftIdx].e;
                const float* topEdges  = configs[topIdx].e;

                int delta;
                if (isRight)
                    delta = Internal::DeltaRight(leftEdges, topEdges);
                else
                    delta = Internal::DeltaLeft(leftEdges, topEdges);

                // Encode as 127 * delta (0, 127, or 254).
                int idx = y * kSearchTexWidth + x;
                data[idx] = static_cast<uint8_t>(std::clamp(127 * delta, 0, 255));
            }
        }

        return data;
    }
}
