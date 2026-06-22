module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

module Extrinsic.Graphics.GpuTransfer;

import Extrinsic.RHI.BufferTransfer;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;

namespace Extrinsic::Graphics
{
    namespace
    {
        [[nodiscard]] bool HasValidRange(const RHI::BufferDesc& desc,
                                         const RHI::BufferRange range) noexcept
        {
            return RHI::ValidateBufferRange(desc, range).has_value();
        }

        [[nodiscard]] bool HasUsage(const RHI::BufferDesc& desc,
                                    const RHI::BufferUsage usage) noexcept
        {
            return RHI::HasUsage(desc.Usage, usage);
        }
    }

    struct GpuTransfer::Impl
    {
        struct UploadRecord
        {
            GpuTransferUploadTicket Ticket{};
            RHI::MemoryAccess ReadyAccess = RHI::MemoryAccess::ShaderRead;
            bool Ready = false;
        };

        struct ReadbackRecord
        {
            GpuTransferReadbackTicket Ticket{};
            RHI::ReadbackToken Token{};
            bool Delivered = false;
        };

        explicit Impl(RHI::ITransferQueue& transferQueue)
            : TransferQueue(&transferQueue)
        {
        }

        [[nodiscard]] std::uint64_t PendingUploads() const noexcept
        {
            return static_cast<std::uint64_t>(
                std::count_if(Uploads.begin(),
                              Uploads.end(),
                              [](const UploadRecord& record)
                              {
                                  return record.Ticket.IsValid() && !record.Ready;
                              }));
        }

        [[nodiscard]] std::uint64_t PendingReadbacks() const noexcept
        {
            return static_cast<std::uint64_t>(
                std::count_if(Readbacks.begin(),
                              Readbacks.end(),
                              [](const ReadbackRecord& record)
                              {
                                  return record.Token.IsValid() && !record.Delivered;
                              }));
        }

        RHI::ITransferQueue* TransferQueue = nullptr;
        std::uint64_t NextUploadTicket = 0;
        std::uint64_t NextReadbackTicket = 0;
        std::vector<UploadRecord> Uploads{};
        std::vector<ReadbackRecord> Readbacks{};
        GpuTransferDiagnostics Diagnostics{};
    };

    GpuTransfer::GpuTransfer(RHI::ITransferQueue& transferQueue)
        : m_Impl(std::make_unique<Impl>(transferQueue))
    {
    }

    GpuTransfer::~GpuTransfer() = default;

    GpuTransferUploadTicket GpuTransfer::ScheduleUpload(const GpuTransferUploadDesc& desc)
    {
        if (m_Impl == nullptr ||
            m_Impl->TransferQueue == nullptr ||
            !desc.Destination.IsValid() ||
            desc.Source.empty() ||
            !HasUsage(desc.DestinationDesc, RHI::BufferUsage::TransferDst))
        {
            if (m_Impl != nullptr)
                ++m_Impl->Diagnostics.UploadsDropped;
            return {};
        }

        const RHI::BufferRange destinationRange{
            .OffsetBytes = desc.DestinationOffsetBytes,
            .SizeBytes = static_cast<std::uint64_t>(desc.Source.size_bytes()),
        };
        if (!HasValidRange(desc.DestinationDesc, destinationRange))
        {
            ++m_Impl->Diagnostics.UploadsDropped;
            return {};
        }

        const RHI::TransferToken token =
            m_Impl->TransferQueue->UploadBuffer(desc.Destination,
                                                desc.Source,
                                                desc.DestinationOffsetBytes);
        if (!token.IsValid())
        {
            ++m_Impl->Diagnostics.UploadsDropped;
            return {};
        }

        GpuTransferUploadTicket ticket{
            .Id = ++m_Impl->NextUploadTicket,
            .Token = token,
            .Buffer = desc.Destination,
        };
        m_Impl->Uploads.push_back(Impl::UploadRecord{
            .Ticket = ticket,
            .ReadyAccess = desc.ReadyAccess,
            .Ready = false,
        });
        ++m_Impl->Diagnostics.UploadsScheduled;
        m_Impl->Diagnostics.PendingUploadHighWater =
            std::max(m_Impl->Diagnostics.PendingUploadHighWater, m_Impl->PendingUploads());
        return ticket;
    }

    bool GpuTransfer::UploadInCommand(RHI::ICommandContext& cmd,
                                      const GpuTransferInCommandUploadDesc& desc)
    {
        if (m_Impl == nullptr ||
            !desc.Source.IsValid() ||
            !desc.Destination.IsValid() ||
            !HasUsage(desc.SourceDesc, RHI::BufferUsage::TransferSrc) ||
            !HasUsage(desc.DestinationDesc, RHI::BufferUsage::TransferDst))
        {
            if (m_Impl != nullptr)
                ++m_Impl->Diagnostics.UploadsDropped;
            return false;
        }

        const RHI::BufferRange sourceRange{
            .OffsetBytes = desc.SourceOffsetBytes,
            .SizeBytes = desc.SizeBytes,
        };
        const RHI::BufferRange destinationRange{
            .OffsetBytes = desc.DestinationOffsetBytes,
            .SizeBytes = desc.SizeBytes,
        };
        if (!HasValidRange(desc.SourceDesc, sourceRange) ||
            !HasValidRange(desc.DestinationDesc, destinationRange))
        {
            ++m_Impl->Diagnostics.UploadsDropped;
            return false;
        }

        cmd.CopyBuffer(desc.Source,
                       desc.Destination,
                       desc.SourceOffsetBytes,
                       desc.DestinationOffsetBytes,
                       desc.SizeBytes);
        cmd.BufferBarrier(desc.Destination,
                          RHI::MemoryAccess::TransferWrite,
                          desc.ReadyAccess);
        ++m_Impl->Diagnostics.UploadsScheduled;
        ++m_Impl->Diagnostics.UploadsReady;
        ++m_Impl->Diagnostics.UploadBarriersEmitted;
        return true;
    }

    GpuTransferReadbackTicket GpuTransfer::ScheduleReadback(RHI::ICommandContext& cmd,
                                                            GpuTransferReadbackDesc desc)
    {
        if (m_Impl == nullptr ||
            m_Impl->TransferQueue == nullptr ||
            !desc.Source.IsValid() ||
            !HasUsage(desc.SourceDesc, RHI::BufferUsage::TransferSrc) ||
            !HasValidRange(desc.SourceDesc, desc.SourceRange) ||
            !desc.Sink.IsValidForSize(desc.SourceRange.SizeBytes))
        {
            if (m_Impl != nullptr)
                ++m_Impl->Diagnostics.ReadbacksDropped;
            return {};
        }

        cmd.BufferBarrier(desc.Source,
                          desc.SourceAccess,
                          RHI::MemoryAccess::TransferRead);
        ++m_Impl->Diagnostics.ReadbackBarriersEmitted;

        const RHI::ReadbackToken token =
            m_Impl->TransferQueue->DownloadBuffer(desc.Source,
                                                  desc.SourceRange.SizeBytes,
                                                  desc.SourceRange.OffsetBytes,
                                                  std::move(desc.Sink));
        if (!token.IsValid())
        {
            ++m_Impl->Diagnostics.ReadbacksDropped;
            return {};
        }

        GpuTransferReadbackTicket ticket{
            .Id = ++m_Impl->NextReadbackTicket,
            .Buffer = desc.Source,
        };
        m_Impl->Readbacks.push_back(Impl::ReadbackRecord{
            .Ticket = ticket,
            .Token = token,
            .Delivered = false,
        });
        ++m_Impl->Diagnostics.ReadbacksIssued;
        m_Impl->Diagnostics.PendingReadbackHighWater =
            std::max(m_Impl->Diagnostics.PendingReadbackHighWater, m_Impl->PendingReadbacks());
        return ticket;
    }

    void GpuTransfer::DrainCompleted(RHI::ICommandContext& cmd)
    {
        if (m_Impl == nullptr || m_Impl->TransferQueue == nullptr)
            return;

        for (Impl::UploadRecord& record : m_Impl->Uploads)
        {
            if (record.Ready || !record.Ticket.IsValid())
                continue;

            if (!m_Impl->TransferQueue->IsComplete(record.Ticket.Token))
                continue;

            cmd.BufferBarrier(record.Ticket.Buffer,
                              RHI::MemoryAccess::TransferWrite,
                              record.ReadyAccess);
            record.Ready = true;
            ++m_Impl->Diagnostics.UploadsReady;
            ++m_Impl->Diagnostics.UploadBarriersEmitted;
        }

        for (Impl::ReadbackRecord& record : m_Impl->Readbacks)
        {
            if (record.Delivered || !record.Token.IsValid())
                continue;

            if (!m_Impl->TransferQueue->IsComplete(record.Token))
                continue;

            record.Delivered = true;
            ++m_Impl->Diagnostics.ReadbacksDelivered;
        }
    }

    bool GpuTransfer::IsReady(const GpuTransferUploadTicket ticket) const noexcept
    {
        if (m_Impl == nullptr || !ticket.IsValid())
            return false;

        for (const Impl::UploadRecord& record : m_Impl->Uploads)
        {
            if (record.Ticket.Id == ticket.Id &&
                record.Ticket.Buffer == ticket.Buffer &&
                record.Ticket.Token == ticket.Token)
            {
                return record.Ready;
            }
        }
        return false;
    }

    bool GpuTransfer::IsDelivered(const GpuTransferReadbackTicket ticket) const noexcept
    {
        if (m_Impl == nullptr || !ticket.IsValid())
            return false;

        for (const Impl::ReadbackRecord& record : m_Impl->Readbacks)
        {
            if (record.Ticket.Id == ticket.Id &&
                record.Ticket.Buffer == ticket.Buffer)
            {
                return record.Delivered;
            }
        }
        return false;
    }

    bool GpuTransfer::IsComplete(const RHI::TransferToken token) const
    {
        return m_Impl == nullptr ||
               m_Impl->TransferQueue == nullptr ||
               m_Impl->TransferQueue->IsComplete(token);
    }

    GpuTransferDiagnostics GpuTransfer::GetDiagnostics() const noexcept
    {
        return m_Impl != nullptr ? m_Impl->Diagnostics : GpuTransferDiagnostics{};
    }
}
