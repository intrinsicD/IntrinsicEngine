module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
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

        [[nodiscard]] bool HasSameBufferShape(
            const RHI::BufferDesc& lhs,
            const RHI::BufferDesc& rhs) noexcept
        {
            return lhs.SizeBytes == rhs.SizeBytes &&
                   lhs.Usage == rhs.Usage &&
                   lhs.HostVisible == rhs.HostVisible;
        }

        [[nodiscard]] bool HasSameRange(const RHI::BufferRange lhs,
                                        const RHI::BufferRange rhs) noexcept
        {
            return lhs.OffsetBytes == rhs.OffsetBytes &&
                   lhs.SizeBytes == rhs.SizeBytes;
        }

        [[nodiscard]] bool HasSameTicket(
            const GpuTransferReadbackBatchTicket lhs,
            const GpuTransferReadbackBatchTicket rhs) noexcept
        {
            return lhs.Id == rhs.Id &&
                   lhs.RangeCount == rhs.RangeCount &&
                   lhs.TotalSizeBytes == rhs.TotalSizeBytes;
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

        struct ReadbackBatchRangeRecord
        {
            GpuTransferReadbackRangeDesc Desc{};
            std::vector<std::byte> Bytes{};
            RHI::ReadbackToken Token{};
            bool Delivered = false;
        };

        struct ReadbackBatchRecord
        {
            GpuTransferReadbackBatchTicket Ticket{};
            std::vector<ReadbackBatchRangeRecord> Ranges{};
            GpuTransferReadbackBatchState State{
                GpuTransferReadbackBatchState::Pending};
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
            std::uint64_t pending = static_cast<std::uint64_t>(
                std::count_if(Readbacks.begin(),
                              Readbacks.end(),
                              [](const ReadbackRecord& record)
                              {
                                  return record.Token.IsValid() && !record.Delivered;
                              }));
            for (const std::unique_ptr<ReadbackBatchRecord>& batch :
                 ReadbackBatches)
            {
                if (batch == nullptr)
                    continue;
                pending += static_cast<std::uint64_t>(
                    std::count_if(
                        batch->Ranges.begin(),
                        batch->Ranges.end(),
                        [](const ReadbackBatchRangeRecord& range)
                        {
                            return range.Token.IsValid() && !range.Delivered;
                        }));
            }
            return pending;
        }

        RHI::ITransferQueue* TransferQueue = nullptr;
        std::uint64_t NextUploadTicket = 0;
        std::uint64_t NextReadbackTicket = 0;
        std::uint64_t NextReadbackBatchTicket = 0;
        std::vector<UploadRecord> Uploads{};
        std::vector<ReadbackRecord> Readbacks{};
        std::vector<std::unique_ptr<ReadbackBatchRecord>> ReadbackBatches{};
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
        // Readback is submitted by ITransferQueue, not by this caller-owned
        // command context. Recording a source barrier here would place it in a
        // later graphics submission while the transfer can execute immediately,
        // and would also keep the source alive on an unrelated frame timeline.
        (void)cmd;
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
        ++m_Impl->Diagnostics.ReadbackBarriersEmitted;

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

    GpuTransferReadbackBatchTicket GpuTransfer::ScheduleReadbackBatch(
        RHI::ICommandContext& cmd,
        const GpuTransferReadbackBatchDesc& desc)
    {
        // See ScheduleReadback(): the backend submission owns its source
        // barrier. Keep the parameter until the wider method-residency API can
        // replace this compatibility surface without a mixed-risk bug fix.
        (void)cmd;
        const auto reject = [this]() -> GpuTransferReadbackBatchTicket
        {
            if (m_Impl != nullptr)
                ++m_Impl->Diagnostics.ReadbacksDropped;
            return {};
        };

        if (m_Impl == nullptr ||
            m_Impl->TransferQueue == nullptr ||
            desc.Ranges.empty() ||
            desc.Ranges.size() > std::numeric_limits<std::uint32_t>::max())
        {
            return reject();
        }

        std::uint64_t totalSizeBytes = 0u;
        for (std::size_t rangeIndex = 0u;
             rangeIndex < desc.Ranges.size();
             ++rangeIndex)
        {
            const GpuTransferReadbackRangeDesc& range =
                desc.Ranges[rangeIndex];
            if (!range.Source.IsValid() ||
                !HasUsage(range.SourceDesc, RHI::BufferUsage::TransferSrc) ||
                !HasValidRange(range.SourceDesc, range.SourceRange) ||
                range.SourceRange.SizeBytes >
                    std::numeric_limits<std::size_t>::max() ||
                totalSizeBytes >
                    std::numeric_limits<std::uint64_t>::max() -
                        range.SourceRange.SizeBytes)
            {
                return reject();
            }

            // A generational handle denotes one exact resource description and
            // one current producer access. Contradictory descriptions for the
            // same handle fail before any barrier or transfer is recorded.
            for (std::size_t priorIndex = 0u;
                 priorIndex < rangeIndex;
                 ++priorIndex)
            {
                const GpuTransferReadbackRangeDesc& prior =
                    desc.Ranges[priorIndex];
                if (prior.Source == range.Source &&
                    (!HasSameBufferShape(prior.SourceDesc, range.SourceDesc) ||
                     prior.SourceAccess != range.SourceAccess))
                {
                    return reject();
                }
            }

            totalSizeBytes += range.SourceRange.SizeBytes;
        }

        auto record = std::make_unique<Impl::ReadbackBatchRecord>();
        record->Ticket = GpuTransferReadbackBatchTicket{
            .Id = ++m_Impl->NextReadbackBatchTicket,
            .RangeCount = static_cast<std::uint32_t>(desc.Ranges.size()),
            .TotalSizeBytes = totalSizeBytes,
        };
        record->Ranges.reserve(desc.Ranges.size());
        for (const GpuTransferReadbackRangeDesc& range : desc.Ranges)
        {
            record->Ranges.push_back(Impl::ReadbackBatchRangeRecord{
                .Desc = range,
                .Bytes = std::vector<std::byte>{
                    static_cast<std::size_t>(range.SourceRange.SizeBytes)},
            });
        }

        std::size_t issuedCount = 0u;
        for (Impl::ReadbackBatchRangeRecord& range : record->Ranges)
        {
            range.Token = m_Impl->TransferQueue->DownloadBuffer(
                range.Desc.Source,
                range.Desc.SourceRange.SizeBytes,
                range.Desc.SourceRange.OffsetBytes,
                RHI::ReadbackSink::CopyTo(
                    std::span<std::byte>{range.Bytes.data(), range.Bytes.size()}));
            if (!range.Token.IsValid())
            {
                ++m_Impl->Diagnostics.ReadbacksDropped;
                if (issuedCount != 0u)
                {
                    // Earlier sinks still point into this storage. Retain the
                    // rejected batch until those already-issued transfers land.
                    record->State = GpuTransferReadbackBatchState::Cancelled;
                    m_Impl->ReadbackBatches.push_back(std::move(record));
                    m_Impl->Diagnostics.PendingReadbackHighWater = std::max(
                        m_Impl->Diagnostics.PendingReadbackHighWater,
                        m_Impl->PendingReadbacks());
                }
                return {};
            }
            ++issuedCount;
            ++m_Impl->Diagnostics.ReadbacksIssued;
            ++m_Impl->Diagnostics.ReadbackBarriersEmitted;
        }

        const GpuTransferReadbackBatchTicket ticket = record->Ticket;
        m_Impl->ReadbackBatches.push_back(std::move(record));
        ++m_Impl->Diagnostics.ReadbackBatchesIssued;
        m_Impl->Diagnostics.PendingReadbackHighWater =
            std::max(m_Impl->Diagnostics.PendingReadbackHighWater,
                     m_Impl->PendingReadbacks());
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

        for (const std::unique_ptr<Impl::ReadbackBatchRecord>& batch :
             m_Impl->ReadbackBatches)
        {
            if (batch == nullptr ||
                batch->State == GpuTransferReadbackBatchState::Ready ||
                batch->State == GpuTransferReadbackBatchState::Consumed)
            {
                continue;
            }

            bool allDelivered = true;
            for (Impl::ReadbackBatchRangeRecord& range : batch->Ranges)
            {
                // A partial scheduling failure leaves the rejected range with
                // no token. Earlier valid tokens still own their sink storage.
                if (!range.Token.IsValid() || range.Delivered)
                    continue;
                if (!m_Impl->TransferQueue->IsComplete(range.Token))
                {
                    allDelivered = false;
                    continue;
                }

                range.Delivered = true;
                ++m_Impl->Diagnostics.ReadbacksDelivered;
            }

            if (!allDelivered)
                continue;

            if (batch->State == GpuTransferReadbackBatchState::Pending)
            {
                batch->State = GpuTransferReadbackBatchState::Ready;
                ++m_Impl->Diagnostics.ReadbackBatchesDelivered;
            }
            else if (batch->State == GpuTransferReadbackBatchState::Cancelled)
            {
                for (Impl::ReadbackBatchRangeRecord& range : batch->Ranges)
                    range.Bytes = {};
            }
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

    GpuTransferReadbackBatchState GpuTransfer::ReadbackBatchState(
        const GpuTransferReadbackBatchTicket ticket) const noexcept
    {
        if (m_Impl == nullptr || !ticket.IsValid())
            return GpuTransferReadbackBatchState::Invalid;

        for (const std::unique_ptr<Impl::ReadbackBatchRecord>& record :
             m_Impl->ReadbackBatches)
        {
            if (record != nullptr && HasSameTicket(record->Ticket, ticket))
                return record->State;
        }
        return GpuTransferReadbackBatchState::Invalid;
    }

    bool GpuTransfer::CancelReadbackBatch(
        const GpuTransferReadbackBatchTicket ticket) noexcept
    {
        if (m_Impl == nullptr || !ticket.IsValid())
            return false;

        for (const std::unique_ptr<Impl::ReadbackBatchRecord>& record :
             m_Impl->ReadbackBatches)
        {
            if (record == nullptr || !HasSameTicket(record->Ticket, ticket))
                continue;
            if (record->State != GpuTransferReadbackBatchState::Pending &&
                record->State != GpuTransferReadbackBatchState::Ready)
            {
                return false;
            }

            const bool wasReady =
                record->State == GpuTransferReadbackBatchState::Ready;
            record->State = GpuTransferReadbackBatchState::Cancelled;
            if (wasReady)
            {
                for (Impl::ReadbackBatchRangeRecord& range : record->Ranges)
                    range.Bytes = {};
            }
            ++m_Impl->Diagnostics.ReadbackBatchesCancelled;
            return true;
        }
        return false;
    }

    bool GpuTransfer::ConsumeReadbackBatch(
        const GpuTransferReadbackBatchTicket ticket,
        const std::span<const GpuTransferReadbackRangeDesc> expectedRanges,
        GpuTransferReadbackBatchResult& result)
    {
        result = {};
        if (m_Impl == nullptr ||
            !ticket.IsValid() ||
            expectedRanges.size() != ticket.RangeCount)
        {
            if (m_Impl != nullptr)
                ++m_Impl->Diagnostics.ReadbackBatchValidationFailures;
            return false;
        }

        for (const std::unique_ptr<Impl::ReadbackBatchRecord>& record :
             m_Impl->ReadbackBatches)
        {
            if (record == nullptr || !HasSameTicket(record->Ticket, ticket))
                continue;
            if (record->State != GpuTransferReadbackBatchState::Ready ||
                record->Ranges.size() != expectedRanges.size())
            {
                return false;
            }

            std::uint64_t expectedTotalSizeBytes = 0u;
            for (std::size_t rangeIndex = 0u;
                 rangeIndex < expectedRanges.size();
                 ++rangeIndex)
            {
                const GpuTransferReadbackRangeDesc& expected =
                    expectedRanges[rangeIndex];
                const Impl::ReadbackBatchRangeRecord& actual =
                    record->Ranges[rangeIndex];
                if (!expected.Source.IsValid() ||
                    expected.Source != actual.Desc.Source ||
                    !HasSameBufferShape(expected.SourceDesc,
                                        actual.Desc.SourceDesc) ||
                    !HasSameRange(expected.SourceRange,
                                  actual.Desc.SourceRange) ||
                    expected.SourceAccess != actual.Desc.SourceAccess ||
                    !HasUsage(expected.SourceDesc,
                              RHI::BufferUsage::TransferSrc) ||
                    !HasValidRange(expected.SourceDesc,
                                   expected.SourceRange) ||
                    actual.Bytes.size() != expected.SourceRange.SizeBytes ||
                    expectedTotalSizeBytes >
                        std::numeric_limits<std::uint64_t>::max() -
                            expected.SourceRange.SizeBytes)
                {
                    ++m_Impl->Diagnostics.ReadbackBatchValidationFailures;
                    return false;
                }
                expectedTotalSizeBytes += expected.SourceRange.SizeBytes;
            }

            if (expectedTotalSizeBytes != ticket.TotalSizeBytes)
            {
                ++m_Impl->Diagnostics.ReadbackBatchValidationFailures;
                return false;
            }

            result.Ticket = ticket;
            result.Ranges.reserve(record->Ranges.size());
            for (Impl::ReadbackBatchRangeRecord& range : record->Ranges)
                result.Ranges.push_back(std::move(range.Bytes));
            record->State = GpuTransferReadbackBatchState::Consumed;
            ++m_Impl->Diagnostics.ReadbackBatchesConsumed;
            return true;
        }

        ++m_Impl->Diagnostics.ReadbackBatchValidationFailures;
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
