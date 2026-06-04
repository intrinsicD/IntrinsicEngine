#include <gtest/gtest.h>

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

import Extrinsic.Core.Error;
import Extrinsic.Graphics.RenderGraph;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.QueueAffinity;
import Extrinsic.RHI.TimelineSemaphore;

namespace
{
    using namespace Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;

    struct TimelineCall
    {
        RHI::QueueAffinity Queue = RHI::QueueAffinity::Graphics;
        std::uint64_t Value = 0;
    };

    class RecordingTimelineSemaphore final : public RHI::ITimelineSemaphore
    {
    public:
        std::vector<TimelineCall> Signals{};
        std::vector<TimelineCall> Waits{};

        void Signal(const RHI::QueueAffinity queue, const std::uint64_t value) override
        {
            Signals.push_back(TimelineCall{.Queue = queue, .Value = value});
        }

        void Wait(const RHI::QueueAffinity queue, const std::uint64_t value) override
        {
            Waits.push_back(TimelineCall{.Queue = queue, .Value = value});
        }
    };

    [[nodiscard]] std::string TimelineSignature(const CompiledRenderGraph& compiled)
    {
        std::ostringstream out;
        out << "signals:";
        for (const CrossQueueTimelineSignal& signal : compiled.CrossQueueTimelineSignals)
        {
            out << signal.PassIndex << ':' << static_cast<int>(signal.Queue) << ':' << signal.Value << ';';
        }
        out << "waits:";
        for (const CrossQueueTimelineWait& wait : compiled.CrossQueueTimelineWaits)
        {
            out << wait.SignalPassIndex << "->" << wait.PassIndex << ':'
                << static_cast<int>(wait.SignalQueue) << "->" << static_cast<int>(wait.Queue)
                << ':' << wait.Value << ';';
        }
        out << "edges:";
        for (const CrossQueueTimelineEdge& edge : compiled.CrossQueueTimelineEdges)
        {
            out << edge.SignalPassIndex << "->" << edge.WaitPassIndex << ':'
                << static_cast<int>(edge.SignalQueue) << "->" << static_cast<int>(edge.WaitQueue)
                << ':' << edge.Value << ';';
        }
        return out.str();
    }

    [[nodiscard]] CompiledRenderGraph CompileDeterministicTimelineGraph()
    {
        RenderGraph graph;
        const TextureRef first = graph.CreateTexture("First", RHI::TextureDesc{});
        const TextureRef second = graph.CreateTexture("Second", RHI::TextureDesc{});
        const TextureRef third = graph.CreateTexture("Third", RHI::TextureDesc{});

        (void)graph.AddPass("GraphicsProducer0", [first](RenderGraphBuilder& builder) {
            builder.SetQueue(RenderQueue::Graphics);
            (void)builder.Write(first, TextureUsage::ShaderWrite);
        });
        (void)graph.AddPass("AsyncConsumer0", [first](RenderGraphBuilder& builder) {
            builder.SetQueue(RenderQueue::AsyncCompute);
            (void)builder.Read(first, TextureUsage::ShaderRead);
            builder.SideEffect();
        });
        (void)graph.AddPass("GraphicsProducer1", [second](RenderGraphBuilder& builder) {
            builder.SetQueue(RenderQueue::Graphics);
            (void)builder.Write(second, TextureUsage::ShaderWrite);
        });
        (void)graph.AddPass("TransferConsumer1", [second](RenderGraphBuilder& builder) {
            builder.SetQueue(RenderQueue::Transfer);
            (void)builder.Read(second, TextureUsage::ShaderRead);
            builder.SideEffect();
        });
        (void)graph.AddPass("AsyncProducer2", [third](RenderGraphBuilder& builder) {
            builder.SetQueue(RenderQueue::AsyncCompute);
            (void)builder.Write(third, TextureUsage::ShaderWrite);
        });
        (void)graph.AddPass("GraphicsConsumer2", [third](RenderGraphBuilder& builder) {
            builder.SetQueue(RenderQueue::Graphics);
            (void)builder.Read(third, TextureUsage::ShaderRead);
            builder.SideEffect();
        });

        auto compiled = graph.Compile();
        EXPECT_TRUE(compiled.has_value());
        return compiled.has_value() ? std::move(*compiled) : CompiledRenderGraph{};
    }
}

TEST(GraphicsCrossQueueTimeline, RhiTimelineSemaphoreSurfaceRecordsSignalAndWait)
{
    RecordingTimelineSemaphore semaphore;

    semaphore.Signal(RHI::QueueAffinity::Graphics, 7u);
    semaphore.Wait(RHI::QueueAffinity::AsyncCompute, 7u);

    ASSERT_EQ(semaphore.Signals.size(), 1u);
    ASSERT_EQ(semaphore.Waits.size(), 1u);
    EXPECT_EQ(semaphore.Signals[0].Queue, RHI::QueueAffinity::Graphics);
    EXPECT_EQ(semaphore.Signals[0].Value, 7u);
    EXPECT_EQ(semaphore.Waits[0].Queue, RHI::QueueAffinity::AsyncCompute);
    EXPECT_EQ(semaphore.Waits[0].Value, 7u);
}

TEST(GraphicsCrossQueueTimeline, EmitsSignalAndWaitAtCrossQueueBoundary)
{
    RenderGraph graph;
    const TextureRef shared = graph.CreateTexture("Shared", RHI::TextureDesc{});

    const PassRef producer = graph.AddPass("Produce", [shared](RenderGraphBuilder& builder) {
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
    EXPECT_EQ(compiled->QueueHandoffEdgeCount, 1u);
    EXPECT_EQ(compiled->CrossQueueTimelineEdgeCount, 1u);
    ASSERT_EQ(compiled->CrossQueueTimelineSignals.size(), 1u);
    ASSERT_EQ(compiled->CrossQueueTimelineWaits.size(), 1u);
    ASSERT_EQ(compiled->CrossQueueTimelineEdges.size(), 1u);

    EXPECT_EQ(compiled->CrossQueueTimelineSignals[0].PassIndex, producer.Index);
    EXPECT_EQ(compiled->CrossQueueTimelineSignals[0].Queue, RenderQueue::Graphics);
    EXPECT_EQ(compiled->CrossQueueTimelineSignals[0].Value, 1u);

    EXPECT_EQ(compiled->CrossQueueTimelineWaits[0].PassIndex, consumer.Index);
    EXPECT_EQ(compiled->CrossQueueTimelineWaits[0].Queue, RenderQueue::AsyncCompute);
    EXPECT_EQ(compiled->CrossQueueTimelineWaits[0].SignalPassIndex, producer.Index);
    EXPECT_EQ(compiled->CrossQueueTimelineWaits[0].SignalQueue, RenderQueue::Graphics);
    EXPECT_EQ(compiled->CrossQueueTimelineWaits[0].Value, 1u);
}

TEST(GraphicsCrossQueueTimeline, ValuesAreMonotonicPerProducingQueueAndDeterministic)
{
    const CompiledRenderGraph first = CompileDeterministicTimelineGraph();
    const CompiledRenderGraph second = CompileDeterministicTimelineGraph();

    ASSERT_EQ(first.CrossQueueTimelineSignals.size(), 3u);
    EXPECT_EQ(first.CrossQueueTimelineSignals[0].Queue, RenderQueue::Graphics);
    EXPECT_EQ(first.CrossQueueTimelineSignals[0].Value, 1u);
    EXPECT_EQ(first.CrossQueueTimelineSignals[1].Queue, RenderQueue::Graphics);
    EXPECT_EQ(first.CrossQueueTimelineSignals[1].Value, 2u);
    EXPECT_EQ(first.CrossQueueTimelineSignals[2].Queue, RenderQueue::AsyncCompute);
    EXPECT_EQ(first.CrossQueueTimelineSignals[2].Value, 1u);
    EXPECT_EQ(first.CrossQueueTimelineEdgeCount, 3u);
    EXPECT_EQ(TimelineSignature(first), TimelineSignature(second));
}

TEST(GraphicsCrossQueueTimeline, CulledProducerBranchEmitsNoTimelineEdges)
{
    RenderGraph graph;
    const TextureRef culled = graph.CreateTexture("Culled", RHI::TextureDesc{});

    (void)graph.AddPass("CulledProducer", [culled](RenderGraphBuilder& builder) {
        builder.SetQueue(RenderQueue::Graphics);
        (void)builder.Write(culled, TextureUsage::ShaderWrite);
    });
    (void)graph.AddPass("CulledConsumer", [culled](RenderGraphBuilder& builder) {
        builder.SetQueue(RenderQueue::AsyncCompute);
        (void)builder.Read(culled, TextureUsage::ShaderRead);
    });
    (void)graph.AddPass("LiveSideEffect", true);

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    EXPECT_EQ(compiled->PassCount, 1u);
    EXPECT_EQ(compiled->CulledPassCount, 2u);
    EXPECT_EQ(compiled->QueueHandoffEdgeCount, 0u);
    EXPECT_EQ(compiled->CrossQueueTimelineEdgeCount, 0u);
    EXPECT_TRUE(compiled->CrossQueueTimelineSignals.empty());
    EXPECT_TRUE(compiled->CrossQueueTimelineWaits.empty());
}

TEST(GraphicsCrossQueueTimeline, CrossQueueCycleReportsStructuredFinding)
{
    std::vector<TextureResourceDesc> textures(1u);
    textures[0].Name = "Shared";

    std::vector<RenderPassRecord> passes{
        RenderPassRecord{
            .Name = "GraphicsRead",
            .SideEffect = true,
            .Queue = RenderQueue::Graphics,
            .TextureAccesses = {TextureAccess{.Ref = TextureRef{.Index = 0u, .Generation = 1u}, .Usage = TextureUsage::ShaderRead}},
            .ExplicitDependencies = {PassRef{.Index = 1u, .Generation = 1u}},
        },
        RenderPassRecord{
            .Name = "AsyncRead",
            .Queue = RenderQueue::AsyncCompute,
            .TextureAccesses = {TextureAccess{.Ref = TextureRef{.Index = 0u, .Generation = 1u}, .Usage = TextureUsage::ShaderRead}},
        },
    };

    const auto compiled = RenderGraphCompiler::Compile(passes, textures, {});

    ASSERT_FALSE(compiled.has_value());
    EXPECT_EQ(compiled.error(), Extrinsic::Core::ErrorCode::InvalidState);
    const RenderGraphValidationResult& result = RenderGraphCompiler::GetLastCompileValidationResult();
    ASSERT_EQ(result.Findings.size(), 2u);
    EXPECT_EQ(result.Findings[0].Code, RenderGraphValidationCode::CrossQueueCycle);
    EXPECT_EQ(result.Findings[1].Code, RenderGraphValidationCode::CrossQueueCycle);
    EXPECT_TRUE(result.HasErrors());
    EXPECT_NE(result.Findings[0].Message.find("cross-queue cycle"), std::string::npos);
}
