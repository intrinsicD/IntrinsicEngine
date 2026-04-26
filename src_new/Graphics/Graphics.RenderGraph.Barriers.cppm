module;

#include <cstdint>

export module Extrinsic.Graphics.RenderGraph:Barriers;

namespace Extrinsic::Graphics
{
    export enum class BarrierKind : std::uint8_t
    {
        None = 0,
    };

    export struct BarrierPacket
    {
        BarrierKind Kind = BarrierKind::None;
    };
}
