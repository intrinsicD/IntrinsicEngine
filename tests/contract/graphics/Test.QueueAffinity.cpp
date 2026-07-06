#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

import Extrinsic.Backends.Null;
import Extrinsic.Graphics.RenderGraph;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.QueueAffinity;
import Extrinsic.RHI.TransferQueue;

#include "MockRHI.hpp"

namespace
{
    using namespace Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;

    [[nodiscard]] std::vector<std::uint32_t> PassIndices(const std::vector<QueuePartitionedPass>& passes)
    {
        std::vector<std::uint32_t> indices{};
        indices.reserve(passes.size());
        for (const QueuePartitionedPass& pass : passes)
        {
            indices.push_back(pass.PassIndex);
        }
        return indices;
    }

    [[nodiscard]] CompiledRenderGraph CompileSubmitPlanGraph()
    {
        RenderGraph graph;
        const TextureRef first = graph.CreateTexture("First", RHI::TextureDesc{});
        const TextureRef second = graph.CreateTexture("Second", RHI::TextureDesc{});

        (void)graph.AddPass("GraphicsProducer", [first](RenderGraphBuilder& builder) {
            builder.SetQueue(RenderQueue::Graphics);
            (void)builder.Write(first, TextureUsage::ShaderWrite);
        });
        (void)graph.AddPass("AsyncMiddle", [first, second](RenderGraphBuilder& builder) {
            builder.SetQueue(RenderQueue::AsyncCompute);
            (void)builder.Read(first, TextureUsage::ShaderRead);
            (void)builder.Write(second, TextureUsage::ShaderWrite);
        });
        (void)graph.AddPass("GraphicsConsumer", [second](RenderGraphBuilder& builder) {
            builder.SetQueue(RenderQueue::Graphics);
            (void)builder.Read(second, TextureUsage::ShaderRead);
            builder.SideEffect();
        });

        auto compiled = graph.Compile();
        EXPECT_TRUE(compiled.has_value());
        return compiled.has_value() ? std::move(*compiled) : CompiledRenderGraph{};
    }
}

TEST(GraphicsQueueAffinity, ResolveQueueAffinityDemotesMissingOptionalQueues)
{
    const RHI::QueueCapabilityProfile singleQueue{};

    const RHI::QueueAffinityResolution graphics =
        RHI::ResolveQueueAffinity(RHI::QueueAffinity::Graphics, singleQueue);
    EXPECT_EQ(graphics.Requested, RHI::QueueAffinity::Graphics);
    EXPECT_EQ(graphics.Resolved, RHI::QueueAffinity::Graphics);
    EXPECT_FALSE(graphics.Demoted);

    const RHI::QueueAffinityResolution asyncCompute =
        RHI::ResolveQueueAffinity(RHI::QueueAffinity::AsyncCompute, singleQueue);
    EXPECT_EQ(asyncCompute.Requested, RHI::QueueAffinity::AsyncCompute);
    EXPECT_EQ(asyncCompute.Resolved, RHI::QueueAffinity::Graphics);
    EXPECT_TRUE(asyncCompute.Demoted);

    const RHI::QueueAffinityResolution transfer =
        RHI::ResolveQueueAffinity(RHI::QueueAffinity::Transfer, singleQueue);
    EXPECT_EQ(transfer.Requested, RHI::QueueAffinity::Transfer);
    EXPECT_EQ(transfer.Resolved, RHI::QueueAffinity::Graphics);
    EXPECT_TRUE(transfer.Demoted);

    const RHI::QueueCapabilityProfile fullProfile{
        .SupportsAsyncCompute = true,
        .SupportsTransfer = true,
    };
    EXPECT_EQ(RHI::ResolveQueueAffinity(RHI::QueueAffinity::AsyncCompute, fullProfile).Resolved,
              RHI::QueueAffinity::AsyncCompute);
    EXPECT_FALSE(RHI::ResolveQueueAffinity(RHI::QueueAffinity::AsyncCompute, fullProfile).Demoted);
    EXPECT_EQ(RHI::ResolveQueueAffinity(RHI::QueueAffinity::Transfer, fullProfile).Resolved,
              RHI::QueueAffinity::Transfer);
    EXPECT_FALSE(RHI::ResolveQueueAffinity(RHI::QueueAffinity::Transfer, fullProfile).Demoted);
}

TEST(GraphicsQueueAffinity, PartitionPassesByQueueUsesStableRankThenPassIndexOrdering)
{
    const std::array<RenderQueue, 5u> passQueues{
        RenderQueue::Graphics,
        RenderQueue::AsyncCompute,
        RenderQueue::Transfer,
        RenderQueue::AsyncCompute,
        RenderQueue::Graphics,
    };
    const std::array<std::uint32_t, 5u> livePasses{2u, 1u, 0u, 4u, 3u};
    const std::array<std::uint32_t, 5u> topologicalRankByPass{0u, 0u, 0u, 1u, 1u};
    const RHI::QueueCapabilityProfile fullProfile{
        .SupportsAsyncCompute = true,
        .SupportsTransfer = true,
    };

    const QueuePartition partition =
        PartitionPassesByQueue(passQueues, livePasses, topologicalRankByPass, fullProfile);

    EXPECT_EQ(PassIndices(partition.Graphics), (std::vector<std::uint32_t>{0u, 4u}));
    EXPECT_EQ(PassIndices(partition.AsyncCompute), (std::vector<std::uint32_t>{1u, 3u}));
    EXPECT_EQ(PassIndices(partition.Transfer), (std::vector<std::uint32_t>{2u}));
    EXPECT_EQ(partition.QueueAffinityDemotedCount, 0u);
    ASSERT_EQ(partition.Graphics.size(), 2u);
    EXPECT_EQ(partition.Graphics[0].TopologicalRank, 0u);
    EXPECT_EQ(partition.Graphics[1].TopologicalRank, 1u);
}

TEST(GraphicsQueueAffinity, MissingCapabilitiesDemoteIntoGraphicsBucketAndCountDemotions)
{
    CompiledRenderGraph compiled{};
    compiled.PassQueues = {
        RenderQueue::Graphics,
        RenderQueue::AsyncCompute,
        RenderQueue::Transfer,
        RenderQueue::AsyncCompute,
        RenderQueue::Graphics,
    };
    compiled.TopologicalOrder = {2u, 1u, 0u, 4u, 3u};
    compiled.TopologicalLayerByPass = {0u, 0u, 0u, 1u, 1u};

    const QueuePartition partition = PartitionPassesByQueue(compiled, RHI::QueueCapabilityProfile{});

    EXPECT_EQ(PassIndices(partition.Graphics), (std::vector<std::uint32_t>{0u, 1u, 2u, 3u, 4u}));
    EXPECT_TRUE(partition.AsyncCompute.empty());
    EXPECT_TRUE(partition.Transfer.empty());
    EXPECT_EQ(partition.QueueAffinityDemotedCount, 3u);
    ASSERT_EQ(partition.Graphics.size(), 5u);
    EXPECT_EQ(partition.Graphics[0].Requested, RenderQueue::Graphics);
    EXPECT_EQ(partition.Graphics[0].Resolved, RenderQueue::Graphics);
    EXPECT_FALSE(partition.Graphics[0].Demoted);
    EXPECT_EQ(partition.Graphics[4].Requested, RenderQueue::Graphics);
    EXPECT_EQ(partition.Graphics[4].Resolved, RenderQueue::Graphics);
    EXPECT_FALSE(partition.Graphics[4].Demoted);
}

TEST(GraphicsQueueAffinity, MockDeviceExposesToggleableOptionalQueues)
{
    Extrinsic::Tests::MockDevice device;

    EXPECT_TRUE(device.SupportsMockQueue(RHI::QueueAffinity::Graphics));
    EXPECT_FALSE(device.SupportsMockQueue(RHI::QueueAffinity::AsyncCompute));
    EXPECT_FALSE(device.SupportsMockQueue(RHI::QueueAffinity::Transfer));
    EXPECT_FALSE(device.GetQueueCapabilityProfile().SupportsAsyncCompute);
    EXPECT_FALSE(device.GetQueueCapabilityProfile().SupportsTransfer);

    RHI::ICommandContext* graphicsContext = &device.GetMockQueueContext(RHI::QueueAffinity::Graphics);
    EXPECT_EQ(graphicsContext, static_cast<RHI::ICommandContext*>(&device.CommandContext));
    RHI::ICommandContext* fallbackContext = &device.GetMockQueueContext(RHI::QueueAffinity::AsyncCompute);
    EXPECT_EQ(fallbackContext, static_cast<RHI::ICommandContext*>(&device.CommandContext));

    device.SetQueueCapabilityProfile(RHI::QueueCapabilityProfile{
        .SupportsAsyncCompute = true,
        .SupportsTransfer = true,
    });

    EXPECT_TRUE(device.SupportsMockQueue(RHI::QueueAffinity::AsyncCompute));
    EXPECT_TRUE(device.SupportsMockQueue(RHI::QueueAffinity::Transfer));
    EXPECT_TRUE(device.GetQueueCapabilityProfile().SupportsAsyncCompute);
    EXPECT_TRUE(device.GetQueueCapabilityProfile().SupportsTransfer);
    RHI::ICommandContext* asyncContext = &device.GetMockQueueContext(RHI::QueueAffinity::AsyncCompute);
    EXPECT_EQ(asyncContext, static_cast<RHI::ICommandContext*>(&device.AsyncComputeContext));
    RHI::ICommandContext* asyncInterfaceContext =
        &device.GetQueueContext(RHI::QueueAffinity::AsyncCompute, 0u);
    EXPECT_EQ(asyncInterfaceContext, static_cast<RHI::ICommandContext*>(&device.AsyncComputeContext));
    RHI::ICommandContext* transferInterfaceContext =
        &device.GetQueueContext(RHI::QueueAffinity::Transfer, 0u);
    EXPECT_EQ(transferInterfaceContext, static_cast<RHI::ICommandContext*>(&device.TransferContext));
    RHI::ITransferQueue* transferQueue = &device.GetMockTransferQueueForAffinity();
    EXPECT_EQ(transferQueue, static_cast<RHI::ITransferQueue*>(&device.TransferQueue));
}

TEST(GraphicsQueueAffinity, MockDeviceRecordsFrameQueueSubmitPlanAndBatchContexts)
{
    const CompiledRenderGraph compiled = CompileSubmitPlanGraph();
    const QueueSubmitPlan plan = BuildQueueSubmitPlan(
        compiled,
        RHI::QueueCapabilityProfile{
            .SupportsAsyncCompute = true,
            .SupportsTransfer = true,
        });
    ASSERT_EQ(plan.Batches.size(), 3u);

    std::vector<std::vector<RHI::QueueTimelineWaitDesc>> waits(plan.Batches.size());
    std::vector<std::vector<RHI::QueueTimelineSignalDesc>> signals(plan.Batches.size());
    std::vector<RHI::QueueSubmitBatchDesc> batches{};
    batches.reserve(plan.Batches.size());
    for (std::size_t batchIndex = 0; batchIndex < plan.Batches.size(); ++batchIndex)
    {
        const QueueSubmitBatch& batch = plan.Batches[batchIndex];
        for (const QueueSubmitTimelineWait& wait : batch.Waits)
        {
            waits[batchIndex].push_back(RHI::QueueTimelineWaitDesc{
                .Queue = wait.Queue,
                .SignalQueue = wait.SignalQueue,
                .Value = wait.Value,
            });
        }
        for (const QueueSubmitTimelineSignal& signal : batch.Signals)
        {
            signals[batchIndex].push_back(RHI::QueueTimelineSignalDesc{
                .Queue = signal.Queue,
                .Value = signal.Value,
            });
        }
        batches.push_back(RHI::QueueSubmitBatchDesc{
            .Queue = batch.Queue,
            .Waits = waits[batchIndex],
            .Signals = signals[batchIndex],
        });
    }

    Extrinsic::Tests::MockDevice device;
    device.AcceptQueueSubmitPlans = true;
    device.SetQueueCapabilityProfile(RHI::QueueCapabilityProfile{
        .SupportsAsyncCompute = true,
        .SupportsTransfer = true,
    });

    EXPECT_TRUE(device.BeginFrameQueueSubmitPlan(RHI::FrameHandle{.FrameIndex = 2u},
                                                 RHI::FrameQueueSubmitPlanDesc{.Batches = batches}));
    ASSERT_EQ(device.RecordedQueueSubmitPlan.size(), 3u);
    EXPECT_EQ(device.RecordedQueueSubmitPlan[0].Queue, RHI::QueueAffinity::Graphics);
    EXPECT_EQ(device.RecordedQueueSubmitPlan[1].Queue, RHI::QueueAffinity::AsyncCompute);
    EXPECT_EQ(device.RecordedQueueSubmitPlan[2].Queue, RHI::QueueAffinity::Graphics);
    ASSERT_EQ(device.RecordedQueueSubmitPlan[1].Waits.size(), 1u);
    EXPECT_EQ(device.RecordedQueueSubmitPlan[1].Waits[0].SignalQueue, RHI::QueueAffinity::Graphics);
    ASSERT_EQ(device.RecordedQueueSubmitPlan[1].Signals.size(), 1u);
    EXPECT_EQ(device.RecordedQueueSubmitPlan[1].Signals[0].Queue, RHI::QueueAffinity::AsyncCompute);

    RHI::ICommandContext* graphics0 =
        &device.GetQueueSubmitContext(RHI::QueueAffinity::Graphics, 2u, 0u);
    RHI::ICommandContext* async =
        &device.GetQueueSubmitContext(RHI::QueueAffinity::AsyncCompute, 2u, 1u);
    RHI::ICommandContext* graphics1 =
        &device.GetQueueSubmitContext(RHI::QueueAffinity::Graphics, 2u, 2u);
    EXPECT_EQ(graphics0, static_cast<RHI::ICommandContext*>(&device.CommandContext));
    EXPECT_EQ(async, static_cast<RHI::ICommandContext*>(&device.AsyncComputeContext));
    EXPECT_EQ(graphics1, static_cast<RHI::ICommandContext*>(&device.CommandContext));
    ASSERT_EQ(device.QueueSubmitContextRequests.size(), 3u);
    EXPECT_EQ(device.QueueSubmitContextRequests[1].BatchIndex, 1u);
}

TEST(GraphicsQueueAffinity, NullDeviceProvidesDistinctParallelCommandContexts)
{
    std::unique_ptr<RHI::IDevice> device = Extrinsic::Backends::Null::CreateNullDevice();
    ASSERT_NE(device, nullptr);
    EXPECT_TRUE(device->SupportsParallelCommandContexts());

    const RHI::FrameHandle frame{.FrameIndex = 0u, .SwapchainImageIndex = 0u};
    const std::array<RHI::ParallelCommandContextRequest, 2u> requests{{
        RHI::ParallelCommandContextRequest{
            .Queue = RHI::QueueAffinity::Graphics,
            .FrameIndex = frame.FrameIndex,
            .PassIndex = 3u,
            .TopologicalLayer = 0u,
            .ContextIndex = 0u,
        },
        RHI::ParallelCommandContextRequest{
            .Queue = RHI::QueueAffinity::Graphics,
            .FrameIndex = frame.FrameIndex,
            .PassIndex = 7u,
            .TopologicalLayer = 0u,
            .ContextIndex = 1u,
        },
    }};

    ASSERT_TRUE(device->BeginFrameParallelCommandContexts(
        frame,
        RHI::ParallelCommandContextPlanDesc{.Requests = requests}));
    RHI::ICommandContext* primary = &device->GetGraphicsContext(frame.FrameIndex);
    RHI::ICommandContext* first = &device->GetParallelCommandContext(requests[0]);
    RHI::ICommandContext* second = &device->GetParallelCommandContext(requests[1]);
    EXPECT_NE(first, primary);
    EXPECT_NE(second, primary);
    EXPECT_NE(first, second);

    device->SubmitParallelCommandContext(requests[0], *primary);
    device->SubmitParallelCommandContext(requests[1], *primary);
    device->EndFrameParallelCommandContexts(frame);
}

TEST(GraphicsQueueAffinity, MockDeviceRecordsParallelCommandContextPlanAndSubmitOrder)
{
    Extrinsic::Tests::MockDevice device;
    device.ParallelCommandContextsAvailable = true;
    device.AcceptParallelCommandContextPlans = true;

    const RHI::FrameHandle frame{.FrameIndex = 5u, .SwapchainImageIndex = 0u};
    const std::array<RHI::ParallelCommandContextRequest, 3u> requests{{
        RHI::ParallelCommandContextRequest{
            .Queue = RHI::QueueAffinity::Graphics,
            .FrameIndex = frame.FrameIndex,
            .PassIndex = 0u,
            .TopologicalLayer = 0u,
            .ContextIndex = 0u,
        },
        RHI::ParallelCommandContextRequest{
            .Queue = RHI::QueueAffinity::Graphics,
            .FrameIndex = frame.FrameIndex,
            .PassIndex = 1u,
            .TopologicalLayer = 0u,
            .ContextIndex = 1u,
        },
        RHI::ParallelCommandContextRequest{
            .Queue = RHI::QueueAffinity::Graphics,
            .FrameIndex = frame.FrameIndex,
            .PassIndex = 2u,
            .TopologicalLayer = 1u,
            .ContextIndex = 2u,
        },
    }};

    ASSERT_TRUE(device.SupportsParallelCommandContexts());
    ASSERT_TRUE(device.BeginFrameParallelCommandContexts(
        frame,
        RHI::ParallelCommandContextPlanDesc{.Requests = requests}));
    EXPECT_EQ(device.RecordedParallelCommandContextPlan.size(), requests.size());

    RHI::ICommandContext* first = &device.GetParallelCommandContext(requests[0]);
    RHI::ICommandContext* second = &device.GetParallelCommandContext(requests[1]);
    RHI::ICommandContext* third = &device.GetParallelCommandContext(requests[2]);
    EXPECT_NE(first, second);
    EXPECT_NE(second, third);
    EXPECT_EQ(device.ParallelCommandContextRequests.size(), requests.size());

    device.SubmitParallelCommandContext(requests[0], device.CommandContext);
    device.SubmitParallelCommandContext(requests[1], device.CommandContext);
    device.SubmitParallelCommandContext(requests[2], device.CommandContext);
    ASSERT_EQ(device.SubmittedParallelCommandContexts.size(), requests.size());
    EXPECT_EQ(device.SubmittedParallelCommandContexts[0].PassIndex, 0u);
    EXPECT_EQ(device.SubmittedParallelCommandContexts[1].PassIndex, 1u);
    EXPECT_EQ(device.SubmittedParallelCommandContexts[2].PassIndex, 2u);

    device.EndFrameParallelCommandContexts(frame);
    EXPECT_TRUE(device.ParallelCommandContexts.empty());
}

TEST(GraphicsQueueAffinity, BuildQueueSubmitPlanCreatesBatchesAndTimelineBoundaries)
{
    const CompiledRenderGraph compiled = CompileSubmitPlanGraph();
    ASSERT_EQ(compiled.CrossQueueTimelineEdgeCount, 2u);

    const QueueSubmitPlan plan = BuildQueueSubmitPlan(
        compiled,
        RHI::QueueCapabilityProfile{
            .SupportsAsyncCompute = true,
            .SupportsTransfer = true,
        });

    ASSERT_EQ(plan.Batches.size(), 3u);
    EXPECT_EQ(plan.QueueAffinityDemotedCount, 0u);
    EXPECT_EQ(plan.OmittedSameQueueTimelineEdgeCount, 0u);

    EXPECT_EQ(plan.Batches[0].Queue, RenderQueue::Graphics);
    EXPECT_EQ(plan.Batches[0].PassIndices, (std::vector<std::uint32_t>{0u}));
    ASSERT_EQ(plan.Batches[0].Signals.size(), 1u);
    EXPECT_EQ(plan.Batches[0].Signals[0].PassIndex, 0u);
    EXPECT_EQ(plan.Batches[0].Signals[0].RequestedQueue, RenderQueue::Graphics);
    EXPECT_EQ(plan.Batches[0].Signals[0].Queue, RenderQueue::Graphics);
    EXPECT_EQ(plan.Batches[0].Signals[0].Value, 1u);

    EXPECT_EQ(plan.Batches[1].Queue, RenderQueue::AsyncCompute);
    EXPECT_EQ(plan.Batches[1].PassIndices, (std::vector<std::uint32_t>{1u}));
    ASSERT_EQ(plan.Batches[1].Waits.size(), 1u);
    EXPECT_EQ(plan.Batches[1].Waits[0].PassIndex, 1u);
    EXPECT_EQ(plan.Batches[1].Waits[0].Queue, RenderQueue::AsyncCompute);
    EXPECT_EQ(plan.Batches[1].Waits[0].SignalPassIndex, 0u);
    EXPECT_EQ(plan.Batches[1].Waits[0].SignalQueue, RenderQueue::Graphics);
    EXPECT_EQ(plan.Batches[1].Waits[0].Value, 1u);
    ASSERT_EQ(plan.Batches[1].Signals.size(), 1u);
    EXPECT_EQ(plan.Batches[1].Signals[0].PassIndex, 1u);
    EXPECT_EQ(plan.Batches[1].Signals[0].RequestedQueue, RenderQueue::AsyncCompute);
    EXPECT_EQ(plan.Batches[1].Signals[0].Queue, RenderQueue::AsyncCompute);
    EXPECT_EQ(plan.Batches[1].Signals[0].Value, 1u);

    EXPECT_EQ(plan.Batches[2].Queue, RenderQueue::Graphics);
    EXPECT_EQ(plan.Batches[2].PassIndices, (std::vector<std::uint32_t>{2u}));
    ASSERT_EQ(plan.Batches[2].Waits.size(), 1u);
    EXPECT_EQ(plan.Batches[2].Waits[0].PassIndex, 2u);
    EXPECT_EQ(plan.Batches[2].Waits[0].Queue, RenderQueue::Graphics);
    EXPECT_EQ(plan.Batches[2].Waits[0].SignalPassIndex, 1u);
    EXPECT_EQ(plan.Batches[2].Waits[0].SignalQueue, RenderQueue::AsyncCompute);
    EXPECT_EQ(plan.Batches[2].Waits[0].Value, 1u);
}

TEST(GraphicsQueueAffinity, BuildQueueSubmitPlanDemotesMissingOptionalQueues)
{
    const CompiledRenderGraph compiled = CompileSubmitPlanGraph();
    ASSERT_EQ(compiled.CrossQueueTimelineEdgeCount, 2u);

    const QueueSubmitPlan plan = BuildQueueSubmitPlan(compiled, RHI::QueueCapabilityProfile{});

    ASSERT_EQ(plan.Batches.size(), 1u);
    EXPECT_EQ(plan.Batches[0].Queue, RenderQueue::Graphics);
    EXPECT_EQ(plan.Batches[0].PassIndices, (std::vector<std::uint32_t>{0u, 1u, 2u}));
    EXPECT_TRUE(plan.Batches[0].Waits.empty());
    EXPECT_TRUE(plan.Batches[0].Signals.empty());
    EXPECT_EQ(plan.QueueAffinityDemotedCount, 1u);
    EXPECT_EQ(plan.OmittedSameQueueTimelineEdgeCount, 2u);
}
