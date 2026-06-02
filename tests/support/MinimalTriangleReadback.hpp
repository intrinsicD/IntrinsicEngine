#pragma once

#include <array>
#include <cstdint>
#include <string_view>

// Reusable readback harness for the default-recipe visible-triangle smoke. The
// harness is intentionally header-only and engine-free so GPU fixtures can share
// the sample points and color quantization without depending on runtime state.
//
// Triangle constants mirror the explicit debug-triangle packet seeded by
// Test.DefaultRecipeSurfaceGpuSmoke.cpp.

namespace Extrinsic::Tests::Support::MinimalTriangleReadback
{
    // NDC vertices of the fixed visible triangle. Keep in sync with the
    // shader's `kPositions` array.
    inline constexpr std::array<std::array<float, 2>, 3> kTriangleNdc{{
        {-0.5f, -0.5f},
        { 0.5f, -0.5f},
        { 0.0f,  0.5f},
    }};

    // Reference triangle output color (linear, pre-format-conversion).
    inline constexpr float kTriangleR = 0.55f;
    inline constexpr float kTriangleG = 0.20f;
    inline constexpr float kTriangleB = 0.85f;
    inline constexpr float kTriangleA = 1.0f;

    // Clear color produced by kMinimalRenderPassColorAttachments
    // (src/graphics/renderer/Graphics.FrameRecipe.cpp).
    inline constexpr float kClearR = 0.0f;
    inline constexpr float kClearG = 0.0f;
    inline constexpr float kClearB = 0.0f;
    inline constexpr float kClearA = 1.0f;

    // Framebuffer dimensions used by the default-recipe smoke window.
    inline constexpr std::uint32_t kFramebufferWidth  = 128u;
    inline constexpr std::uint32_t kFramebufferHeight = 128u;

    struct SamplePoint
    {
        std::string_view Label;
        std::uint32_t    PixelX;
        std::uint32_t    PixelY;
        bool             InsideTriangle;
    };

    // Four deterministic sample points: one interior plus three exterior
    // corners. Chosen so the Vulkan Y-flip convention does not change
    // membership for any reasonable viewport setup -- corners are far
    // outside the (-0.5, 0.5) NDC extent of the triangle regardless of
    // orientation, and the framebuffer-center pixel maps to NDC (0, 0)
    // which is inside the triangle in either Y orientation.
    inline constexpr std::array<SamplePoint, 4> kSamplePoints{{
        SamplePoint{"interior_center",       64u,  64u, true},
        SamplePoint{"exterior_top_left",      8u,   8u, false},
        SamplePoint{"exterior_bottom_right",120u, 120u, false},
        SamplePoint{"exterior_top_right",   120u,   8u, false},
    }};

    [[nodiscard]] constexpr std::uint8_t Quantize8(const float component) noexcept
    {
        const float clamped = component < 0.0f ? 0.0f : (component > 1.0f ? 1.0f : component);
        return static_cast<std::uint8_t>(clamped * 255.0f + 0.5f);
    }

    // Expected linear 8-bit RGBA value at a sample point. The smoke caller
    // converts the backbuffer's actual format (sRGB or BGRA swizzle) into
    // this canonical layout before comparison.
    struct ExpectedPixel
    {
        std::uint8_t R = 0;
        std::uint8_t G = 0;
        std::uint8_t B = 0;
        std::uint8_t A = 0;
    };

    [[nodiscard]] constexpr ExpectedPixel ExpectedAt(const SamplePoint& point) noexcept
    {
        if (point.InsideTriangle)
        {
            return ExpectedPixel{Quantize8(kTriangleR), Quantize8(kTriangleG),
                                  Quantize8(kTriangleB), Quantize8(kTriangleA)};
        }
        return ExpectedPixel{Quantize8(kClearR), Quantize8(kClearG),
                              Quantize8(kClearB), Quantize8(kClearA)};
    }

    // Per-channel tolerance bound covering sRGB rounding and 8-bit
    // format-conversion noise across the swapchain formats the operational
    // gate accepts. Tighten by passing an explicit override.
    inline constexpr std::uint8_t kDefaultChannelTolerance = 4u;

    [[nodiscard]] constexpr bool ChannelsWithinTolerance(
        const ExpectedPixel& expected,
        const ExpectedPixel& actual,
        const std::uint8_t   tolerance = kDefaultChannelTolerance) noexcept
    {
        const auto within = [tolerance](const std::uint8_t a, const std::uint8_t b) noexcept {
            const int d = static_cast<int>(a) - static_cast<int>(b);
            const int abs = d < 0 ? -d : d;
            return abs <= static_cast<int>(tolerance);
        };
        return within(expected.R, actual.R)
            && within(expected.G, actual.G)
            && within(expected.B, actual.B)
            && within(expected.A, actual.A);
    }
}
