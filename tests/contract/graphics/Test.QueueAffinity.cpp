#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

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
    RHI::ITransferQueue* transferQueue = &device.GetMockTransferQueueForAffinity();
    EXPECT_EQ(transferQueue, static_cast<RHI::ITransferQueue*>(&device.TransferQueue));
}
