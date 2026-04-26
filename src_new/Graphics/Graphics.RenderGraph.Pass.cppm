module;

#include <cstdint>
#include <string>

export module Extrinsic.Graphics.RenderGraph:Pass;

namespace Extrinsic::Graphics
{
    export struct PassRef
    {
        std::uint32_t Index = 0;
        std::uint32_t Generation = 0;

        [[nodiscard]] constexpr bool IsValid() const
        {
            return Generation != 0;
        }
    };

    export struct RenderPassRecord
    {
        std::string Name{};
        bool SideEffect = false;
    };
}
