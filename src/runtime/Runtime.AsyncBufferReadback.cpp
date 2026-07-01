module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

module Extrinsic.Runtime.AsyncBufferReadback;

import Extrinsic.Graphics.GpuTransfer;
import Extrinsic.RHI.BufferTransfer;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TransferQueue;

namespace Extrinsic::Runtime
{
    AsyncBufferReadback::AsyncBufferReadback(Graphics::GpuTransfer& transfer) noexcept
        : m_Transfer(&transfer)
    {
    }

    bool AsyncBufferReadback::Enqueue(RHI::ICommandContext& cmd,
                                      const AsyncBufferReadbackRequest& request)
    {
        // One in-flight readback per instance: reject while a prior download is
        // still Pending so the pooled destination the sink points at is never
        // reallocated under a deferred delivery.
        if (m_Status == AsyncReadbackStatus::Pending)
            return false;

        const std::uint64_t sizeBytes = request.SourceRange.SizeBytes;
        if (m_Transfer == nullptr || !request.Source.IsValid() || sizeBytes == 0u)
            return false;

        // Reuse pooled storage; `resize` grows only when the request is larger
        // than any previous drain and never shrinks capacity. No prior sink is
        // outstanding here (status is Idle or a consumed Ready), so this cannot
        // dangle a deferred delivery destination.
        m_Destination.resize(static_cast<std::size_t>(sizeBytes));
        m_ReadySize = 0u;
        m_Status = AsyncReadbackStatus::Idle;

        const std::span<std::byte> destination{m_Destination.data(),
                                               static_cast<std::size_t>(sizeBytes)};

        Graphics::GpuTransferReadbackDesc desc{
            .Source = request.Source,
            .SourceDesc = request.SourceDesc,
            .SourceRange = request.SourceRange,
            .SourceAccess = request.SourceAccess,
            .Sink = RHI::ReadbackSink::CopyTo(destination),
        };

        const Graphics::GpuTransferReadbackTicket ticket =
            m_Transfer->ScheduleReadback(cmd, std::move(desc));
        if (!ticket.IsValid())
            return false;

        m_Ticket = ticket;
        m_ReadySize = static_cast<std::size_t>(sizeBytes);
        m_Status = AsyncReadbackStatus::Pending;
        return true;
    }

    bool AsyncBufferReadback::Poll() noexcept
    {
        if (m_Status == AsyncReadbackStatus::Pending &&
            m_Transfer != nullptr &&
            m_Transfer->IsDelivered(m_Ticket))
        {
            m_Status = AsyncReadbackStatus::Ready;
            m_Ticket = {};
        }
        return m_Status == AsyncReadbackStatus::Ready;
    }

    void AsyncBufferReadback::Reset() noexcept
    {
        m_Ticket = {};
        m_ReadySize = 0u;
        m_Status = AsyncReadbackStatus::Idle;
    }

    std::size_t AsyncBufferReadback::PooledCapacityBytes() const noexcept
    {
        return m_Destination.capacity();
    }
}
