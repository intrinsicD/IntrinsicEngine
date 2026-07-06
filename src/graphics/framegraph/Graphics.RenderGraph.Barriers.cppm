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
        AliasReuse,
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

    // BUG-015: a compiled ownership transfer (release/acquire pair) only describes
    // a *real* queue-family hand-off when the device's framegraph queue-capability
    // profile actually schedules the producer and consumer onto different queues.
    // When optional async-compute/transfer passes demote to graphics, both sides
    // resolve to the graphics queue and the transfer must collapse to a plain
    // barrier — otherwise single-queue submission records a QFOT acquire with no
    // matching release. This predicate is the single source of truth used by the
    // renderer's barrier lowering and is unit-tested independently of a live GPU.
    export [[nodiscard]] constexpr bool IsLiveCrossQueueOwnershipTransfer(
        const QueueOwnershipTransfer& transfer,
        const RHI::QueueCapabilityProfile profile) noexcept
    {
        if (transfer.Kind == QueueOwnershipTransferKind::None)
        {
            return false;
        }
        return RHI::ResolveQueueAffinity(transfer.SourceQueue, profile).Resolved !=
               RHI::ResolveQueueAffinity(transfer.DestinationQueue, profile).Resolved;
    }

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
        ColorAttachmentRead,
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

    export struct TextureAliasReuseBarrierPacket
    {
        std::uint32_t PreviousTextureIndex = 0;
        std::uint32_t TextureIndex = 0;
        std::uint32_t BlockIndex = 0;
        std::uint64_t OffsetBytes = 0;
        std::uint64_t SizeBytes = 0;
    };

    export struct BufferAliasReuseBarrierPacket
    {
        std::uint32_t PreviousBufferIndex = 0;
        std::uint32_t BufferIndex = 0;
        std::uint32_t BlockIndex = 0;
        std::uint64_t OffsetBytes = 0;
        std::uint64_t SizeBytes = 0;
    };

    export struct BarrierPacket
    {
        BarrierKind Kind = BarrierKind::None;
        std::uint32_t PassIndex = 0;
        BarrierPacketStage Stage = BarrierPacketStage::BeforePass;
        std::vector<TextureBarrierPacket> TextureBarriers{};
        std::vector<BufferBarrierPacket> BufferBarriers{};
        std::vector<TextureAliasReuseBarrierPacket> TextureAliasReuseBarriers{};
        std::vector<BufferAliasReuseBarrierPacket> BufferAliasReuseBarriers{};
    };
}
