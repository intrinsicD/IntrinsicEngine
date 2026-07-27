#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Extrinsic.Core.Tasks;
import Extrinsic.Graphics.GpuTransfer;
import Extrinsic.RHI.BufferTransfer;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.Types;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.WorldHandle;

namespace
{
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;
    namespace Runtime = Extrinsic::Runtime;

    using namespace std::chrono_literals;

    class SchedulerScope final
    {
    public:
        explicit SchedulerScope(const unsigned workers = 1)
        {
            if (Extrinsic::Core::Tasks::Scheduler::IsInitialized())
                Extrinsic::Core::Tasks::Scheduler::Shutdown();
            Extrinsic::Core::Tasks::Scheduler::Initialize(workers);
        }

        ~SchedulerScope()
        {
            Extrinsic::Core::Tasks::Scheduler::WaitForAll();
            Extrinsic::Core::Tasks::Scheduler::Shutdown();
        }

        SchedulerScope(const SchedulerScope&) = delete;
        SchedulerScope& operator=(const SchedulerScope&) = delete;
    };

    template <typename Predicate>
    [[nodiscard]] bool WaitUntil(Predicate&& predicate,
                                 const std::chrono::milliseconds timeout = 5s)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!predicate())
        {
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            std::this_thread::sleep_for(1ms);
        }
        return true;
    }

    [[nodiscard]] bool WaitUntilReadbackIssued(
        Runtime::JobService& jobs,
        const Runtime::JobToken token)
    {
        return WaitUntil(
            [&jobs, token]
            {
                return jobs.GetState(token) == Runtime::JobState::AwaitingGate;
            });
    }

    struct FollowUpRan
    {
        int Unused{0};
    };

    struct ReadbackBatchIssued
    {
        std::uint64_t TicketId{0u};
    };

    template <class T>
    [[nodiscard]] std::vector<std::byte> BytesOf(
        const std::span<const T> values)
    {
        const std::span<const std::byte> bytes = std::as_bytes(values);
        return std::vector<std::byte>{bytes.begin(), bytes.end()};
    }

    class MockReadbackTransferQueue final : public RHI::ITransferQueue
    {
    public:
        [[nodiscard]] RHI::BufferHandle AddBuffer(
            std::vector<std::byte> bytes,
            const RHI::BufferUsage usage = RHI::BufferUsage::TransferSrc)
        {
            const auto index = static_cast<std::uint32_t>(m_Buffers.size());
            m_Buffers.push_back(BufferRecord{
                .Desc = RHI::BufferDesc{
                    .SizeBytes = static_cast<std::uint64_t>(bytes.size()),
                    .Usage = usage,
                    .HostVisible = false,
                    .DebugName = "GpuResultReadbackJobTest.Buffer",
                },
                .Bytes = std::move(bytes),
            });
            return RHI::BufferHandle{index, 1u};
        }

        [[nodiscard]] const RHI::BufferDesc* GetDesc(
            const RHI::BufferHandle handle) const noexcept
        {
            const BufferRecord* record = Lookup(handle);
            return record == nullptr ? nullptr : &record->Desc;
        }

        [[nodiscard]] RHI::TransferToken UploadBuffer(
            RHI::BufferHandle,
            const void*,
            std::uint64_t,
            std::uint64_t) override
        {
            return {};
        }

        [[nodiscard]] RHI::TransferToken UploadBuffer(
            RHI::BufferHandle,
            std::span<const std::byte>,
            std::uint64_t) override
        {
            return {};
        }

        [[nodiscard]] RHI::TransferToken UploadTexture(
            RHI::TextureHandle,
            const void*,
            std::uint64_t,
            std::uint32_t,
            std::uint32_t) override
        {
            return {};
        }

        [[nodiscard]] RHI::TransferToken UploadTextureFullChain(
            RHI::TextureHandle,
            std::span<const std::byte>) override
        {
            return {};
        }

        [[nodiscard]] bool IsComplete(
            const RHI::TransferToken token) const override
        {
            return !token.IsValid();
        }

        void CollectCompleted() override
        {
            while (!m_PendingReadbacks.empty())
            {
                PendingReadback pending = std::move(m_PendingReadbacks.front());
                m_PendingReadbacks.pop_front();
                pending.Sink.Deliver(
                    std::span<const std::byte>{pending.Bytes});
                m_CompletedReadback =
                    std::max(m_CompletedReadback, pending.Token.Value);
            }
        }

        [[nodiscard]] RHI::ReadbackToken DownloadBuffer(
            const RHI::BufferHandle source,
            const std::uint64_t sizeBytes,
            const std::uint64_t offsetBytes,
            RHI::ReadbackSink sink) override
        {
            const BufferRecord* record = Lookup(source);
            if (record == nullptr ||
                !RHI::HasUsage(record->Desc.Usage,
                               RHI::BufferUsage::TransferSrc))
            {
                return {};
            }

            const auto range = RHI::ValidateBufferRange(
                record->Desc,
                RHI::BufferRange{
                    .OffsetBytes = offsetBytes,
                    .SizeBytes = sizeBytes,
                });
            if (!range.has_value() || !sink.IsValidForSize(sizeBytes))
                return {};

            std::vector<std::byte> bytes(
                static_cast<std::size_t>(sizeBytes));
            std::copy_n(
                record->Bytes.begin() +
                    static_cast<std::ptrdiff_t>(offsetBytes),
                bytes.size(),
                bytes.begin());

            const RHI::ReadbackToken token{++m_NextReadback};
            m_PendingReadbacks.push_back(PendingReadback{
                .Token = token,
                .Sink = std::move(sink),
                .Bytes = std::move(bytes),
            });
            return token;
        }

        [[nodiscard]] bool IsComplete(
            const RHI::ReadbackToken token) const override
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

        [[nodiscard]] const BufferRecord* Lookup(
            const RHI::BufferHandle handle) const noexcept
        {
            if (!handle.IsValid() || handle.Index >= m_Buffers.size())
                return nullptr;
            return &m_Buffers[handle.Index];
        }

        std::vector<BufferRecord> m_Buffers{};
        std::deque<PendingReadback> m_PendingReadbacks{};
        std::uint64_t m_NextReadback{0u};
        std::uint64_t m_CompletedReadback{0u};
    };

    class RecordingCommandContext final : public RHI::ICommandContext
    {
    public:
        void Begin() override {}
        void End() override {}
        void BeginRenderPass(const RHI::RenderPassDesc&) override {}
        void EndRenderPass() override {}
        void SetViewport(float, float, float, float, float, float) override {}
        void SetScissor(std::int32_t, std::int32_t, std::uint32_t,
                        std::uint32_t) override {}
        void BindPipeline(RHI::PipelineHandle) override {}
        void BindIndexBuffer(RHI::BufferHandle, std::uint64_t,
                             RHI::IndexType) override {}
        void PushConstants(const void*, std::uint32_t,
                           std::uint32_t) override {}
        void Draw(std::uint32_t, std::uint32_t, std::uint32_t,
                  std::uint32_t) override {}
        void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t,
                         std::int32_t, std::uint32_t) override {}
        void DrawIndirect(RHI::BufferHandle, std::uint64_t,
                          std::uint32_t) override {}
        void DrawIndexedIndirect(RHI::BufferHandle, std::uint64_t,
                                 std::uint32_t) override {}
        void DrawIndexedIndirectCount(RHI::BufferHandle, std::uint64_t,
                                      RHI::BufferHandle, std::uint64_t,
                                      std::uint32_t) override {}
        void DrawIndirectCount(RHI::BufferHandle, std::uint64_t,
                               RHI::BufferHandle, std::uint64_t,
                               std::uint32_t) override {}
        void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DispatchIndirect(RHI::BufferHandle, std::uint64_t) override {}
        void TextureBarrier(RHI::TextureHandle, RHI::TextureLayout,
                            RHI::TextureLayout) override {}
        void BufferBarrier(RHI::BufferHandle, RHI::MemoryAccess,
                           RHI::MemoryAccess) override {}
        void SubmitBarriers(const RHI::BarrierBatchDesc&) override {}
        void FillBuffer(RHI::BufferHandle, std::uint64_t, std::uint64_t,
                        std::uint32_t) override {}
        void CopyBuffer(RHI::BufferHandle, RHI::BufferHandle, std::uint64_t,
                        std::uint64_t, std::uint64_t) override {}
        void CopyBufferToTexture(RHI::BufferHandle, std::uint64_t,
                                 RHI::TextureHandle, std::uint32_t,
                                 std::uint32_t) override {}
    };

    struct Harness
    {
        MockReadbackTransferQueue Queue{};
        Graphics::GpuTransfer Transfer{Queue};
        RecordingCommandContext CommandContext{};
        Runtime::JobService Jobs{};
        Runtime::KernelEventBus Events{};
    };
}

TEST(GpuResultReadbackJob,
     BatchParksAndReleasesDependentAfterExactConsumption)
{
    SchedulerScope scheduler{1};
    Harness harness;

    const std::array firstValues{std::uint32_t{4u}, std::uint32_t{9u}};
    const std::array secondValues{2.5f, 7.5f};
    const RHI::BufferHandle first = harness.Queue.AddBuffer(
        BytesOf<std::uint32_t>(
            std::span<const std::uint32_t>{firstValues}),
        RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc);
    const RHI::BufferHandle second = harness.Queue.AddBuffer(
        BytesOf<float>(std::span<const float>{secondValues}),
        RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc);
    const RHI::BufferDesc* firstDesc = harness.Queue.GetDesc(first);
    const RHI::BufferDesc* secondDesc = harness.Queue.GetDesc(second);
    ASSERT_NE(firstDesc, nullptr);
    ASSERT_NE(secondDesc, nullptr);

    const std::array ranges{
        Graphics::GpuTransferReadbackRangeDesc{
            .Source = first,
            .SourceDesc = *firstDesc,
            .SourceRange = {
                .OffsetBytes = 0u,
                .SizeBytes = sizeof(firstValues),
            },
        },
        Graphics::GpuTransferReadbackRangeDesc{
            .Source = second,
            .SourceDesc = *secondDesc,
            .SourceRange = {
                .OffsetBytes = 0u,
                .SizeBytes = sizeof(secondValues),
            },
        },
    };
    const Graphics::GpuTransferReadbackBatchTicket ticket =
        harness.Transfer.ScheduleReadbackBatch(
            harness.CommandContext,
            Graphics::GpuTransferReadbackBatchDesc{.Ranges = ranges});
    ASSERT_TRUE(ticket.IsValid());

    std::uint32_t applyCount = 0u;
    std::vector<std::vector<std::byte>> consumedRanges{};
    Runtime::JobDesc readback{};
    readback.DebugName = "shared multi-range result readback";
    readback.Work = [ticket](const Runtime::JobCancellation&)
    {
        return Runtime::JobResultEnvelope::Make<ReadbackBatchIssued>(
            ReadbackBatchIssued{.TicketId = ticket.Id});
    };
    readback.IsReadyToApply = [&transfer = harness.Transfer, ticket]
    {
        return transfer.ReadbackBatchState(ticket) ==
               Graphics::GpuTransferReadbackBatchState::Ready;
    };
    readback.PublishCompletion =
        [&transfer = harness.Transfer,
         ranges,
         ticket,
         &applyCount,
         &consumedRanges](Runtime::KernelEventBus&,
                          const Runtime::JobResultEnvelope& envelope)
    {
        const ReadbackBatchIssued* issued =
            envelope.TryGet<ReadbackBatchIssued>();
        if (issued == nullptr || issued->TicketId != ticket.Id)
            return false;

        Graphics::GpuTransferReadbackBatchResult result{};
        if (!transfer.ConsumeReadbackBatch(ticket, ranges, result))
            return false;
        ++applyCount;
        consumedRanges = std::move(result.Ranges);
        return true;
    };
    readback.FinalizeUnpublishedOnMainThread =
        [&transfer = harness.Transfer, ticket]
    {
        (void)transfer.CancelReadbackBatch(ticket);
    };

    const Runtime::JobToken readbackToken =
        harness.Jobs.Submit(std::move(readback));
    ASSERT_TRUE(readbackToken.IsValid());

    std::vector<std::string> order{};
    Runtime::JobDesc dependent{};
    dependent.DebugName = "consume shared result";
    dependent.DependsOn.push_back(Runtime::JobDependency{
        .Job = readbackToken,
        .Reason = "shared result delivered",
    });
    dependent.Work = [&order](const Runtime::JobCancellation&)
    {
        order.push_back("worker");
        return Runtime::JobResultEnvelope::Make<FollowUpRan>(FollowUpRan{});
    };
    dependent.PublishCompletion =
        [&order](Runtime::KernelEventBus&,
                 const Runtime::JobResultEnvelope&) -> bool
    {
        order.push_back("apply");
        return true;
    };
    const Runtime::JobToken dependentToken =
        harness.Jobs.Submit(std::move(dependent));
    ASSERT_TRUE(dependentToken.IsValid());

    ASSERT_TRUE(WaitUntilReadbackIssued(harness.Jobs, readbackToken));
    EXPECT_EQ(harness.Jobs.DrainCompletions(harness.Events), 0u);
    EXPECT_EQ(harness.Jobs.GetState(readbackToken),
              Runtime::JobState::AwaitingApply);
    EXPECT_EQ(harness.Jobs.GetState(dependentToken),
              Runtime::JobState::AwaitingDependencies);
    EXPECT_TRUE(order.empty());

    harness.Queue.CollectCompleted();
    harness.Transfer.DrainCompleted(harness.CommandContext);
    EXPECT_EQ(harness.Jobs.DrainCompletions(harness.Events), 1u);
    EXPECT_EQ(harness.Jobs.GetState(readbackToken),
              Runtime::JobState::Published);

    ASSERT_TRUE(WaitUntil(
        [&]
        {
            (void)harness.Jobs.DrainCompletions(harness.Events);
            return harness.Jobs.IsComplete(dependentToken);
        }));
    EXPECT_EQ(applyCount, 1u);
    ASSERT_EQ(consumedRanges.size(), 2u);
    EXPECT_EQ(consumedRanges[0],
              BytesOf<std::uint32_t>(
                  std::span<const std::uint32_t>{firstValues}));
    EXPECT_EQ(consumedRanges[1],
              BytesOf<float>(std::span<const float>{secondValues}));
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], "worker");
    EXPECT_EQ(order[1], "apply");
    EXPECT_EQ(harness.Transfer.ReadbackBatchState(ticket),
              Graphics::GpuTransferReadbackBatchState::Consumed);

    EXPECT_EQ(harness.Jobs.DrainCompletions(harness.Events), 0u);
    EXPECT_EQ(applyCount, 1u);
}

TEST(GpuResultReadbackJob,
     WorldCancellationCancelsPendingTransportBatch)
{
    SchedulerScope scheduler{1};
    Harness harness;

    const std::array sourceValues{std::uint32_t{17u}};
    const RHI::BufferHandle source = harness.Queue.AddBuffer(
        BytesOf<std::uint32_t>(
            std::span<const std::uint32_t>{sourceValues}),
        RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc);
    const RHI::BufferDesc* sourceDesc = harness.Queue.GetDesc(source);
    ASSERT_NE(sourceDesc, nullptr);
    const std::array ranges{
        Graphics::GpuTransferReadbackRangeDesc{
            .Source = source,
            .SourceDesc = *sourceDesc,
            .SourceRange = {
                .OffsetBytes = 0u,
                .SizeBytes = sizeof(sourceValues),
            },
        },
    };
    const Graphics::GpuTransferReadbackBatchTicket ticket =
        harness.Transfer.ScheduleReadbackBatch(
            harness.CommandContext,
            Graphics::GpuTransferReadbackBatchDesc{.Ranges = ranges});
    ASSERT_TRUE(ticket.IsValid());

    constexpr Runtime::WorldHandle world{7u, 1u};
    std::uint32_t publishCount = 0u;
    std::uint32_t finalizerCount = 0u;
    Runtime::JobDesc readback{};
    readback.DebugName = "world-scoped shared result readback";
    readback.Scope = world;
    readback.CancellationGeneration = harness.Jobs.WorldGeneration(world);
    readback.Work = [ticket](const Runtime::JobCancellation&)
    {
        return Runtime::JobResultEnvelope::Make<ReadbackBatchIssued>(
            ReadbackBatchIssued{.TicketId = ticket.Id});
    };
    readback.IsReadyToApply = [&transfer = harness.Transfer, ticket]
    {
        return transfer.ReadbackBatchState(ticket) ==
               Graphics::GpuTransferReadbackBatchState::Ready;
    };
    readback.PublishCompletion =
        [&publishCount](Runtime::KernelEventBus&,
                        const Runtime::JobResultEnvelope&) -> bool
    {
        ++publishCount;
        return true;
    };
    readback.FinalizeUnpublishedOnMainThread =
        [&transfer = harness.Transfer, ticket, &finalizerCount]
    {
        ++finalizerCount;
        (void)transfer.CancelReadbackBatch(ticket);
    };

    const Runtime::JobToken token = harness.Jobs.Submit(std::move(readback));
    ASSERT_TRUE(token.IsValid());
    ASSERT_TRUE(WaitUntilReadbackIssued(harness.Jobs, token));
    EXPECT_EQ(harness.Jobs.DrainCompletions(harness.Events), 0u);
    EXPECT_EQ(harness.Jobs.GetState(token), Runtime::JobState::AwaitingApply);

    EXPECT_EQ(harness.Jobs.CancelAllForWorld(world), 1u);
    EXPECT_EQ(harness.Jobs.DrainCompletions(harness.Events), 0u);
    EXPECT_EQ(harness.Jobs.GetState(token), Runtime::JobState::Cancelled);
    EXPECT_EQ(finalizerCount, 1u);
    EXPECT_EQ(publishCount, 0u);
    EXPECT_EQ(harness.Transfer.ReadbackBatchState(ticket),
              Graphics::GpuTransferReadbackBatchState::Cancelled);

    harness.Queue.CollectCompleted();
    harness.Transfer.DrainCompleted(harness.CommandContext);
    EXPECT_EQ(harness.Jobs.DrainCompletions(harness.Events), 0u);
    EXPECT_EQ(finalizerCount, 1u);
    EXPECT_EQ(publishCount, 0u);
    EXPECT_EQ(harness.Transfer.ReadbackBatchState(ticket),
              Graphics::GpuTransferReadbackBatchState::Cancelled);
}
