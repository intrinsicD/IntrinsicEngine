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
import Extrinsic.Runtime.AsyncBufferReadback;

namespace
{
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;
    namespace Runtime = Extrinsic::Runtime;

    [[nodiscard]] RHI::BufferDesc MakeBufferDesc(const std::uint64_t size,
                                                 const RHI::BufferUsage usage)
    {
        return RHI::BufferDesc{
            .SizeBytes = size,
            .Usage = usage,
            .HostVisible = false,
            .DebugName = "AsyncBufferReadbackTest.Buffer",
        };
    }

    template <class T>
    [[nodiscard]] std::vector<std::byte> BytesOf(std::span<const T> values)
    {
        const std::span<const std::byte> bytes = std::as_bytes(values);
        return std::vector<std::byte>{bytes.begin(), bytes.end()};
    }

    // Minimal async transfer queue: queues downloads and delivers the requested
    // bytes to the sink from CollectCompleted(), mirroring the real backend's
    // deferred, non-blocking readback contract.
    class MockTransferQueue final : public RHI::ITransferQueue
    {
    public:
        [[nodiscard]] RHI::BufferHandle AddBuffer(std::vector<std::byte> bytes,
                                                  RHI::BufferUsage usage)
        {
            const auto index = static_cast<std::uint32_t>(m_Buffers.size());
            m_Buffers.push_back(BufferRecord{
                .Desc = MakeBufferDesc(static_cast<std::uint64_t>(bytes.size()), usage),
                .Bytes = std::move(bytes),
            });
            return RHI::BufferHandle{index, 1u};
        }

        [[nodiscard]] const RHI::BufferDesc* GetDesc(RHI::BufferHandle handle) const noexcept
        {
            const BufferRecord* record = Lookup(handle);
            return record != nullptr ? &record->Desc : nullptr;
        }

        [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle, const void*,
                                                      std::uint64_t, std::uint64_t) override { return {}; }
        [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle, std::span<const std::byte>,
                                                      std::uint64_t) override { return {}; }
        [[nodiscard]] RHI::TransferToken UploadTexture(RHI::TextureHandle, const void*, std::uint64_t,
                                                       std::uint32_t, std::uint32_t) override { return {}; }
        [[nodiscard]] bool IsComplete(RHI::TransferToken token) const override { return !token.IsValid(); }
        [[nodiscard]] RHI::TransferToken UploadTextureFullChain(RHI::TextureHandle,
                                                                std::span<const std::byte>) override { return {}; }

        void CollectCompleted() override
        {
            while (!m_PendingReadbacks.empty())
            {
                PendingReadback pending = std::move(m_PendingReadbacks.front());
                m_PendingReadbacks.pop_front();
                pending.Sink.Deliver(std::span<const std::byte>{pending.Bytes});
                m_CompletedReadback = std::max(m_CompletedReadback, pending.Token.Value);
            }
        }

        [[nodiscard]] RHI::ReadbackToken DownloadBuffer(RHI::BufferHandle src,
                                                        std::uint64_t size,
                                                        std::uint64_t offset,
                                                        RHI::ReadbackSink sink) override
        {
            const BufferRecord* record = Lookup(src);
            if (record == nullptr ||
                !RHI::HasUsage(record->Desc.Usage, RHI::BufferUsage::TransferSrc))
            {
                return {};
            }

            const auto range = RHI::ValidateBufferRange(
                record->Desc,
                RHI::BufferRange{.OffsetBytes = offset, .SizeBytes = size});
            if (!range.has_value() || !sink.IsValidForSize(size))
            {
                return {};
            }

            std::vector<std::byte> bytes(static_cast<std::size_t>(size));
            std::copy_n(record->Bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                        bytes.size(), bytes.begin());

            const RHI::ReadbackToken token{++m_NextReadback};
            m_PendingReadbacks.push_back(PendingReadback{
                .Token = token,
                .Sink = std::move(sink),
                .Bytes = std::move(bytes),
            });
            return token;
        }

        [[nodiscard]] bool IsComplete(RHI::ReadbackToken token) const override
        {
            return !token.IsValid() || token.Value <= m_CompletedReadback;
        }

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

        [[nodiscard]] const BufferRecord* Lookup(RHI::BufferHandle handle) const noexcept
        {
            if (!handle.IsValid() || handle.Index >= m_Buffers.size())
                return nullptr;
            return &m_Buffers[handle.Index];
        }

        std::vector<BufferRecord> m_Buffers{};
        std::deque<PendingReadback> m_PendingReadbacks{};
        std::uint64_t m_NextReadback = 0u;
        std::uint64_t m_CompletedReadback = 0u;
    };

    class RecordingCommandContext final : public RHI::ICommandContext
    {
    public:
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
            Barriers.push_back(BufferBarrierEvent{.Buffer = buffer, .Before = before, .After = after});
        }

        void SubmitBarriers(const RHI::BarrierBatchDesc& batch) override
        {
            for (const RHI::BufferBarrierDesc& barrier : batch.BufferBarriers)
                BufferBarrier(barrier.Buffer, barrier.BeforeAccess, barrier.AfterAccess);
        }

        void FillBuffer(RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint32_t) override {}
        void CopyBuffer(RHI::BufferHandle, RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint64_t) override {}
        void CopyBufferToTexture(RHI::BufferHandle, std::uint64_t, RHI::TextureHandle, std::uint32_t, std::uint32_t) override {}

        struct BufferBarrierEvent
        {
            RHI::BufferHandle Buffer{};
            RHI::MemoryAccess Before{RHI::MemoryAccess::None};
            RHI::MemoryAccess After{RHI::MemoryAccess::None};
        };

        std::vector<BufferBarrierEvent> Barriers{};
    };

    struct Harness
    {
        MockTransferQueue Queue{};
        Graphics::GpuTransfer Transfer{Queue};
        RecordingCommandContext CommandContext{};
    };

    [[nodiscard]] Runtime::AsyncBufferReadbackRequest MakeRequest(
        Harness& harness, const RHI::BufferHandle source, const std::uint64_t byteCount)
    {
        const RHI::BufferDesc* desc = harness.Queue.GetDesc(source);
        return Runtime::AsyncBufferReadbackRequest{
            .Source = source,
            .SourceDesc = desc != nullptr ? *desc : RHI::BufferDesc{},
            .SourceRange = RHI::BufferRange{.OffsetBytes = 0u, .SizeBytes = byteCount},
            .SourceAccess = RHI::MemoryAccess::ShaderWrite,
        };
    }

    constexpr RHI::BufferUsage kReadableStorage =
        RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc;
}

TEST(AsyncBufferReadback, RecordsBarrierParksUntilDeliveredThenExposesBytes)
{
    Harness harness;
    Runtime::AsyncBufferReadback readback{harness.Transfer};

    const std::array values{1.0f, 2.0f, 3.0f, 4.0f};
    const RHI::BufferHandle source =
        harness.Queue.AddBuffer(BytesOf<float>(std::span<const float>{values}), kReadableStorage);
    const auto byteCount = static_cast<std::uint64_t>(values.size() * sizeof(float));

    ASSERT_TRUE(readback.Enqueue(harness.CommandContext, MakeRequest(harness, source, byteCount)));
    EXPECT_EQ(readback.Status(), Runtime::AsyncReadbackStatus::Pending);

    // Exactly one ShaderWrite -> TransferRead barrier on the source; no stall.
    ASSERT_EQ(harness.CommandContext.Barriers.size(), 1u);
    EXPECT_EQ(harness.CommandContext.Barriers[0].Buffer, source);
    EXPECT_EQ(harness.CommandContext.Barriers[0].Before, RHI::MemoryAccess::ShaderWrite);
    EXPECT_EQ(harness.CommandContext.Barriers[0].After, RHI::MemoryAccess::TransferRead);

    // Not ready before the transfer queue delivers.
    EXPECT_FALSE(readback.Poll());
    EXPECT_TRUE(readback.Bytes().empty());

    harness.Queue.CollectCompleted();
    harness.Transfer.DrainCompleted(harness.CommandContext);

    ASSERT_TRUE(readback.Poll());
    EXPECT_TRUE(readback.IsReady());

    const std::span<const float> result = readback.BytesAs<float>();
    ASSERT_EQ(result.size(), values.size());
    for (std::size_t i = 0; i < values.size(); ++i)
        EXPECT_FLOAT_EQ(result[i], values[i]);
}

TEST(AsyncBufferReadback, RejectsSecondEnqueueWhilePending)
{
    Harness harness;
    Runtime::AsyncBufferReadback readback{harness.Transfer};

    const std::array values{5.0f, 6.0f};
    const RHI::BufferHandle source =
        harness.Queue.AddBuffer(BytesOf<float>(std::span<const float>{values}), kReadableStorage);
    const auto byteCount = static_cast<std::uint64_t>(values.size() * sizeof(float));

    ASSERT_TRUE(readback.Enqueue(harness.CommandContext, MakeRequest(harness, source, byteCount)));
    // One-in-flight safety: a second enqueue is rejected and records no extra work.
    EXPECT_FALSE(readback.Enqueue(harness.CommandContext, MakeRequest(harness, source, byteCount)));
    EXPECT_EQ(readback.Status(), Runtime::AsyncReadbackStatus::Pending);
    EXPECT_EQ(harness.CommandContext.Barriers.size(), 1u);
}

TEST(AsyncBufferReadback, ReusesPooledDestinationAcrossSequentialDrains)
{
    Harness harness;
    Runtime::AsyncBufferReadback readback{harness.Transfer};

    const std::array first{10.0f, 20.0f, 30.0f, 40.0f};
    const RHI::BufferHandle source =
        harness.Queue.AddBuffer(BytesOf<float>(std::span<const float>{first}), kReadableStorage);
    const auto byteCount = static_cast<std::uint64_t>(first.size() * sizeof(float));

    ASSERT_TRUE(readback.Enqueue(harness.CommandContext, MakeRequest(harness, source, byteCount)));
    harness.Queue.CollectCompleted();
    harness.Transfer.DrainCompleted(harness.CommandContext);
    ASSERT_TRUE(readback.Poll());
    const std::size_t capacityAfterFirst = readback.PooledCapacityBytes();
    ASSERT_GE(capacityAfterFirst, static_cast<std::size_t>(byteCount));

    readback.Reset();
    EXPECT_EQ(readback.Status(), Runtime::AsyncReadbackStatus::Idle);

    // A second same-size drain reuses the pooled host storage (no growth).
    ASSERT_TRUE(readback.Enqueue(harness.CommandContext, MakeRequest(harness, source, byteCount)));
    harness.Queue.CollectCompleted();
    harness.Transfer.DrainCompleted(harness.CommandContext);
    ASSERT_TRUE(readback.Poll());
    EXPECT_EQ(readback.PooledCapacityBytes(), capacityAfterFirst);

    const std::span<const float> result = readback.BytesAs<float>();
    ASSERT_EQ(result.size(), first.size());
    EXPECT_FLOAT_EQ(result[2], 30.0f);
}

TEST(AsyncBufferReadback, FailsClosedOnSourceWithoutTransferSrc)
{
    Harness harness;
    Runtime::AsyncBufferReadback readback{harness.Transfer};

    const std::array values{7.0f};
    // Storage only — not readable; the transfer queue must reject it.
    const RHI::BufferHandle source =
        harness.Queue.AddBuffer(BytesOf<float>(std::span<const float>{values}), RHI::BufferUsage::Storage);

    EXPECT_FALSE(readback.Enqueue(harness.CommandContext, MakeRequest(harness, source, sizeof(float))));
    EXPECT_EQ(readback.Status(), Runtime::AsyncReadbackStatus::Idle);
    EXPECT_TRUE(harness.CommandContext.Barriers.empty());
    EXPECT_TRUE(readback.Bytes().empty());
}
