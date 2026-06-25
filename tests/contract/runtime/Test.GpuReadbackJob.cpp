#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <utility>
#include <vector>

import Extrinsic.Core.Error;
import Extrinsic.Graphics.GpuTransfer;
import Extrinsic.RHI.BufferTransfer;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.Types;
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.GpuReadbackJob;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.StreamingExecutor;
import Geometry.Properties;

namespace
{
    namespace Core = Extrinsic::Core;
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;
    namespace Runtime = Extrinsic::Runtime;

    [[nodiscard]] Runtime::DerivedJobKey MakeKey(
        const std::uint32_t entityId,
        const Runtime::ProgressiveSlotSemantic semantic =
            Runtime::ProgressiveSlotSemantic::Albedo)
    {
        return Runtime::DerivedJobKey{
            .EntityId = entityId,
            .Domain = Runtime::ProgressiveGeometryDomain::MeshVertex,
            .OutputSemantic = semantic,
            .EntityGeneration = 1u,
            .GeometryGeneration = 1u,
            .SourcePropertyGeneration = 1u,
            .BindingGeneration = 1u,
            .OutputName = std::string{Runtime::ToString(semantic)},
        };
    }

    [[nodiscard]] RHI::BufferDesc BufferDesc(const std::uint64_t size,
                                             const RHI::BufferUsage usage)
    {
        return RHI::BufferDesc{
            .SizeBytes = size,
            .Usage = usage,
            .HostVisible = false,
            .DebugName = "GpuReadbackJobTest.Buffer",
        };
    }

    template <class T>
    [[nodiscard]] std::vector<std::byte> BytesOf(std::span<const T> values)
    {
        const std::span<const std::byte> bytes = std::as_bytes(values);
        return std::vector<std::byte>{bytes.begin(), bytes.end()};
    }

    class MockTransferQueue final : public RHI::ITransferQueue
    {
    public:
        [[nodiscard]] RHI::BufferHandle AddBuffer(std::vector<std::byte> bytes,
                                                  RHI::BufferUsage usage =
                                                      RHI::BufferUsage::TransferSrc)
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

        [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle,
                                                      const void*,
                                                      std::uint64_t,
                                                      std::uint64_t) override
        {
            return {};
        }

        [[nodiscard]] RHI::TransferToken UploadBuffer(RHI::BufferHandle,
                                                      std::span<const std::byte>,
                                                      std::uint64_t) override
        {
            return {};
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
            return !token.IsValid();
        }

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
            {
                return nullptr;
            }
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
            Barriers.push_back(BufferBarrierEvent{
                .Buffer = buffer,
                .Before = before,
                .After = after,
            });
        }

        void SubmitBarriers(const RHI::BarrierBatchDesc& batch) override
        {
            for (const RHI::BufferBarrierDesc& barrier : batch.BufferBarriers)
            {
                BufferBarrier(barrier.Buffer, barrier.BeforeAccess, barrier.AfterAccess);
            }
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
        Runtime::StreamingExecutor Executor{};
        Runtime::DerivedJobRegistry Jobs{Executor};
    };

    [[nodiscard]] Runtime::GpuReadbackJobDesc MakeFloatReadback(
        Harness& harness,
        Geometry::PropertySet& properties,
        const RHI::BufferHandle source,
        const RHI::BufferDesc& sourceDesc,
        const std::uint64_t byteCount)
    {
        return Runtime::GpuReadbackJobDesc{
            .Key = MakeKey(10u),
            .Name = "read back scalar field",
            .Transfer = &harness.Transfer,
            .CommandContext = &harness.CommandContext,
            .Source = source,
            .SourceDesc = sourceDesc,
            .SourceAccess = RHI::MemoryAccess::ShaderWrite,
            .Binding = Runtime::GpuReadbackPropertyBinding{
                .TargetProperties = &properties.Registry(),
                .TargetProperty = "v:height",
                .TargetType = Runtime::GpuReadbackPropertyType::Float32,
                .SourceRange = RHI::BufferRange{.OffsetBytes = 0u, .SizeBytes = byteCount},
            },
        };
    }
}

TEST(GpuReadbackJob, IncompleteReadbackParksAndAppliesOnlyAfterDelivery)
{
    Harness harness;
    Geometry::PropertySet properties;
    properties.Resize(4u);
    auto target = properties.Add<float>("v:height", 0.0f);
    ASSERT_TRUE(target);

    const std::array sourceValues{1.0f, 2.0f, 3.0f, 4.0f};
    const RHI::BufferHandle source =
        harness.Queue.AddBuffer(BytesOf<float>(std::span<const float>{sourceValues}),
                                RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc);
    const RHI::BufferDesc* sourceDesc = harness.Queue.GetDesc(source);
    ASSERT_NE(sourceDesc, nullptr);

    std::uint32_t applyCount = 0u;
    auto desc = MakeFloatReadback(
        harness,
        properties,
        source,
        *sourceDesc,
        static_cast<std::uint64_t>(sourceValues.size() * sizeof(float)));
    desc.ApplyAfterWrite = [&applyCount](Runtime::DerivedJobApplyContext&) -> Core::Result
    {
        ++applyCount;
        return Core::Ok();
    };

    const Runtime::DerivedJobHandle handle =
        Runtime::SubmitGpuReadbackJob(harness.Jobs, std::move(desc));
    ASSERT_TRUE(handle.IsValid());

    harness.Jobs.Pump(1u);
    harness.Jobs.DrainCompletions();
    ASSERT_EQ(harness.CommandContext.Barriers.size(), 1u);
    EXPECT_EQ(harness.CommandContext.Barriers[0].Buffer, source);
    EXPECT_EQ(harness.CommandContext.Barriers[0].Before, RHI::MemoryAccess::ShaderWrite);
    EXPECT_EQ(harness.CommandContext.Barriers[0].After, RHI::MemoryAccess::TransferRead);

    auto waiting = harness.Jobs.Snapshot(handle);
    ASSERT_TRUE(waiting.has_value());
    EXPECT_TRUE(waiting->IsReadbackJob);
    EXPECT_EQ(waiting->Status, Runtime::DerivedJobStatus::Applying);
    EXPECT_EQ(waiting->ExecutionState, Runtime::StreamingTaskState::WaitingForReadback);

    const Runtime::DerivedJobQueueSnapshot queued = harness.Jobs.SnapshotAll();
    EXPECT_EQ(queued.Readbacks.Issued, 1u);
    EXPECT_EQ(queued.Readbacks.Waiting, 1u);
    EXPECT_EQ(queued.Readbacks.Completed, 0u);

    harness.Jobs.ApplyMainThreadResults();
    EXPECT_EQ(applyCount, 0u);
    for (float value : target.Vector())
    {
        EXPECT_EQ(value, 0.0f);
    }

    harness.Queue.CollectCompleted();
    harness.Transfer.DrainCompleted(harness.CommandContext);
    harness.Jobs.DrainReadbacks();

    auto ready = harness.Jobs.Snapshot(handle);
    ASSERT_TRUE(ready.has_value());
    EXPECT_EQ(ready->ExecutionState, Runtime::StreamingTaskState::WaitingForMainThreadApply);

    harness.Jobs.ApplyMainThreadResults();

    EXPECT_EQ(applyCount, 1u);
    ASSERT_EQ(target.Vector().size(), sourceValues.size());
    for (std::size_t i = 0; i < sourceValues.size(); ++i)
    {
        EXPECT_FLOAT_EQ(target.Vector()[i], sourceValues[i]);
    }
    EXPECT_EQ(harness.Jobs.GetStatus(handle), Runtime::DerivedJobStatus::Complete);
    EXPECT_EQ(harness.Jobs.SnapshotAll().Readbacks.Completed, 1u);
}

TEST(GpuReadbackJob, PropertyBindingFailsClosedOnDimensionMismatch)
{
    Geometry::PropertySet properties;
    properties.Resize(3u);
    ASSERT_TRUE(properties.Add<float>("v:height", 0.0f));

    const Runtime::GpuReadbackPropertyBinding binding{
        .TargetProperties = &properties.Registry(),
        .TargetProperty = "v:height",
        .TargetType = Runtime::GpuReadbackPropertyType::Float32,
        .SourceRange = RHI::BufferRange{.OffsetBytes = 0u, .SizeBytes = 2u * sizeof(float)},
    };

    const Core::Result validation = Runtime::ValidateGpuReadbackPropertyBinding(binding);
    ASSERT_FALSE(validation.has_value());
    EXPECT_EQ(validation.error(), Core::ErrorCode::TypeMismatch);
}

TEST(GpuReadbackJob, FollowUpStaysPendingUntilReadbackApplies)
{
    Harness harness;
    Geometry::PropertySet properties;
    properties.Resize(2u);
    ASSERT_TRUE(properties.Add<float>("v:height", 0.0f));

    const std::array sourceValues{5.0f, 7.0f};
    const RHI::BufferHandle source =
        harness.Queue.AddBuffer(BytesOf<float>(std::span<const float>{sourceValues}),
                                RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc);
    const RHI::BufferDesc* sourceDesc = harness.Queue.GetDesc(source);
    ASSERT_NE(sourceDesc, nullptr);

    const Runtime::DerivedJobHandle readback =
        Runtime::SubmitGpuReadbackJob(
            harness.Jobs,
            MakeFloatReadback(
                harness,
                properties,
                source,
                *sourceDesc,
                static_cast<std::uint64_t>(sourceValues.size() * sizeof(float))));
    ASSERT_TRUE(readback.IsValid());

    std::vector<std::string> order{};
    Runtime::DerivedJobDesc followUp{
        .Key = MakeKey(10u, Runtime::ProgressiveSlotSemantic::Albedo),
        .Name = "derive color from readback",
        .Execute = [&order]() -> Runtime::DerivedJobWorkerResult
        {
            order.push_back("follow-worker");
            return Runtime::DerivedJobOutput{.PayloadToken = 99u};
        },
        .ApplyOnMainThread = [&order](Runtime::DerivedJobApplyContext&) -> Core::Result
        {
            order.push_back("follow-apply");
            return Core::Ok();
        },
    };
    const Runtime::DerivedJobHandle follow =
        harness.Jobs.SubmitFollowUp(readback, std::move(followUp), "readback property ready");
    ASSERT_TRUE(follow.IsValid());

    harness.Jobs.Pump(1u);
    harness.Jobs.DrainCompletions();
    EXPECT_EQ(harness.Jobs.Snapshot(readback)->ExecutionState,
              Runtime::StreamingTaskState::WaitingForReadback);

    harness.Jobs.Pump(1u);
    harness.Jobs.DrainCompletions();
    EXPECT_TRUE(order.empty());
    EXPECT_EQ(harness.Jobs.Snapshot(follow)->Status, Runtime::DerivedJobStatus::Blocked);

    harness.Queue.CollectCompleted();
    harness.Transfer.DrainCompleted(harness.CommandContext);
    harness.Jobs.DrainReadbacks();
    harness.Jobs.ApplyMainThreadResults();

    harness.Jobs.Pump(1u);
    harness.Jobs.DrainCompletions();
    harness.Jobs.ApplyMainThreadResults();

    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], "follow-worker");
    EXPECT_EQ(order[1], "follow-apply");
    EXPECT_EQ(harness.Jobs.GetStatus(readback), Runtime::DerivedJobStatus::Complete);
    EXPECT_EQ(harness.Jobs.GetStatus(follow), Runtime::DerivedJobStatus::Complete);
}

TEST(GpuReadbackJob, CancelledParkedReadbackDoesNotWriteProperty)
{
    Harness harness;
    Geometry::PropertySet properties;
    properties.Resize(1u);
    auto target = properties.Add<float>("v:height", 0.0f);
    ASSERT_TRUE(target);

    const std::array sourceValues{42.0f};
    const RHI::BufferHandle source =
        harness.Queue.AddBuffer(BytesOf<float>(std::span<const float>{sourceValues}),
                                RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc);
    const RHI::BufferDesc* sourceDesc = harness.Queue.GetDesc(source);
    ASSERT_NE(sourceDesc, nullptr);

    const Runtime::DerivedJobHandle handle =
        Runtime::SubmitGpuReadbackJob(
            harness.Jobs,
            MakeFloatReadback(harness, properties, source, *sourceDesc, sizeof(float)));
    ASSERT_TRUE(handle.IsValid());

    harness.Jobs.Pump(1u);
    harness.Jobs.DrainCompletions();
    EXPECT_EQ(harness.Jobs.Snapshot(handle)->ExecutionState,
              Runtime::StreamingTaskState::WaitingForReadback);

    harness.Jobs.Cancel(handle);
    harness.Queue.CollectCompleted();
    harness.Transfer.DrainCompleted(harness.CommandContext);
    harness.Jobs.DrainReadbacks();
    harness.Jobs.ApplyMainThreadResults();

    EXPECT_EQ(target.Vector()[0], 0.0f);
    EXPECT_EQ(harness.Jobs.GetStatus(handle), Runtime::DerivedJobStatus::Cancelled);
    const Runtime::DerivedJobQueueSnapshot snapshot = harness.Jobs.SnapshotAll();
    EXPECT_EQ(snapshot.Readbacks.Issued, 1u);
    EXPECT_EQ(snapshot.Readbacks.StaleOrCancelled, 1u);
}
