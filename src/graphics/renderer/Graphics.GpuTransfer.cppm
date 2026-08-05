module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

export module Extrinsic.Graphics.GpuTransfer;

import Extrinsic.RHI.BufferTransfer;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;

export namespace Extrinsic::Graphics
{
    struct GpuTransferUploadTicket
    {
        std::uint64_t Id = 0;
        RHI::TransferToken Token{};
        RHI::BufferHandle Buffer{};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Id != 0 && Token.IsValid() && Buffer.IsValid();
        }
        [[nodiscard]] explicit operator bool() const noexcept { return IsValid(); }
    };

    struct GpuTransferReadbackTicket
    {
        std::uint64_t Id = 0;
        RHI::BufferHandle Buffer{};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Id != 0 && Buffer.IsValid();
        }
        [[nodiscard]] explicit operator bool() const noexcept { return IsValid(); }
    };

    struct GpuTransferUploadDesc
    {
        RHI::BufferHandle Destination{};
        RHI::BufferDesc DestinationDesc{};
        std::span<const std::byte> Source{};
        std::uint64_t DestinationOffsetBytes = 0;
        RHI::MemoryAccess ReadyAccess = RHI::MemoryAccess::ShaderRead;
    };

    struct GpuTransferInCommandUploadDesc
    {
        RHI::BufferHandle Source{};
        RHI::BufferDesc SourceDesc{};
        std::uint64_t SourceOffsetBytes = 0;
        RHI::BufferHandle Destination{};
        RHI::BufferDesc DestinationDesc{};
        std::uint64_t DestinationOffsetBytes = 0;
        std::uint64_t SizeBytes = 0;
        RHI::MemoryAccess ReadyAccess = RHI::MemoryAccess::ShaderRead;
    };

    struct GpuTransferReadbackDesc
    {
        RHI::BufferHandle Source{};
        RHI::BufferDesc SourceDesc{};
        RHI::BufferRange SourceRange{};
        RHI::MemoryAccess SourceAccess = RHI::MemoryAccess::ShaderWrite;
        RHI::ReadbackSink Sink{};
    };

    enum class GpuTransferReadbackBatchState : std::uint8_t
    {
        Invalid,
        Pending,
        Ready,
        Cancelled,
        Consumed,
    };

    // One copied range in a multi-range result readback. BufferHandle is
    // generational; consuming a batch revalidates the exact handle, range,
    // descriptor shape, and source access against the caller's current
    // expectation before any bytes are exposed.
    struct GpuTransferReadbackRangeDesc
    {
        RHI::BufferHandle Source{};
        RHI::BufferDesc SourceDesc{};
        RHI::BufferRange SourceRange{};
        RHI::MemoryAccess SourceAccess = RHI::MemoryAccess::ShaderWrite;
    };

    struct GpuTransferReadbackBatchDesc
    {
        std::span<const GpuTransferReadbackRangeDesc> Ranges{};
    };

    struct GpuTransferReadbackBatchTicket
    {
        std::uint64_t Id = 0;
        std::uint32_t RangeCount = 0;
        std::uint64_t TotalSizeBytes = 0;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Id != 0 && RangeCount != 0 && TotalSizeBytes != 0;
        }
        [[nodiscard]] explicit operator bool() const noexcept { return IsValid(); }
    };

    struct GpuTransferReadbackBatchResult
    {
        GpuTransferReadbackBatchTicket Ticket{};
        std::vector<std::vector<std::byte>> Ranges{};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Ticket.IsValid() && Ranges.size() == Ticket.RangeCount;
        }

        [[nodiscard]] std::span<const std::byte> Bytes(
            const std::size_t rangeIndex) const noexcept
        {
            if (rangeIndex >= Ranges.size())
                return {};
            return std::span<const std::byte>{Ranges[rangeIndex]};
        }
    };

    struct GpuTransferDiagnostics
    {
        std::uint64_t UploadsScheduled = 0;
        std::uint64_t UploadsReady = 0;
        std::uint64_t UploadsDropped = 0;
        std::uint64_t UploadBarriersEmitted = 0;
        std::uint64_t ReadbacksIssued = 0;
        std::uint64_t ReadbacksDelivered = 0;
        std::uint64_t ReadbacksDropped = 0;
        std::uint64_t ReadbackBarriersEmitted = 0;
        std::uint64_t ReadbackBatchesIssued = 0;
        std::uint64_t ReadbackBatchesDelivered = 0;
        std::uint64_t ReadbackBatchesCancelled = 0;
        std::uint64_t ReadbackBatchesConsumed = 0;
        std::uint64_t ReadbackBatchValidationFailures = 0;
        std::uint64_t PendingUploadHighWater = 0;
        std::uint64_t PendingReadbackHighWater = 0;
    };

    class GpuTransfer
    {
    public:
        explicit GpuTransfer(RHI::ITransferQueue& transferQueue);
        ~GpuTransfer();

        GpuTransfer(const GpuTransfer&) = delete;
        GpuTransfer& operator=(const GpuTransfer&) = delete;

        [[nodiscard]] GpuTransferUploadTicket ScheduleUpload(const GpuTransferUploadDesc& desc);

        [[nodiscard]] bool UploadInCommand(RHI::ICommandContext& cmd,
                                           const GpuTransferInCommandUploadDesc& desc);

        // The producer submission must already be enqueued before this call.
        // The transfer backend owns the source write -> transfer-read barrier
        // and queue ordering; `cmd` is retained only for source compatibility
        // with callers of the original GRAPHICS-096 facade.
        [[nodiscard]] GpuTransferReadbackTicket ScheduleReadback(RHI::ICommandContext& cmd,
                                                                 GpuTransferReadbackDesc desc);

        // Schedule every range as one logical result after its producer
        // submission has been enqueued. The transfer backend owns the source
        // barriers and queue ordering. The transfer owns copied host storage
        // until the batch is consumed or its cancelled transfers retire, so
        // callers never lend a destination span across frames.
        [[nodiscard]] GpuTransferReadbackBatchTicket ScheduleReadbackBatch(
            RHI::ICommandContext& cmd,
            const GpuTransferReadbackBatchDesc& desc);

        [[nodiscard]] GpuTransferReadbackBatchState ReadbackBatchState(
            GpuTransferReadbackBatchTicket ticket) const noexcept;

        // Cancellation suppresses result consumption but cannot revoke an
        // already submitted GPU transfer. Owned storage remains alive until
        // all underlying transfer tokens complete.
        [[nodiscard]] bool CancelReadbackBatch(
            GpuTransferReadbackBatchTicket ticket) noexcept;

        // Exposes copied bytes exactly once, only after delivery and only when
        // `expectedRanges` still exactly matches the scheduled generational
        // handles, ranges, descriptor shapes, and source accesses.
        [[nodiscard]] bool ConsumeReadbackBatch(
            GpuTransferReadbackBatchTicket ticket,
            std::span<const GpuTransferReadbackRangeDesc> expectedRanges,
            GpuTransferReadbackBatchResult& result);

        /// Call after the owner has drained `RHI::ITransferQueue::CollectCompleted()`.
        /// This emits async-upload ready barriers only for tokens that are complete.
        void DrainCompleted(RHI::ICommandContext& cmd);

        [[nodiscard]] bool IsReady(GpuTransferUploadTicket ticket) const noexcept;
        [[nodiscard]] bool IsDelivered(GpuTransferReadbackTicket ticket) const noexcept;
        [[nodiscard]] bool IsComplete(RHI::TransferToken token) const;
        [[nodiscard]] GpuTransferDiagnostics GetDiagnostics() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
