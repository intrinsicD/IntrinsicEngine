module;

#include <cstdint>

export module Extrinsic.Core.Geometry2D;

namespace Extrinsic::Core
{
    export struct Extent2D
    {
        int Width{0};
        int Height{0};
    };

    export struct Offset2D
    {
        int X{0};
        int Y{0};
    };

    export struct Rect2D
    {
        Offset2D Offset{};
        Extent2D Extent{};
    };

    export [[nodiscard]] constexpr bool IsEmpty(const Extent2D extent) noexcept
    {
        return extent.Width <= 0 || extent.Height <= 0;
    }

    export [[nodiscard]] constexpr std::uint32_t PositiveWidthOrZero(const Extent2D extent) noexcept
    {
        return extent.Width > 0 ? static_cast<std::uint32_t>(extent.Width) : 0u;
    }

    export [[nodiscard]] constexpr std::uint32_t PositiveHeightOrZero(const Extent2D extent) noexcept
    {
        return extent.Height > 0 ? static_cast<std::uint32_t>(extent.Height) : 0u;
    }
}

