module;

#include <cstdint>

export module Graphics:GpuColor;

// =============================================================================
// GPU color packing utilities — shared by DebugDraw and render passes.
//
// Convention: packed ABGR, Vulkan byte order (R in the low bits).
//   PackColor(255, 0, 0)  → opaque red
//   PackColorF(1, 0, 0)   → opaque red (float variant)
// =============================================================================

export namespace Graphics::GpuColor
{
    [[nodiscard]] constexpr uint32_t PackColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) noexcept
    {
        return static_cast<uint32_t>(r)
             | (static_cast<uint32_t>(g) << 8)
             | (static_cast<uint32_t>(b) << 16)
             | (static_cast<uint32_t>(a) << 24);
    }

    [[nodiscard]] constexpr uint32_t PackColorF(float r, float g, float b, float a = 1.0f) noexcept
    {
        auto clamp = [](float v) noexcept -> uint8_t {
            if (v <= 0.0f) return 0;
            if (v >= 1.0f) return 255;
            return static_cast<uint8_t>(v * 255.0f + 0.5f);
        };
        return PackColor(clamp(r), clamp(g), clamp(b), clamp(a));
    }

    // =========================================================================
    // Per-face attribute helpers — used by curvature/segmentation visualization
    // and SurfacePass CachedFaceColors population.
    // =========================================================================

    // Map a normalized scalar t ∈ [0,1] to a blue→cyan→green→yellow→red
    // heat colormap. Values outside [0,1] are clamped.
    [[nodiscard]] constexpr uint32_t ScalarToHeatColor(float t, float a = 1.0f) noexcept
    {
        // Clamp to [0,1].
        if (t <= 0.0f) t = 0.0f;
        if (t >= 1.0f) t = 1.0f;

        // 4-segment piecewise-linear heat ramp:
        //   [0.00, 0.25) blue → cyan     (B=1, G increases)
        //   [0.25, 0.50) cyan → green    (B decreases, G=1)
        //   [0.50, 0.75) green → yellow  (G=1, R increases)
        //   [0.75, 1.00] yellow → red    (G decreases, R=1)
        float r = 0.0f, g = 0.0f, b = 0.0f;
        if (t < 0.25f)
        {
            b = 1.0f;
            g = t * 4.0f;
        }
        else if (t < 0.5f)
        {
            g = 1.0f;
            b = 1.0f - (t - 0.25f) * 4.0f;
        }
        else if (t < 0.75f)
        {
            g = 1.0f;
            r = (t - 0.5f) * 4.0f;
        }
        else
        {
            r = 1.0f;
            g = 1.0f - (t - 0.75f) * 4.0f;
        }

        return PackColorF(r, g, b, a);
    }

    // Map an integer label to a deterministic color from a 12-entry palette.
    // Negative labels map to transparent black.
    [[nodiscard]] constexpr uint32_t LabelToColor(int label) noexcept
    {
        if (label < 0) return 0u;

        // 12 visually distinct colors (Tableau-inspired categorical palette).
        // Each entry is constructed via PackColor(R, G, B, 0xFF) to ensure
        // the packed ABGR byte order matches our convention (R in low bits).
        constexpr uint32_t kPalette[] = {
            PackColor(0x1F, 0x77, 0xB4),  //  0: blue
            PackColor(0xFF, 0x7F, 0x0E),  //  1: orange
            PackColor(0x2C, 0xA0, 0x2C),  //  2: green
            PackColor(0xD6, 0x27, 0x28),  //  3: red
            PackColor(0x94, 0x67, 0xBD),  //  4: purple
            PackColor(0x8C, 0x56, 0x4B),  //  5: brown
            PackColor(0xE3, 0x77, 0xC2),  //  6: pink
            PackColor(0x7F, 0x7F, 0x7F),  //  7: gray
            PackColor(0xBC, 0xBD, 0x22),  //  8: olive
            PackColor(0x17, 0xBE, 0xCF),  //  9: cyan
            PackColor(0xAA, 0xFF, 0xAA),  // 10: light green
            PackColor(0xFF, 0xBB, 0x78),  // 11: peach
        };
        constexpr int kPaletteSize = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));

        return kPalette[label % kPaletteSize];
    }
}
