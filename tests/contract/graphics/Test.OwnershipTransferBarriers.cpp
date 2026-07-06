#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

import Extrinsic.Core.Error;
import Extrinsic.Graphics.RenderGraph;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.QueueAffinity;

namespace
{
    using namespace Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;

    struct TextureTransferRecord
    {
        std::uint32_t PassIndex = 0;
        BarrierPacketStage Stage = BarrierPacketStage::BeforePass;
        TextureBarrierPacket Barrier{};
    };

    struct BufferTransferRecord
    {
        std::uint32_t PassIndex = 0;
        BarrierPacketStage Stage = BarrierPacketStage::BeforePass;
        BufferBarrierPacket Barrier{};
    };

    [[nodiscard]] std::vector<TextureTransferRecord> TextureTransfers(const CompiledRenderGraph& compiled,
                                                                      const std::uint32_t textureIndex)
    {
        std::vector<TextureTransferRecord> records{};
        for (const BarrierPacket& packet : compiled.BarrierPackets)
        {
            for (const TextureBarrierPacket& barrier : packet.TextureBarriers)
            {
                if (barrier.TextureIndex == textureIndex &&
                    barrier.OwnershipTransfer.Kind != QueueOwnershipTransferKind::None)
                {
                    records.push_back(TextureTransferRecord{
                        .PassIndex = packet.PassIndex,
                        .Stage = packet.Stage,
                        .Barrier = barrier,
                    });
                }
            }
        }
        return records;
    }

    [[nodiscard]] std::vector<BufferTransferRecord> BufferTransfers(const CompiledRenderGraph& compiled,
                                                                    const std::uint32_t bufferIndex)
    {
        std::vector<BufferTransferRecord> records{};
        for (const BarrierPacket& packet : compiled.BarrierPackets)
        {
            for (const BufferBarrierPacket& barrier : packet.BufferBarriers)
            {
                if (barrier.BufferIndex == bufferIndex &&
                    barrier.OwnershipTransfer.Kind != QueueOwnershipTransferKind::None)
                {
                    records.push_back(BufferTransferRecord{
                        .PassIndex = packet.PassIndex,
                        .Stage = packet.Stage,
                        .Barrier = barrier,
                    });
                }
            }
        }
        return records;
    }
}

TEST(GraphicsOwnershipTransferBarriers, BarrierPacketRangeUsesSortedPassStageWindow)
{
    const std::vector<BarrierPacket> packets{
        BarrierPacket{.PassIndex = 0u, .Stage = BarrierPacketStage::BeforePass},
        BarrierPacket{.PassIndex = 0u, .Stage = BarrierPacketStage::AfterPass},
        BarrierPacket{.PassIndex = 2u, .Stage = BarrierPacketStage::BeforePass},
        BarrierPacket{.PassIndex = 2u, .Stage = BarrierPacketStage::BeforePass},
        BarrierPacket{.PassIndex = 2u, .Stage = BarrierPacketStage::AfterPass},
        BarrierPacket{.PassIndex = 5u, .Stage = BarrierPacketStage::BeforePass},
    };

    ASSERT_TRUE(AreBarrierPacketsSortedByPassAndStage(packets));

    const BarrierPacketRange beforeRange =
        FindBarrierPacketRange(packets, 2u, BarrierPacketStage::BeforePass);
    EXPECT_EQ(beforeRange.Begin, 2u);
    EXPECT_EQ(beforeRange.End, 4u);

    const BarrierPacketRange afterRange =
        FindBarrierPacketRange(packets, 2u, BarrierPacketStage::AfterPass);
    EXPECT_EQ(afterRange.Begin, 4u);
    EXPECT_EQ(afterRange.End, 5u);

    const BarrierPacketRange missingRange =
        FindBarrierPacketRange(packets, 4u, BarrierPacketStage::BeforePass);
    EXPECT_TRUE(missingRange.Empty());
}

TEST(GraphicsOwnershipTransferBarriers, ExecutorRejectsUnsortedBarrierPackets)
{
    CompiledRenderGraph compiled{};
    compiled.TopologicalOrder = {0u, 1u};
    compiled.PassDeclarations.resize(2u);
    compiled.PassDeclarations[0].PassIndex = 0u;
    compiled.PassDeclarations[1].PassIndex = 1u;
    compiled.BarrierPackets = {
        BarrierPacket{.PassIndex = 1u, .Stage = BarrierPacketStage::BeforePass},
        BarrierPacket{.PassIndex = 0u, .Stage = BarrierPacketStage::BeforePass},
    };

    ASSERT_FALSE(AreBarrierPacketsSortedByPassAndStage(compiled.BarrierPackets));

    RenderGraphExecutor executor;
    const auto result = executor.Execute(compiled);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Extrinsic::Core::ErrorCode::InvalidState);
}

TEST(GraphicsOwnershipTransferBarriers, ImportedTextureCrossQueueEmitsReleaseAcquirePair)
{
    RenderGraph graph;
    const TextureRef history = graph.ImportTexture("History",
                                                   RHI::TextureHandle{42u, 1u},
                                                   TextureState::ShaderWrite,
                                                   TextureState::ShaderRead);

    const PassRef producer = graph.AddPass("HistoryWrite", [history](RenderGraphBuilder& builder) {
        builder.SetQueue(RenderQueue::Graphics);
        (void)builder.Write(history, TextureUsage::ShaderWrite);
        builder.SideEffect();
    });
    const PassRef consumer = graph.AddPass("HistoryRead", [history](RenderGraphBuilder& builder) {
        builder.SetQueue(RenderQueue::AsyncCompute);
        (void)builder.Read(history, TextureUsage::ShaderRead);
        builder.SideEffect();
    });

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    ASSERT_LT(history.Index, compiled->TextureQueueSharingModes.size());
    EXPECT_EQ(compiled->TextureQueueSharingModes[history.Index], QueueSharingMode::Exclusive);
    EXPECT_EQ(compiled->QueueHandoffEdgeCount, 1u);
    EXPECT_EQ(compiled->CrossQueueTimelineEdgeCount, 1u);
    EXPECT_EQ(compiled->CrossQueueOwnershipTransferCount, 1u);

    const std::vector<TextureTransferRecord> transfers = TextureTransfers(*compiled, history.Index);
    ASSERT_EQ(transfers.size(), 2u);

    const TextureTransferRecord& release = transfers[0];
    EXPECT_EQ(release.PassIndex, producer.Index);
    EXPECT_EQ(release.Stage, BarrierPacketStage::AfterPass);
    EXPECT_EQ(release.Barrier.Before, TextureBarrierState::ShaderWrite);
    EXPECT_EQ(release.Barrier.After, TextureBarrierState::ShaderRead);
    EXPECT_EQ(release.Barrier.SharingMode, QueueSharingMode::Exclusive);
    EXPECT_EQ(release.Barrier.OwnershipTransfer.Kind, QueueOwnershipTransferKind::Release);
    EXPECT_EQ(release.Barrier.OwnershipTransfer.SourceQueue, RenderQueue::Graphics);
    EXPECT_EQ(release.Barrier.OwnershipTransfer.DestinationQueue, RenderQueue::AsyncCompute);
    EXPECT_EQ(release.Barrier.OwnershipTransfer.SourceQueueFamily, QueueFamilyToken(RenderQueue::Graphics));
    EXPECT_EQ(release.Barrier.OwnershipTransfer.DestinationQueueFamily, QueueFamilyToken(RenderQueue::AsyncCompute));

    const TextureTransferRecord& acquire = transfers[1];
    EXPECT_EQ(acquire.PassIndex, consumer.Index);
    EXPECT_EQ(acquire.Stage, BarrierPacketStage::BeforePass);
    EXPECT_EQ(acquire.Barrier.Before, TextureBarrierState::ShaderWrite);
    EXPECT_EQ(acquire.Barrier.After, TextureBarrierState::ShaderRead);
    EXPECT_EQ(acquire.Barrier.SharingMode, QueueSharingMode::Exclusive);
    EXPECT_EQ(acquire.Barrier.OwnershipTransfer.Kind, QueueOwnershipTransferKind::Acquire);
    EXPECT_EQ(acquire.Barrier.OwnershipTransfer.SourceQueue, RenderQueue::Graphics);
    EXPECT_EQ(acquire.Barrier.OwnershipTransfer.DestinationQueue, RenderQueue::AsyncCompute);
    EXPECT_EQ(acquire.Barrier.OwnershipTransfer.SourceQueueFamily, QueueFamilyToken(RenderQueue::Graphics));
    EXPECT_EQ(acquire.Barrier.OwnershipTransfer.DestinationQueueFamily, QueueFamilyToken(RenderQueue::AsyncCompute));

    std::vector<std::string> events{};
    RenderGraphExecutor executor;
    const auto result = executor.Execute(
        *compiled,
        [&events](const std::uint32_t passIndex) {
            events.push_back("pass(" + std::to_string(passIndex) + ")");
        },
        [&events](const BarrierPacket& packet) {
            for (const TextureBarrierPacket& barrier : packet.TextureBarriers)
            {
                if (barrier.OwnershipTransfer.Kind == QueueOwnershipTransferKind::Release)
                {
                    events.push_back("release(" + std::to_string(packet.PassIndex) + ")");
                }
                if (barrier.OwnershipTransfer.Kind == QueueOwnershipTransferKind::Acquire)
                {
                    events.push_back("acquire(" + std::to_string(packet.PassIndex) + ")");
                }
            }
        });

    ASSERT_TRUE(result.has_value());
    const std::vector<std::string> expected{
        "pass(" + std::to_string(producer.Index) + ")",
        "release(" + std::to_string(producer.Index) + ")",
        "acquire(" + std::to_string(consumer.Index) + ")",
        "pass(" + std::to_string(consumer.Index) + ")",
    };
    EXPECT_EQ(events, expected);
}

TEST(GraphicsOwnershipTransferBarriers, ImportedBufferCrossQueueEmitsReleaseAcquirePair)
{
    RenderGraph graph;
    const BufferRef drawArgs = graph.ImportBuffer("DrawArgs",
                                                  RHI::BufferHandle{77u, 1u},
                                                  BufferState::ShaderWrite,
                                                  BufferState::IndirectRead);

    const PassRef producer = graph.AddPass("CullWrite", [drawArgs](RenderGraphBuilder& builder) {
        builder.SetQueue(RenderQueue::Graphics);
        (void)builder.Write(drawArgs, BufferUsage::ShaderWrite);
        builder.SideEffect();
    });
    const PassRef consumer = graph.AddPass("TransferRead", [drawArgs](RenderGraphBuilder& builder) {
        builder.SetQueue(RenderQueue::Transfer);
        (void)builder.Read(drawArgs, BufferUsage::IndirectRead);
        builder.SideEffect();
    });

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    ASSERT_LT(drawArgs.Index, compiled->BufferQueueSharingModes.size());
    EXPECT_EQ(compiled->BufferQueueSharingModes[drawArgs.Index], QueueSharingMode::Exclusive);
    EXPECT_EQ(compiled->CrossQueueOwnershipTransferCount, 1u);

    const std::vector<BufferTransferRecord> transfers = BufferTransfers(*compiled, drawArgs.Index);
    ASSERT_EQ(transfers.size(), 2u);
    EXPECT_EQ(transfers[0].PassIndex, producer.Index);
    EXPECT_EQ(transfers[0].Stage, BarrierPacketStage::AfterPass);
    EXPECT_EQ(transfers[0].Barrier.Before, BufferBarrierState::ShaderWrite);
    EXPECT_EQ(transfers[0].Barrier.After, BufferBarrierState::IndirectRead);
    EXPECT_EQ(transfers[0].Barrier.OwnershipTransfer.Kind, QueueOwnershipTransferKind::Release);
    EXPECT_EQ(transfers[0].Barrier.OwnershipTransfer.SourceQueueFamily, QueueFamilyToken(RenderQueue::Graphics));
    EXPECT_EQ(transfers[0].Barrier.OwnershipTransfer.DestinationQueueFamily, QueueFamilyToken(RenderQueue::Transfer));

    EXPECT_EQ(transfers[1].PassIndex, consumer.Index);
    EXPECT_EQ(transfers[1].Stage, BarrierPacketStage::BeforePass);
    EXPECT_EQ(transfers[1].Barrier.Before, BufferBarrierState::ShaderWrite);
    EXPECT_EQ(transfers[1].Barrier.After, BufferBarrierState::IndirectRead);
    EXPECT_EQ(transfers[1].Barrier.OwnershipTransfer.Kind, QueueOwnershipTransferKind::Acquire);
    EXPECT_EQ(transfers[1].Barrier.OwnershipTransfer.SourceQueueFamily, QueueFamilyToken(RenderQueue::Graphics));
    EXPECT_EQ(transfers[1].Barrier.OwnershipTransfer.DestinationQueueFamily, QueueFamilyToken(RenderQueue::Transfer));
}

TEST(GraphicsOwnershipTransferBarriers, ExplicitDependencyDoesNotSuppressCrossQueueTransferRecords)
{
    RenderGraph graph;
    const TextureRef history = graph.ImportTexture("History",
                                                   RHI::TextureHandle{88u, 1u},
                                                   TextureState::ShaderWrite,
                                                   TextureState::ShaderRead);

    const PassRef producer = graph.AddPass("Produce", [history](RenderGraphBuilder& builder) {
        builder.SetQueue(RenderQueue::Graphics);
        (void)builder.Write(history, TextureUsage::ShaderWrite);
        builder.SideEffect();
    });
    (void)graph.AddPass("Consume", [history, producer](RenderGraphBuilder& builder) {
        builder.SetQueue(RenderQueue::AsyncCompute);
        builder.DependsOn(producer);
        (void)builder.Read(history, TextureUsage::ShaderRead);
        builder.SideEffect();
    });

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    EXPECT_EQ(compiled->QueueHandoffEdgeCount, 1u);
    EXPECT_EQ(compiled->CrossQueueTimelineEdgeCount, 1u);
    EXPECT_EQ(compiled->CrossQueueOwnershipTransferCount, 1u);
    EXPECT_EQ(TextureTransfers(*compiled, history.Index).size(), 2u);
}

TEST(GraphicsOwnershipTransferBarriers, TransientCrossQueueResourceUsesConcurrentSharingWithoutOwnershipTransfer)
{
    RenderGraph graph;
    const TextureRef shared = graph.CreateTexture("TransientShared", RHI::TextureDesc{});

    (void)graph.AddPass("Produce", [shared](RenderGraphBuilder& builder) {
        builder.SetQueue(RenderQueue::Graphics);
        (void)builder.Write(shared, TextureUsage::ShaderWrite);
    });
    const PassRef consumer = graph.AddPass("Consume", [shared](RenderGraphBuilder& builder) {
        builder.SetQueue(RenderQueue::AsyncCompute);
        (void)builder.Read(shared, TextureUsage::ShaderRead);
        builder.SideEffect();
    });

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    ASSERT_LT(shared.Index, compiled->TextureQueueSharingModes.size());
    EXPECT_EQ(compiled->TextureQueueSharingModes[shared.Index], QueueSharingMode::Concurrent);
    EXPECT_EQ(compiled->QueueHandoffEdgeCount, 1u);
    EXPECT_EQ(compiled->CrossQueueTimelineEdgeCount, 1u);
    EXPECT_EQ(compiled->CrossQueueOwnershipTransferCount, 0u);
    EXPECT_TRUE(TextureTransfers(*compiled, shared.Index).empty());

    bool sawConcurrentConsumerBarrier = false;
    for (const BarrierPacket& packet : compiled->BarrierPackets)
    {
        for (const TextureBarrierPacket& barrier : packet.TextureBarriers)
        {
            if (packet.PassIndex == consumer.Index &&
                packet.Stage == BarrierPacketStage::BeforePass &&
                barrier.TextureIndex == shared.Index &&
                barrier.Before == TextureBarrierState::ShaderWrite &&
                barrier.After == TextureBarrierState::ShaderRead)
            {
                sawConcurrentConsumerBarrier = true;
                EXPECT_EQ(barrier.SharingMode, QueueSharingMode::Concurrent);
                EXPECT_EQ(barrier.OwnershipTransfer.Kind, QueueOwnershipTransferKind::None);
                EXPECT_EQ(barrier.OwnershipTransfer.SourceQueueFamily, kIgnoredQueueFamily);
                EXPECT_EQ(barrier.OwnershipTransfer.DestinationQueueFamily, kIgnoredQueueFamily);
            }
        }
    }
    EXPECT_TRUE(sawConcurrentConsumerBarrier);
}

TEST(GraphicsOwnershipTransferBarriers, SingleQueueResourceKeepsExclusiveBarriersWithoutQueueFamilies)
{
    RenderGraph graph;
    const TextureRef lighting = graph.CreateTexture("Lighting", RHI::TextureDesc{});

    (void)graph.AddPass("Write", [lighting](RenderGraphBuilder& builder) {
        builder.SetQueue(RenderQueue::Graphics);
        (void)builder.Write(lighting, TextureUsage::ColorAttachmentWrite);
    });
    const PassRef reader = graph.AddPass("Read", [lighting](RenderGraphBuilder& builder) {
        builder.SetQueue(RenderQueue::Graphics);
        (void)builder.Read(lighting, TextureUsage::ShaderRead);
        builder.SideEffect();
    });

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    ASSERT_LT(lighting.Index, compiled->TextureQueueSharingModes.size());
    EXPECT_EQ(compiled->TextureQueueSharingModes[lighting.Index], QueueSharingMode::Exclusive);
    EXPECT_EQ(compiled->QueueHandoffEdgeCount, 0u);
    EXPECT_EQ(compiled->CrossQueueTimelineEdgeCount, 0u);
    EXPECT_EQ(compiled->CrossQueueOwnershipTransferCount, 0u);

    bool sawShaderReadBarrier = false;
    for (const BarrierPacket& packet : compiled->BarrierPackets)
    {
        for (const TextureBarrierPacket& barrier : packet.TextureBarriers)
        {
            if (packet.PassIndex == reader.Index &&
                packet.Stage == BarrierPacketStage::BeforePass &&
                barrier.TextureIndex == lighting.Index &&
                barrier.Before == TextureBarrierState::ColorAttachmentWrite &&
                barrier.After == TextureBarrierState::ShaderRead)
            {
                sawShaderReadBarrier = true;
                EXPECT_EQ(barrier.SharingMode, QueueSharingMode::Exclusive);
                EXPECT_EQ(barrier.OwnershipTransfer.Kind, QueueOwnershipTransferKind::None);
                EXPECT_EQ(barrier.OwnershipTransfer.SourceQueueFamily, kIgnoredQueueFamily);
                EXPECT_EQ(barrier.OwnershipTransfer.DestinationQueueFamily, kIgnoredQueueFamily);
            }
        }
    }
    EXPECT_TRUE(sawShaderReadBarrier);
}

// BUG-015: the renderer lowers a compiled ownership transfer to a real Vulkan
// QFOT only when the device's framegraph queue profile schedules the producer
// and consumer onto different queues. This predicate is the single source of
// truth shared by the renderer; it must collapse to a plain barrier under the
// promoted graphics-only profile even when the compiled transfer names distinct
// async-compute/transfer queues.
TEST(OwnershipTransferBarriers, LiveCrossQueueTransferDependsOnDeviceProfile)
{
    const QueueOwnershipTransfer asyncComputeAcquire{
        .Kind = QueueOwnershipTransferKind::Acquire,
        .SourceQueue = RHI::QueueAffinity::AsyncCompute,
        .DestinationQueue = RHI::QueueAffinity::Graphics,
        .SourceQueueFamily = QueueFamilyToken(RHI::QueueAffinity::AsyncCompute),
        .DestinationQueueFamily = QueueFamilyToken(RHI::QueueAffinity::Graphics),
    };

    // Graphics-only profile (the promoted-device default): both sides resolve to
    // graphics, so the transfer collapses — no QFOT may be recorded.
    const RHI::QueueCapabilityProfile graphicsOnly{
        .SupportsAsyncCompute = false,
        .SupportsTransfer = false,
    };
    EXPECT_FALSE(IsLiveCrossQueueOwnershipTransfer(asyncComputeAcquire, graphicsOnly));

    // A profile that genuinely exposes async compute keeps the hand-off live.
    const RHI::QueueCapabilityProfile asyncCapable{
        .SupportsAsyncCompute = true,
        .SupportsTransfer = false,
    };
    EXPECT_TRUE(IsLiveCrossQueueOwnershipTransfer(asyncComputeAcquire, asyncCapable));

    // A transfer-queue hand-off stays collapsed unless transfer is supported.
    const QueueOwnershipTransfer transferRelease{
        .Kind = QueueOwnershipTransferKind::Release,
        .SourceQueue = RHI::QueueAffinity::Graphics,
        .DestinationQueue = RHI::QueueAffinity::Transfer,
        .SourceQueueFamily = QueueFamilyToken(RHI::QueueAffinity::Graphics),
        .DestinationQueueFamily = QueueFamilyToken(RHI::QueueAffinity::Transfer),
    };
    EXPECT_FALSE(IsLiveCrossQueueOwnershipTransfer(transferRelease, asyncCapable));
    EXPECT_TRUE(IsLiveCrossQueueOwnershipTransfer(
        transferRelease,
        RHI::QueueCapabilityProfile{.SupportsAsyncCompute = false, .SupportsTransfer = true}));

    // A None-kind barrier is never a queue ownership transfer.
    EXPECT_FALSE(IsLiveCrossQueueOwnershipTransfer(QueueOwnershipTransfer{}, asyncCapable));
}
