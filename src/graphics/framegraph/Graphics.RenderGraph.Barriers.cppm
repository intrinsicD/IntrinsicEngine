module;

#include <cstdint>
#include <vector>

export module Extrinsic.Graphics.RenderGraph:Barriers;

namespace Extrinsic::Graphics
{
    export enum class BarrierKind : std::uint8_t
    {
        None = 0,
    };

    export enum class TextureBarrierState : std::uint8_t
    {
        Undefined = 0,
        ColorAttachmentWrite,
        DepthWrite,
        DepthRead,
        ShaderRead,
        ShaderWrite,
        TransferSrc,
        TransferDst,
        Present,
    };

    export enum class BufferBarrierState : std::uint8_t
    {
        Undefined = 0,
        IndirectRead,
        IndexRead,
        VertexRead,
        ShaderRead,
        ShaderWrite,
        TransferSrc,
        TransferDst,
        HostReadback,
    };

    export struct TextureBarrierPacket
    {
        std::uint32_t TextureIndex = 0;
        TextureBarrierState Before = TextureBarrierState::Undefined;
        TextureBarrierState After = TextureBarrierState::Undefined;
    };

    export struct BufferBarrierPacket
    {
        std::uint32_t BufferIndex = 0;
        BufferBarrierState Before = BufferBarrierState::Undefined;
        BufferBarrierState After = BufferBarrierState::Undefined;
    };

    export struct BarrierPacket
    {
        BarrierKind Kind = BarrierKind::None;
        std::uint32_t PassIndex = 0;
        std::vector<TextureBarrierPacket> TextureBarriers{};
        std::vector<BufferBarrierPacket> BufferBarriers{};
    };
}
