#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <utility>
#include <vector>

import Extrinsic.Graphics.GpuTransfer;
import Extrinsic.RHI.BufferTransfer;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.Types;

namespace
{
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;

    [[nodiscard]] RHI::BufferDesc BufferDesc(const std::uint64_t size,
                                             const RHI::BufferUsage usage)
    {
        return RHI::BufferDesc{
            .SizeBytes = size,
            .Usage = usage,
            .HostVisible = false,
            .DebugName = "GpuTransferFacadeTest.Buffer",
        };
    }

    class MockTransferQueue final : public RHI::ITransferQueue
    {
    public:
        struct UploadCall
        {
            RHI::BufferHandle Destination{};
            std::vector<std::byte> Bytes{};
            std::uint64_t Offset = 0;
            RHI::TransferToken Token{};
        };

        [[nodiscard]] RHI::BufferHandle AddBuffer(std::vector<std::byte> bytes,
                                                  RHI::BufferUsage usage = RHI::BufferUsage::TransferSrc)
        {
            const auto index = static_cast<std::uint32_t>(m_Buffers.size());
            m_Buffers.push_back(BufferRecord{
                .Desc = BufferDesc(static_cast<std::uint64_t>(bytes.size()), usage),
                .Bytes = std::move(bytes),
            });
            return RHI::BufferHandle{index, 1u};
        }

        [[nodiscard]] const RHI::BufferDesc* GetDesc(RHI::BufferHandle handle) const noexcept
        {
            const BufferRecord* record = Lookup(handle);
            return record != nullptr ? &record->Desc : nullptr;
        }

        [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle dst,
                                                      const void* data,
                                                      std::uint64_t size,
                                                      std::uint64_t offset) override
        {
            if (RejectUploads || data == nullptr || size == 0)
                return {};

            std::vector<std::byte> bytes(static_cast<std::size_t>(size));
            const auto* source = static_cast<const std::byte*>(data);
            std::copy_n(source, bytes.size(), bytes.begin());
            return StoreUpload(dst, std::move(bytes), offset);
        }

        [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle dst,
                                                      std::span<const std::byte> src,
                                                      std::uint64_t offset) override
        {
            if (RejectUploads || src.empty())
                return {};

            return StoreUpload(dst, std::vector<std::byte>{src.begin(), src.end()}, offset);
        }

        [[nodiscard]] RHI::TransferToken UploadTexture(RHI::TextureHandle,
                                                       const void*,
                                                       std::uint64_t,
                                                       std::uint32_t,
                                                       std::uint32_t) override
        {
            return {};
        }

        [[nodiscard]] bool IsComplete(RHI::TransferToken token) const override
        {
            return !token.IsValid() || token.Value <= CompletedUpload;
        }

        void CollectCompleted() override
        {
            while (!m_PendingReadbacks.empty())
            {
                PendingReadback pending = std::move(m_PendingReadbacks.front());
                m_PendingReadbacks.pop_front();
                pending.Sink.Deliver(std::span<const std::byte>{pending.Bytes});
                CompletedReadback = std::max(CompletedReadback, pending.Token.Value);
            }
        }

        [[nodiscard]] RHI::TransferToken UploadTextureFullChain(RHI::TextureHandle,
                                                                std::span<const std::byte>) override
        {
            return {};
        }

        [[nodiscard]] RHI::ReadbackToken DownloadBuffer(RHI::BufferHandle src,
                                                        std::uint64_t size,
                                                        std::uint64_t offset,
                                                        RHI::ReadbackSink sink) override
        {
            const BufferRecord* record = Lookup(src);
            if (record == nullptr || !RHI::HasUsage(record->Desc.Usage, RHI::BufferUsage::TransferSrc))
                return {};

            const auto range = RHI::ValidateBufferRange(record->Desc,
                                                        RHI::BufferRange{
                                                            .OffsetBytes = offset,
                                                            .SizeBytes = size,
                                                        });
            if (!range.has_value() || !sink.IsValidForSize(size))
                return {};

            std::vector<std::byte> bytes(static_cast<std::size_t>(size));
            std::copy_n(record->Bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                        bytes.size(),
                        bytes.begin());

            const RHI::ReadbackToken token{++NextReadback};
            m_PendingReadbacks.push_back(PendingReadback{
                .Token = token,
                .Sink = std::move(sink),
                .Bytes = std::move(bytes),
            });
            return token;
        }

        [[nodiscard]] bool IsComplete(RHI::ReadbackToken token) const override
        {
            return !token.IsValid() || token.Value <= CompletedReadback;
        }

        void MarkComplete(RHI::TransferToken token)
        {
            CompletedUpload = std::max(CompletedUpload, token.Value);
        }

        bool RejectUploads = false;
        std::vector<UploadCall> Uploads{};
        std::uint64_t CompletedUpload = 0;
        std::uint64_t CompletedReadback = 0;

    private:
        struct BufferRecord
        {
            RHI::BufferDesc Desc{};
            std::vector<std::byte> Bytes{};
        };

        struct PendingReadback
        {
            RHI::ReadbackToken Token{};
            RHI::ReadbackSink Sink{};
            std::vector<std::byte> Bytes{};
        };

        [[nodiscard]] RHI::TransferToken StoreUpload(RHI::BufferHandle dst,
                                                     std::vector<std::byte> bytes,
                                                     std::uint64_t offset)
        {
            const RHI::TransferToken token{++NextUpload};
            Uploads.push_back(UploadCall{
                .Destination = dst,
                .Bytes = std::move(bytes),
                .Offset = offset,
                .Token = token,
            });
            return token;
        }

        [[nodiscard]] const BufferRecord* Lookup(RHI::BufferHandle handle) const noexcept
        {
            if (!handle.IsValid() || handle.Index >= m_Buffers.size())
                return nullptr;
            return &m_Buffers[handle.Index];
        }

        std::vector<BufferRecord> m_Buffers{};
        std::deque<PendingReadback> m_PendingReadbacks{};
        std::uint64_t NextUpload = 0;
        std::uint64_t NextReadback = 0;
    };

    class RecordingCommandContext final : public RHI::ICommandContext
    {
    public:
        enum class EventKind : std::uint8_t
        {
            CopyBuffer,
            BufferBarrier,
        };

        struct Event
        {
            EventKind Kind{};
            RHI::BufferHandle Source{};
            RHI::BufferHandle Destination{};
            RHI::BufferHandle Buffer{};
            std::uint64_t SourceOffset = 0;
            std::uint64_t DestinationOffset = 0;
            std::uint64_t Size = 0;
            RHI::MemoryAccess Before = RHI::MemoryAccess::None;
            RHI::MemoryAccess After = RHI::MemoryAccess::None;
        };

        void Begin() override {}
        void End() override {}
        void BeginRenderPass(const RHI::RenderPassDesc&) override {}
        void EndRenderPass() override {}
        void SetViewport(float, float, float, float, float, float) override {}
        void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}
        void BindPipeline(RHI::PipelineHandle) override {}
        void BindIndexBuffer(RHI::BufferHandle, std::uint64_t, RHI::IndexType) override {}
        void PushConstants(const void*, std::uint32_t, std::uint32_t) override {}
        void Draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {}
        void DrawIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DispatchIndirect(RHI::BufferHandle, std::uint64_t) override {}
        void TextureBarrier(RHI::TextureHandle, RHI::TextureLayout, RHI::TextureLayout) override {}

        void BufferBarrier(RHI::BufferHandle buffer,
                           RHI::MemoryAccess before,
                           RHI::MemoryAccess after) override
        {
            Events.push_back(Event{
                .Kind = EventKind::BufferBarrier,
                .Buffer = buffer,
                .Before = before,
                .After = after,
            });
        }

        void SubmitBarriers(const RHI::BarrierBatchDesc& batch) override
        {
            for (const RHI::BufferBarrierDesc& barrier : batch.BufferBarriers)
                BufferBarrier(barrier.Buffer, barrier.BeforeAccess, barrier.AfterAccess);
        }

        void FillBuffer(RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint32_t) override {}

        void CopyBuffer(RHI::BufferHandle src,
                        RHI::BufferHandle dst,
                        std::uint64_t srcOffset,
                        std::uint64_t dstOffset,
                        std::uint64_t size) override
        {
            Events.push_back(Event{
                .Kind = EventKind::CopyBuffer,
                .Source = src,
                .Destination = dst,
                .SourceOffset = srcOffset,
                .DestinationOffset = dstOffset,
                .Size = size,
            });
        }

        void CopyBufferToTexture(RHI::BufferHandle, std::uint64_t, RHI::TextureHandle, std::uint32_t, std::uint32_t) override {}

        std::vector<Event> Events{};
    };
}

TEST(GpuTransferFacade, AsyncUploadEmitsReadyBarrierOnlyAfterTokenCompletion)
{
    MockTransferQueue queue;
    Graphics::GpuTransfer transfer{queue};
    RecordingCommandContext cmd;

    const RHI::BufferHandle destination{42u, 1u};
    const RHI::BufferDesc destinationDesc =
        BufferDesc(32u, RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst);
    const std::array payload{
        std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40},
    };

    const Graphics::GpuTransferUploadTicket ticket =
        transfer.ScheduleUpload(Graphics::GpuTransferUploadDesc{
            .Destination = destination,
            .DestinationDesc = destinationDesc,
            .Source = std::span<const std::byte>{payload},
            .DestinationOffsetBytes = 8u,
        });

    ASSERT_TRUE(ticket.IsValid());
    ASSERT_EQ(queue.Uploads.size(), 1u);
    EXPECT_EQ(queue.Uploads[0].Destination, destination);
    EXPECT_EQ(queue.Uploads[0].Offset, 8u);
    EXPECT_EQ(queue.Uploads[0].Bytes, std::vector<std::byte>(payload.begin(), payload.end()));
    EXPECT_FALSE(transfer.IsReady(ticket));
    EXPECT_FALSE(transfer.IsComplete(ticket.Token));
    EXPECT_TRUE(cmd.Events.empty());

    transfer.DrainCompleted(cmd);
    EXPECT_TRUE(cmd.Events.empty());
    EXPECT_FALSE(transfer.IsReady(ticket));

    queue.MarkComplete(ticket.Token);
    transfer.DrainCompleted(cmd);

    ASSERT_EQ(cmd.Events.size(), 1u);
    EXPECT_EQ(cmd.Events[0].Kind, RecordingCommandContext::EventKind::BufferBarrier);
    EXPECT_EQ(cmd.Events[0].Buffer, destination);
    EXPECT_EQ(cmd.Events[0].Before, RHI::MemoryAccess::TransferWrite);
    EXPECT_EQ(cmd.Events[0].After, RHI::MemoryAccess::ShaderRead);
    EXPECT_TRUE(transfer.IsReady(ticket));

    transfer.DrainCompleted(cmd);
    EXPECT_EQ(cmd.Events.size(), 1u);

    const Graphics::GpuTransferDiagnostics diagnostics = transfer.GetDiagnostics();
    EXPECT_EQ(diagnostics.UploadsScheduled, 1u);
    EXPECT_EQ(diagnostics.UploadsReady, 1u);
    EXPECT_EQ(diagnostics.UploadsDropped, 0u);
    EXPECT_EQ(diagnostics.UploadBarriersEmitted, 1u);
    EXPECT_EQ(diagnostics.PendingUploadHighWater, 1u);
}

TEST(GpuTransferFacade, InCommandUploadRecordsCopyAndBarrierImmediately)
{
    MockTransferQueue queue;
    Graphics::GpuTransfer transfer{queue};
    RecordingCommandContext cmd;

    const RHI::BufferHandle source{7u, 1u};
    const RHI::BufferHandle destination{8u, 1u};
    const RHI::BufferDesc sourceDesc = BufferDesc(32u, RHI::BufferUsage::TransferSrc);
    const RHI::BufferDesc destinationDesc =
        BufferDesc(64u, RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst);

    const bool scheduled =
        transfer.UploadInCommand(cmd,
                                 Graphics::GpuTransferInCommandUploadDesc{
                                     .Source = source,
                                     .SourceDesc = sourceDesc,
                                     .SourceOffsetBytes = 4u,
                                     .Destination = destination,
                                     .DestinationDesc = destinationDesc,
                                     .DestinationOffsetBytes = 12u,
                                     .SizeBytes = 8u,
                                 });

    ASSERT_TRUE(scheduled);
    ASSERT_EQ(cmd.Events.size(), 2u);
    EXPECT_EQ(cmd.Events[0].Kind, RecordingCommandContext::EventKind::CopyBuffer);
    EXPECT_EQ(cmd.Events[0].Source, source);
    EXPECT_EQ(cmd.Events[0].Destination, destination);
    EXPECT_EQ(cmd.Events[0].SourceOffset, 4u);
    EXPECT_EQ(cmd.Events[0].DestinationOffset, 12u);
    EXPECT_EQ(cmd.Events[0].Size, 8u);
    EXPECT_EQ(cmd.Events[1].Kind, RecordingCommandContext::EventKind::BufferBarrier);
    EXPECT_EQ(cmd.Events[1].Buffer, destination);
    EXPECT_EQ(cmd.Events[1].Before, RHI::MemoryAccess::TransferWrite);
    EXPECT_EQ(cmd.Events[1].After, RHI::MemoryAccess::ShaderRead);

    const Graphics::GpuTransferDiagnostics diagnostics = transfer.GetDiagnostics();
    EXPECT_EQ(diagnostics.UploadsScheduled, 1u);
    EXPECT_EQ(diagnostics.UploadsReady, 1u);
    EXPECT_EQ(diagnostics.UploadBarriersEmitted, 1u);
    EXPECT_EQ(diagnostics.UploadsDropped, 0u);
}

TEST(GpuTransferFacade, ReadbackRecordsTransferReadBracketAndDeliversOnce)
{
    MockTransferQueue queue;
    Graphics::GpuTransfer transfer{queue};
    RecordingCommandContext cmd;

    const RHI::BufferHandle source = queue.AddBuffer({
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
        std::byte{0x05}, std::byte{0x06}, std::byte{0x07}, std::byte{0x08},
    }, RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc);
    const RHI::BufferDesc* sourceDesc = queue.GetDesc(source);
    ASSERT_NE(sourceDesc, nullptr);

    std::array<std::byte, 4> destination{};
    const Graphics::GpuTransferReadbackTicket ticket =
        transfer.ScheduleReadback(cmd,
                                  Graphics::GpuTransferReadbackDesc{
                                      .Source = source,
                                      .SourceDesc = *sourceDesc,
                                      .SourceRange = RHI::BufferRange{.OffsetBytes = 2u, .SizeBytes = destination.size()},
                                      .SourceAccess = RHI::MemoryAccess::ShaderWrite,
                                      .Sink = RHI::ReadbackSink::CopyTo(std::span<std::byte>{destination}),
                                  });

    ASSERT_TRUE(ticket.IsValid());
    ASSERT_EQ(cmd.Events.size(), 1u);
    EXPECT_EQ(cmd.Events[0].Kind, RecordingCommandContext::EventKind::BufferBarrier);
    EXPECT_EQ(cmd.Events[0].Buffer, source);
    EXPECT_EQ(cmd.Events[0].Before, RHI::MemoryAccess::ShaderWrite);
    EXPECT_EQ(cmd.Events[0].After, RHI::MemoryAccess::TransferRead);
    EXPECT_FALSE(transfer.IsDelivered(ticket));

    queue.CollectCompleted();
    const std::array expected{std::byte{0x03}, std::byte{0x04}, std::byte{0x05}, std::byte{0x06}};
    EXPECT_EQ(destination, expected);
    EXPECT_FALSE(transfer.IsDelivered(ticket));

    transfer.DrainCompleted(cmd);
    EXPECT_TRUE(transfer.IsDelivered(ticket));
    transfer.DrainCompleted(cmd);

    const Graphics::GpuTransferDiagnostics diagnostics = transfer.GetDiagnostics();
    EXPECT_EQ(diagnostics.ReadbacksIssued, 1u);
    EXPECT_EQ(diagnostics.ReadbacksDelivered, 1u);
    EXPECT_EQ(diagnostics.ReadbacksDropped, 0u);
    EXPECT_EQ(diagnostics.ReadbackBarriersEmitted, 1u);
    EXPECT_EQ(diagnostics.PendingReadbackHighWater, 1u);
    EXPECT_EQ(cmd.Events.size(), 1u);
}

TEST(GpuTransferFacade, InvalidRangesFailClosedAndUpdateDiagnostics)
{
    MockTransferQueue queue;
    Graphics::GpuTransfer transfer{queue};
    RecordingCommandContext cmd;

    const RHI::BufferHandle destination{9u, 1u};
    const RHI::BufferDesc destinationDesc =
        BufferDesc(8u, RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst);
    const std::array payload{
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
    };

    const Graphics::GpuTransferUploadTicket upload =
        transfer.ScheduleUpload(Graphics::GpuTransferUploadDesc{
            .Destination = destination,
            .DestinationDesc = destinationDesc,
            .Source = std::span<const std::byte>{payload},
            .DestinationOffsetBytes = 6u,
        });
    EXPECT_FALSE(upload.IsValid());

    const RHI::BufferHandle source = queue.AddBuffer({
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
    }, RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc);
    const RHI::BufferDesc* sourceDesc = queue.GetDesc(source);
    ASSERT_NE(sourceDesc, nullptr);
    std::array<std::byte, 4> destinationBytes{};
    const Graphics::GpuTransferReadbackTicket readback =
        transfer.ScheduleReadback(cmd,
                                  Graphics::GpuTransferReadbackDesc{
                                      .Source = source,
                                      .SourceDesc = *sourceDesc,
                                      .SourceRange = RHI::BufferRange{.OffsetBytes = 2u, .SizeBytes = destinationBytes.size()},
                                      .Sink = RHI::ReadbackSink::CopyTo(std::span<std::byte>{destinationBytes}),
                                  });
    EXPECT_FALSE(readback.IsValid());

    const bool copied =
        transfer.UploadInCommand(cmd,
                                 Graphics::GpuTransferInCommandUploadDesc{
                                     .Source = source,
                                     .SourceDesc = BufferDesc(8u, RHI::BufferUsage::TransferDst),
                                     .SourceOffsetBytes = 0u,
                                     .Destination = destination,
                                     .DestinationDesc = destinationDesc,
                                     .DestinationOffsetBytes = 0u,
                                     .SizeBytes = 4u,
                                 });
    EXPECT_FALSE(copied);
    EXPECT_TRUE(cmd.Events.empty());
    EXPECT_TRUE(queue.Uploads.empty());

    const Graphics::GpuTransferDiagnostics diagnostics = transfer.GetDiagnostics();
    EXPECT_EQ(diagnostics.UploadsScheduled, 0u);
    EXPECT_EQ(diagnostics.UploadsReady, 0u);
    EXPECT_EQ(diagnostics.UploadsDropped, 2u);
    EXPECT_EQ(diagnostics.UploadBarriersEmitted, 0u);
    EXPECT_EQ(diagnostics.ReadbacksIssued, 0u);
    EXPECT_EQ(diagnostics.ReadbacksDelivered, 0u);
    EXPECT_EQ(diagnostics.ReadbacksDropped, 1u);
    EXPECT_EQ(diagnostics.ReadbackBarriersEmitted, 0u);
}
