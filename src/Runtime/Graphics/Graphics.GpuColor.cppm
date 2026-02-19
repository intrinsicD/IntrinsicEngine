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
}
