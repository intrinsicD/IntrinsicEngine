module;

#include <cstdint>
#include <limits>
#include <vector>

export module Extrinsic.Graphics.RenderGraph:Barriers;

import Extrinsic.RHI.QueueAffinity;

namespace Extrinsic::Graphics
{
    export using BarrierQueue = RHI::QueueAffinity;

    export enum class BarrierKind : std::uint8_t
    {
        None = 0,
    };

    export enum class BarrierPacketStage : std::uint8_t
    {
        BeforePass = 0,
        AfterPass,
    };

    export enum class QueueSharingMode : std::uint8_t
    {
        Exclusive = 0,
        Concurrent,
    };

    export enum class QueueOwnershipTransferKind : std::uint8_t
    {
        None = 0,
        Release,
        Acquire,
    };

    export inline constexpr std::uint32_t kIgnoredQueueFamily =
        std::numeric_limits<std::uint32_t>::max();

    export [[nodiscard]] constexpr std::uint32_t QueueFamilyToken(const BarrierQueue queue) noexcept
    {
        return static_cast<std::uint32_t>(queue);
    }

    export struct QueueOwnershipTransfer
    {
        QueueOwnershipTransferKind Kind = QueueOwnershipTransferKind::None;
        BarrierQueue SourceQueue = BarrierQueue::Graphics;
        BarrierQueue DestinationQueue = BarrierQueue::Graphics;
        std::uint32_t SourceQueueFamily = kIgnoredQueueFamily;
        std::uint32_t DestinationQueueFamily = kIgnoredQueueFamily;
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
        QueueSharingMode SharingMode = QueueSharingMode::Exclusive;
        QueueOwnershipTransfer OwnershipTransfer{};
    };

    export struct BufferBarrierPacket
    {
        std::uint32_t BufferIndex = 0;
        BufferBarrierState Before = BufferBarrierState::Undefined;
        BufferBarrierState After = BufferBarrierState::Undefined;
        QueueSharingMode SharingMode = QueueSharingMode::Exclusive;
        QueueOwnershipTransfer OwnershipTransfer{};
    };

    export struct BarrierPacket
    {
        BarrierKind Kind = BarrierKind::None;
        std::uint32_t PassIndex = 0;
        BarrierPacketStage Stage = BarrierPacketStage::BeforePass;
        std::vector<TextureBarrierPacket> TextureBarriers{};
        std::vector<BufferBarrierPacket> BufferBarriers{};
    };
}
