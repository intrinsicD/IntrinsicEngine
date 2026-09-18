// Shared graphics-test command inspection and canonical readback conversion.
#pragma once

#include <cstdint>
#include <string_view>
#include "MinimalTriangleReadback.hpp"

import Extrinsic.Graphics.RenderDiagnostics;
import Extrinsic.RHI.Descriptors;

namespace Extrinsic::Tests::GraphicsSupport
{
    namespace Readback = Support::MinimalTriangleReadback;

    // Borrows the first matching pass; invalidated when the pass vector changes.
    [[nodiscard]] const Graphics::RenderGraphCommandPassStats* FindCommandPass(
        const Graphics::RenderGraphFrameStats& stats,
        std::string_view name) noexcept;

    [[nodiscard]] Readback::ExpectedPixel ReorderToRgba(
        Extrinsic::RHI::Format format,
        std::uint8_t b0, std::uint8_t b1,
        std::uint8_t b2, std::uint8_t b3) noexcept;

    [[nodiscard]] inline constexpr bool IsSrgbFormat(const Extrinsic::RHI::Format format) noexcept
    {
        return format == Extrinsic::RHI::Format::RGBA8_SRGB ||
               format == Extrinsic::RHI::Format::BGRA8_SRGB;
    }

    [[nodiscard]] std::uint8_t SrgbByteToLinearByte(std::uint8_t srgb) noexcept;

    [[nodiscard]] Readback::ExpectedPixel SrgbToLinearPixel(
        Extrinsic::RHI::Format format,
        const Readback::ExpectedPixel& srgbPixel) noexcept;
}
