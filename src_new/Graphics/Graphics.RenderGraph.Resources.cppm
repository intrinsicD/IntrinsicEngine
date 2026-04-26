module;

#include <cstdint>

export module Extrinsic.Graphics.RenderGraph:Resources;

namespace Extrinsic::Graphics
{
    export struct TextureRef
    {
        std::uint32_t Index = 0;
        std::uint32_t Generation = 0;

        [[nodiscard]] constexpr bool IsValid() const
        {
            return Generation != 0;
        }
    };

    export struct BufferRef
    {
        std::uint32_t Index = 0;
        std::uint32_t Generation = 0;

        [[nodiscard]] constexpr bool IsValid() const
        {
            return Generation != 0;
        }
    };
}
