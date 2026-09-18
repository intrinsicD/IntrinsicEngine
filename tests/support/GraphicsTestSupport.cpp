#include <cmath>

#include "GraphicsTestSupport.hpp"

namespace Extrinsic::Tests::GraphicsSupport
{
    namespace Readback = Support::MinimalTriangleReadback;

    const Graphics::RenderGraphCommandPassStats* FindCommandPass(
        const Graphics::RenderGraphFrameStats& stats,
        const std::string_view name) noexcept
    {
        for (const auto& pass : stats.CommandRecords.Passes)
        {
            if (pass.Name == name)
            {
                return &pass;
            }
        }
        return nullptr;
    }

    Readback::ExpectedPixel ReorderToRgba(
        const Extrinsic::RHI::Format format,
        const std::uint8_t b0,
        const std::uint8_t b1,
        const std::uint8_t b2,
        const std::uint8_t b3) noexcept
    {
        switch (format)
        {
        case Extrinsic::RHI::Format::BGRA8_UNORM:
        case Extrinsic::RHI::Format::BGRA8_SRGB:
            return Readback::ExpectedPixel{.R = b2, .G = b1, .B = b0, .A = b3};
        case Extrinsic::RHI::Format::RGBA8_UNORM:
        case Extrinsic::RHI::Format::RGBA8_SRGB:
        default:
            return Readback::ExpectedPixel{.R = b0, .G = b1, .B = b2, .A = b3};
        }
    }

    std::uint8_t SrgbByteToLinearByte(const std::uint8_t srgb) noexcept
    {
        const float s = static_cast<float>(srgb) / 255.0f;
        const float linear = (s <= 0.04045f)
                                 ? (s / 12.92f)
                                 : std::pow((s + 0.055f) / 1.055f, 2.4f);
        const float clamped = linear < 0.0f ? 0.0f : (linear > 1.0f ? 1.0f : linear);
        return static_cast<std::uint8_t>(clamped * 255.0f + 0.5f);
    }

    Readback::ExpectedPixel SrgbToLinearPixel(
        const Extrinsic::RHI::Format format,
        const Readback::ExpectedPixel& srgbPixel) noexcept
    {
        if (!IsSrgbFormat(format))
        {
            return srgbPixel;
        }
        return Readback::ExpectedPixel{
            .R = SrgbByteToLinearByte(srgbPixel.R),
            .G = SrgbByteToLinearByte(srgbPixel.G),
            .B = SrgbByteToLinearByte(srgbPixel.B),
            .A = srgbPixel.A,
        };
    }
}
