module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

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

        [[nodiscard]] GpuTransferReadbackTicket ScheduleReadback(RHI::ICommandContext& cmd,
                                                                 GpuTransferReadbackDesc desc);

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
