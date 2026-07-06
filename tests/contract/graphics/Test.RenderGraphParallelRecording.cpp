#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

import Extrinsic.Core.Error;
import Extrinsic.Core.Tasks;
import Extrinsic.Graphics.RenderGraph;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;

namespace
{
    using namespace Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;
    namespace Tasks = Extrinsic::Core::Tasks;

    class SchedulerScope
    {
    public:
        explicit SchedulerScope(const unsigned threadCount)
            : m_Owns(!Tasks::Scheduler::IsInitialized())
        {
            if (m_Owns)
            {
                Tasks::Scheduler::Initialize(threadCount);
            }
        }

        ~SchedulerScope()
        {
            if (m_Owns)
            {
                Tasks::Scheduler::Shutdown();
            }
        }

        SchedulerScope(const SchedulerScope&) = delete;
        SchedulerScope& operator=(const SchedulerScope&) = delete;

    private:
        bool m_Owns = false;
    };

    [[nodiscard]] std::string BarrierEvent(const BarrierPacket& packet)
    {
        return "barrier(" + std::to_string(packet.PassIndex) + "," +
               std::to_string(static_cast<int>(packet.Stage)) + ")";
    }

    [[nodiscard]] std::vector<std::string> ExecuteSerialEvents(const CompiledRenderGraph& compiled)
    {
        std::vector<std::string> events{};
        RenderGraphExecutor executor;
        const auto result = executor.Execute(
            compiled,
            [&events](const std::uint32_t passIndex) {
                events.push_back("pass(" + std::to_string(passIndex) + ")");
            },
            [&events](const BarrierPacket& packet) {
                events.push_back(BarrierEvent(packet));
            });
        EXPECT_TRUE(result.has_value());
        return events;
    }

    [[nodiscard]] CompiledRenderGraph CompilePresentChain()
    {
        RenderGraph graph;
        const auto lighting = graph.CreateTexture("Lighting", RHI::TextureDesc{});
        const auto backbuffer = graph.ImportBackbuffer("Backbuffer", RHI::TextureHandle{20u, 2u});
        (void)graph.AddPass("Lighting", [lighting](RenderGraphBuilder& builder) {
            (void)builder.Write(lighting, TextureUsage::ColorAttachmentWrite);
        });
        (void)graph.AddPass("Post", [lighting, backbuffer](RenderGraphBuilder& builder) {
            (void)builder.Read(lighting, TextureUsage::ShaderRead);
            (void)builder.Write(backbuffer, TextureUsage::ColorAttachmentWrite);
        }, true);
        (void)graph.AddPass("Present", [backbuffer](RenderGraphBuilder& builder) {
            (void)builder.Read(backbuffer, TextureUsage::Present);
        });

        auto compiled = graph.Compile();
        EXPECT_TRUE(compiled.has_value());
        return compiled.value_or(CompiledRenderGraph{});
    }

    [[nodiscard]] CompiledRenderGraph CompileIndependentSideEffects(const std::uint32_t passCount)
    {
        RenderGraph graph;
        for (std::uint32_t i = 0u; i < passCount; ++i)
        {
            (void)graph.AddPass("Independent" + std::to_string(i), true);
        }

        auto compiled = graph.Compile();
        EXPECT_TRUE(compiled.has_value());
        return compiled.value_or(CompiledRenderGraph{});
    }

    void RaiseMax(std::atomic<int>& maxValue, const int candidate)
    {
        int observed = maxValue.load(std::memory_order_relaxed);
        while (candidate > observed &&
               !maxValue.compare_exchange_weak(observed,
                                               candidate,
                                               std::memory_order_acq_rel,
                                               std::memory_order_relaxed))
        {
        }
    }
}

TEST(RenderGraphParallelRecording, ParallelRecordJoinPreservesSerialSubmitOrder)
{
    const CompiledRenderGraph compiled = CompilePresentChain();
    ASSERT_EQ(compiled.TopologicalOrder.size(), 3u);

    const std::vector<std::string> serialEvents = ExecuteSerialEvents(compiled);
    std::vector<std::uint32_t> recordedPasses{};
    std::vector<std::string> parallelEvents{};
    ParallelRecordStats stats{};

    RenderGraphExecutor executor;
    const auto result = executor.ExecuteParallelRecordJoin(
        compiled,
        [&recordedPasses](const std::uint32_t passIndex, const std::uint32_t) {
            recordedPasses.push_back(passIndex);
            return Extrinsic::Core::Ok();
        },
        [&parallelEvents](const std::uint32_t passIndex) {
            parallelEvents.push_back("pass(" + std::to_string(passIndex) + ")");
        },
        [&parallelEvents](const BarrierPacket& packet) {
            parallelEvents.push_back(BarrierEvent(packet));
        },
        &stats,
        ParallelRecordOptions{.UseScheduler = false});

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(parallelEvents, serialEvents);
    EXPECT_EQ(recordedPasses, compiled.TopologicalOrder);
    EXPECT_EQ(stats.ScheduledPassCount, compiled.TopologicalOrder.size());
    EXPECT_EQ(stats.CallerRecordCount, compiled.TopologicalOrder.size());
    EXPECT_FALSE(stats.UsedScheduler);
}

TEST(RenderGraphParallelRecording, IndependentLayerRecordsOnWorkers)
{
    SchedulerScope scheduler{4u};
    const CompiledRenderGraph compiled = CompileIndependentSideEffects(8u);
    ASSERT_EQ(compiled.TopologicalOrder.size(), 8u);

    std::atomic<int> active{0};
    std::atomic<int> maxActive{0};
    std::atomic<int> recordCount{0};
    std::mutex threadMutex;
    std::set<std::thread::id> recordingThreads{};
    const std::thread::id callerThread = std::this_thread::get_id();
    ParallelRecordStats stats{};

    RenderGraphExecutor executor;
    const auto result = executor.ExecuteParallelRecordJoin(
        compiled,
        [&](const std::uint32_t, const std::uint32_t) {
            {
                std::scoped_lock lock(threadMutex);
                recordingThreads.insert(std::this_thread::get_id());
            }
            const int nowActive = active.fetch_add(1, std::memory_order_acq_rel) + 1;
            RaiseMax(maxActive, nowActive);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            active.fetch_sub(1, std::memory_order_acq_rel);
            recordCount.fetch_add(1, std::memory_order_acq_rel);
            return Extrinsic::Core::Ok();
        },
        {},
        {},
        &stats,
        ParallelRecordOptions{.UseScheduler = true, .MinWorkerPassCount = 2u});

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(stats.UsedScheduler);
    EXPECT_EQ(stats.WorkerTaskCount, compiled.TopologicalOrder.size());
    EXPECT_EQ(stats.CallerRecordCount, 0u);
    EXPECT_EQ(recordCount.load(std::memory_order_acquire),
              static_cast<int>(compiled.TopologicalOrder.size()));
    EXPECT_GT(maxActive.load(std::memory_order_acquire), 1);
    EXPECT_TRUE(std::any_of(recordingThreads.begin(),
                            recordingThreads.end(),
                            [callerThread](const std::thread::id id) {
                                return id != callerThread;
                            }));
}

TEST(RenderGraphParallelRecording, RecordFailureJoinsWorkersAndSkipsSubmit)
{
    SchedulerScope scheduler{4u};
    const CompiledRenderGraph compiled = CompileIndependentSideEffects(6u);
    ASSERT_EQ(compiled.TopologicalOrder.size(), 6u);

    const std::uint32_t failingPass = compiled.TopologicalOrder[2u];
    std::atomic<int> recordCount{0};
    std::vector<std::uint32_t> submittedPasses{};
    ParallelRecordStats stats{};

    RenderGraphExecutor executor;
    const auto result = executor.ExecuteParallelRecordJoin(
        compiled,
        [failingPass, &recordCount](const std::uint32_t passIndex, const std::uint32_t) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            recordCount.fetch_add(1, std::memory_order_acq_rel);
            if (passIndex == failingPass)
            {
                return Extrinsic::Core::Err(Extrinsic::Core::ErrorCode::InvalidState);
            }
            return Extrinsic::Core::Ok();
        },
        [&submittedPasses](const std::uint32_t passIndex) {
            submittedPasses.push_back(passIndex);
        },
        {},
        &stats,
        ParallelRecordOptions{.UseScheduler = true, .MinWorkerPassCount = 2u});

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Extrinsic::Core::ErrorCode::InvalidState);
    EXPECT_EQ(recordCount.load(std::memory_order_acquire),
              static_cast<int>(compiled.TopologicalOrder.size()));
    EXPECT_TRUE(submittedPasses.empty());
    EXPECT_TRUE(stats.UsedScheduler);
    EXPECT_EQ(stats.WorkerTaskCount, compiled.TopologicalOrder.size());
}
