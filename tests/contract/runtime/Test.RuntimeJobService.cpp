#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Tasks;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.WorldHandle;

namespace CoreConfig = Extrinsic::Core::Config;
namespace Runtime = Extrinsic::Runtime;

namespace
{
    using namespace std::chrono_literals;

    struct JobProbeResult
    {
        int Value{0};
        std::thread::id WorkerThread{};
    };

    struct JobProbeCompleted
    {
        int Value{0};
        std::thread::id WorkerThread{};
    };

    struct JobSuppressedCompleted
    {
        int Value{0};
    };

    class SchedulerScope final
    {
    public:
        explicit SchedulerScope(const unsigned workers = 2)
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

    class CompletionQueueInterlock final
    {
    public:
        ~CompletionQueueInterlock()
        {
            ReleaseWorker();
        }

        void PauseWorker(const Runtime::JobToken token)
        {
            std::unique_lock lock(m_Mutex);
            m_QueuedToken = token;
            m_WorkerPaused = true;
            m_Condition.notify_all();
            m_Condition.wait(lock, [this] { return m_ReleaseWorker; });
        }

        [[nodiscard]] bool WaitForWorkerPause(
            const std::chrono::milliseconds timeout = 2s)
        {
            std::unique_lock lock(m_Mutex);
            return m_Condition.wait_for(
                lock,
                timeout,
                [this] { return m_WorkerPaused; });
        }

        [[nodiscard]] Runtime::JobToken QueuedToken() const
        {
            std::lock_guard lock(m_Mutex);
            return m_QueuedToken;
        }

        void ReleaseWorker()
        {
            {
                std::lock_guard lock(m_Mutex);
                m_ReleaseWorker = true;
            }
            m_Condition.notify_all();
        }

    private:
        mutable std::mutex m_Mutex{};
        std::condition_variable m_Condition{};
        Runtime::JobToken m_QueuedToken{};
        bool m_WorkerPaused{false};
        bool m_ReleaseWorker{false};
    };

    template <typename Predicate>
    [[nodiscard]] bool WaitUntil(Predicate&& predicate,
                                 const std::chrono::milliseconds timeout = 2s)
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

    [[nodiscard]] CoreConfig::EngineConfig NullWindowHeadlessConfig()
    {
        CoreConfig::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend = CoreConfig::WindowBackend::Null;
        return config;
    }

    class JobPumpProbeApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override
        {
            auto& engine = Kernel();
            MainThread   = std::this_thread::get_id();
            Subscription = engine.Events().Subscribe<JobProbeCompleted>(
                [this](const JobProbeCompleted& event)
                {
                    CompletionHits += 1;
                    CompletionValue = event.Value;
                    WorkerThread = event.WorkerThread;
                    CompletionThread = std::this_thread::get_id();
                    CompletionOnMainThread = CompletionThread == MainThread;
                });

            Token = engine.Jobs().Submit(
                Runtime::MakeCpuJobDesc<JobProbeResult>(
                    "engine pump probe",
                    Runtime::DefaultWorldHandle,
                    [](const Runtime::JobCancellation&)
                    {
                        return JobProbeResult{
                            .Value = 42,
                            .WorkerThread = std::this_thread::get_id(),
                        };
                    },
                    [](const JobProbeResult& result)
                    {
                        return JobProbeCompleted{
                            .Value = result.Value,
                            .WorkerThread = result.WorkerThread,
                        };
                    }));
        }


        void Frame(double, double) override
        {
            auto& engine = Kernel();
            VariableTicks += 1;
            if (CompletionHits > 0)
            {
                ObservedBeforeVariableTick = true;
                Stats = engine.Jobs().Stats();
                engine.RequestExit();
                return;
            }

            if (VariableTicks > 120)
            {
                TimedOut = true;
                Stats = engine.Jobs().Stats();
                engine.RequestExit();
            }
        }

        void Shutdown() override {}

        Runtime::KernelEventSubscription Subscription{};
        Runtime::JobToken Token{};
        Runtime::JobServiceStats Stats{};
        std::thread::id MainThread{};
        std::thread::id WorkerThread{};
        std::thread::id CompletionThread{};
        std::uint32_t CompletionHits{0};
        std::uint32_t VariableTicks{0};
        int CompletionValue{0};
        bool CompletionOnMainThread{false};
        bool ObservedBeforeVariableTick{false};
        bool TimedOut{false};
    };

    class RecordingGpuCommandContext final : public Extrinsic::RHI::ICommandContext
    {
    public:
        void Begin() override {}
        void End() override {}
        void BeginRenderPass(const Extrinsic::RHI::RenderPassDesc&) override {}
        void EndRenderPass() override {}
        void SetViewport(float, float, float, float, float, float) override {}
        void SetScissor(std::int32_t,
                        std::int32_t,
                        std::uint32_t,
                        std::uint32_t) override {}
        void BindPipeline(Extrinsic::RHI::PipelineHandle) override {}
        void BindIndexBuffer(Extrinsic::RHI::BufferHandle,
                             std::uint64_t,
                             Extrinsic::RHI::IndexType) override {}
        void PushConstants(const void*, std::uint32_t, std::uint32_t) override {}
        void Draw(std::uint32_t,
                  std::uint32_t,
                  std::uint32_t,
                  std::uint32_t) override {}
        void DrawIndexed(std::uint32_t,
                         std::uint32_t,
                         std::uint32_t,
                         std::int32_t,
                         std::uint32_t) override {}
        void DrawIndirect(Extrinsic::RHI::BufferHandle,
                          std::uint64_t,
                          std::uint32_t) override {}
        void DrawIndexedIndirect(Extrinsic::RHI::BufferHandle,
                                 std::uint64_t,
                                 std::uint32_t) override {}
        void DrawIndexedIndirectCount(Extrinsic::RHI::BufferHandle,
                                      std::uint64_t,
                                      Extrinsic::RHI::BufferHandle,
                                      std::uint64_t,
                                      std::uint32_t) override {}
        void DrawIndirectCount(Extrinsic::RHI::BufferHandle,
                               std::uint64_t,
                               Extrinsic::RHI::BufferHandle,
                               std::uint64_t,
                               std::uint32_t) override {}
        void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DispatchIndirect(Extrinsic::RHI::BufferHandle, std::uint64_t) override {}
        void TextureBarrier(Extrinsic::RHI::TextureHandle,
                            Extrinsic::RHI::TextureLayout,
                            Extrinsic::RHI::TextureLayout) override {}
        void BufferBarrier(Extrinsic::RHI::BufferHandle,
                           Extrinsic::RHI::MemoryAccess,
                           Extrinsic::RHI::MemoryAccess) override {}
        void SubmitBarriers(const Extrinsic::RHI::BarrierBatchDesc&) override {}
        void FillBuffer(Extrinsic::RHI::BufferHandle,
                        std::uint64_t,
                        std::uint64_t,
                        std::uint32_t) override {}
        void CopyBuffer(Extrinsic::RHI::BufferHandle,
                        Extrinsic::RHI::BufferHandle,
                        std::uint64_t,
                        std::uint64_t,
                        std::uint64_t) override {}
        void CopyBufferToTexture(Extrinsic::RHI::BufferHandle,
                                 std::uint64_t,
                                 Extrinsic::RHI::TextureHandle,
                                 std::uint32_t,
                                 std::uint32_t) override {}
    };
}

TEST(RuntimeJobService, SubmitRunsOnPoolThreadAndPublishesAtGate)
{
    SchedulerScope scheduler{2};
    Runtime::JobService jobs;
    Runtime::KernelEventBus events;

    const std::thread::id mainThread = std::this_thread::get_id();
    std::atomic<bool> workerFinished{false};
    std::thread::id workerThread{};
    std::thread::id completionThread{};
    int completedValue = 0;

    (void)events.Subscribe<JobProbeCompleted>(
        [&](const JobProbeCompleted& event)
        {
            completedValue = event.Value;
            workerThread = event.WorkerThread;
            completionThread = std::this_thread::get_id();
        });

    const Runtime::JobToken token = jobs.Submit(
        Runtime::MakeCpuJobDesc<JobProbeResult>(
            "standalone publish probe",
            Runtime::DefaultWorldHandle,
            [&](const Runtime::JobCancellation&)
            {
                JobProbeResult result{
                    .Value = 17,
                    .WorkerThread = std::this_thread::get_id(),
                };
                workerFinished.store(true, std::memory_order_release);
                return result;
            },
            [](const JobProbeResult& result)
            {
                return JobProbeCompleted{
                    .Value = result.Value,
                    .WorkerThread = result.WorkerThread,
                };
            }));

    ASSERT_TRUE(token.IsValid());
    ASSERT_TRUE(WaitUntil(
        [&] { return workerFinished.load(std::memory_order_acquire); }));
    Extrinsic::Core::Tasks::Scheduler::WaitForAll();

    EXPECT_EQ(completedValue, 0);
    EXPECT_EQ(jobs.DrainCompletions(events), 1u);
    EXPECT_EQ(completedValue, 0);
    EXPECT_EQ(events.Pump(), 1u);

    EXPECT_EQ(completedValue, 17);
    EXPECT_EQ(completionThread, mainThread);
    EXPECT_NE(workerThread, mainThread);
    EXPECT_TRUE(jobs.IsComplete(token));
    EXPECT_EQ(jobs.Stats().PublishedCompletions, 1u);

    // BUG-067: a drained completion must settle in a terminal state and reap
    // cleanly. The pre-fix worker stored AwaitingGate after the completion was
    // already enqueued (and drainable), so a same-window drain's terminal state
    // could be clobbered back to AwaitingGate, wedging the job forever.
    EXPECT_EQ(jobs.GetState(token), Runtime::JobState::Published);
    EXPECT_EQ(jobs.ReapCompleted(), 1u);
}

TEST(RuntimeJobService, CompletionQueuePublicationCannotClobberTerminalState)
{
    SchedulerScope scheduler{1};
    CompletionQueueInterlock interlock;
    Runtime::JobService jobs(Runtime::JobServiceTestHooks{
        .AfterCompletionQueued =
            [&](const Runtime::JobToken token)
            {
                interlock.PauseWorker(token);
            },
    });
    Runtime::KernelEventBus events;

    const Runtime::JobToken token = jobs.Submit(
        Runtime::MakeCpuJobDesc<JobProbeResult>(
            "forced completion publication interleaving",
            Runtime::DefaultWorldHandle,
            [](const Runtime::JobCancellation&)
            {
                return JobProbeResult{.Value = 23};
            },
            [](const JobProbeResult& result)
            {
                return JobProbeCompleted{.Value = result.Value};
            }));

    ASSERT_TRUE(token.IsValid());
    ASSERT_TRUE(interlock.WaitForWorkerPause());
    EXPECT_EQ(interlock.QueuedToken(), token);
    EXPECT_EQ(jobs.GetState(token), Runtime::JobState::AwaitingGate);

    // The worker is paused after queue visibility. Drain to a terminal state
    // before letting it return; the worker must have no later state write.
    EXPECT_EQ(jobs.DrainCompletions(events), 1u);
    EXPECT_EQ(jobs.GetState(token), Runtime::JobState::Published);

    interlock.ReleaseWorker();
    Extrinsic::Core::Tasks::Scheduler::WaitForAll();

    EXPECT_EQ(jobs.GetState(token), Runtime::JobState::Published);
    EXPECT_TRUE(jobs.IsComplete(token));
    const Runtime::JobServiceStats terminalStats = jobs.Stats();
    EXPECT_EQ(terminalStats.InFlightJobs, 0u);
    EXPECT_EQ(terminalStats.AwaitingGateJobs, 0u);
    EXPECT_EQ(jobs.ReapCompleted(), 1u);
    EXPECT_EQ(jobs.Stats().InFlightJobs, 0u);
}

TEST(RuntimeJobService, GpuQueueParticipantRecordsDrainsAndUnregisters)
{
    Runtime::JobService jobs;
    RecordingGpuCommandContext commandContext;

    bool inFlight = true;
    std::uint32_t recordCalls = 0;
    std::uint32_t drainCalls = 0;
    std::uint32_t idleWaits = 0;
    std::uint32_t shutdownCalls = 0;

    const Runtime::GpuQueueParticipantHandle handle =
        jobs.RegisterGpuQueueParticipant(Runtime::GpuQueueParticipantDesc{
            .DebugName = "gpu participant probe",
            .RecordFrameCommands =
                [&](Extrinsic::RHI::ICommandContext&)
                {
                    recordCalls += 1;
                },
            .DrainCompletedTransfers =
                [&]
                {
                    drainCalls += 1;
                },
            .HasInFlightWork =
                [&]
                {
                    return inFlight;
                },
            .ShutdownAfterDeviceIdle =
                [&]
                {
                    shutdownCalls += 1;
                    inFlight = false;
                },
        });

    ASSERT_TRUE(handle.IsValid());
    EXPECT_TRUE(jobs.HasGpuQueueWork());

    jobs.RecordGpuQueueFrameCommands(commandContext);
    EXPECT_EQ(recordCalls, 1u);
    EXPECT_EQ(jobs.DrainGpuQueueCompletedTransfers(), 1u);
    EXPECT_EQ(drainCalls, 1u);

    jobs.UnregisterGpuQueueParticipant(
        handle,
        [&]
        {
            idleWaits += 1;
        });

    EXPECT_EQ(idleWaits, 1u);
    EXPECT_EQ(shutdownCalls, 1u);
    EXPECT_FALSE(jobs.HasGpuQueueWork());

    jobs.RecordGpuQueueFrameCommands(commandContext);
    EXPECT_EQ(recordCalls, 1u);
    EXPECT_EQ(jobs.DrainGpuQueueCompletedTransfers(), 0u);
    EXPECT_EQ(drainCalls, 1u);
}

TEST(RuntimeJobService, GpuQueueShutdownRunsParticipantsInReverseOrder)
{
    Runtime::JobService jobs;
    std::vector<int> shutdownOrder;
    std::uint32_t idleWaits = 0;

    const Runtime::GpuQueueParticipantHandle first =
        jobs.RegisterGpuQueueParticipant(Runtime::GpuQueueParticipantDesc{
            .DebugName = "first gpu participant",
            .HasInFlightWork = [] { return false; },
            .ShutdownAfterDeviceIdle =
                [&]
                {
                    shutdownOrder.push_back(1);
                },
        });
    const Runtime::GpuQueueParticipantHandle second =
        jobs.RegisterGpuQueueParticipant(Runtime::GpuQueueParticipantDesc{
            .DebugName = "second gpu participant",
            .HasInFlightWork = [] { return true; },
            .ShutdownAfterDeviceIdle =
                [&]
                {
                    shutdownOrder.push_back(2);
                },
        });

    ASSERT_TRUE(first.IsValid());
    ASSERT_TRUE(second.IsValid());
    EXPECT_TRUE(jobs.HasGpuQueueWork());

    EXPECT_EQ(jobs.ShutdownGpuQueueParticipants(
                  [&]
                  {
                      idleWaits += 1;
                  }),
              2u);

    ASSERT_EQ(shutdownOrder.size(), 2u);
    EXPECT_EQ(shutdownOrder[0], 2);
    EXPECT_EQ(shutdownOrder[1], 1);
    EXPECT_EQ(idleWaits, 1u);
    EXPECT_FALSE(jobs.HasGpuQueueWork());
}

TEST(RuntimeJobService, CancelBeforeStartPreventsWork)
{
    SchedulerScope scheduler{1};
    std::atomic<bool> blockerStarted{false};
    std::atomic<bool> releaseBlocker{false};

    Extrinsic::Core::Tasks::Scheduler::Dispatch(
        [&]
        {
            blockerStarted.store(true, std::memory_order_release);
            while (!releaseBlocker.load(std::memory_order_acquire))
                std::this_thread::sleep_for(1ms);
        });
    ASSERT_TRUE(WaitUntil(
        [&] { return blockerStarted.load(std::memory_order_acquire); }));

    Runtime::JobService jobs;
    Runtime::KernelEventBus events;
    std::atomic<bool> workRan{false};

    const Runtime::JobToken token = jobs.Submit(
        Runtime::MakeCpuJobDesc<JobProbeResult>(
            "cancel before start",
            Runtime::DefaultWorldHandle,
            [&](const Runtime::JobCancellation&)
            {
                workRan.store(true, std::memory_order_release);
                return JobProbeResult{.Value = 1};
            },
            [](const JobProbeResult&)
            { return JobSuppressedCompleted{.Value = 1}; }));
    ASSERT_TRUE(token.IsValid());

    EXPECT_TRUE(jobs.Cancel(token));
    releaseBlocker.store(true, std::memory_order_release);
    Extrinsic::Core::Tasks::Scheduler::WaitForAll();

    EXPECT_FALSE(workRan.load(std::memory_order_acquire));
    EXPECT_EQ(jobs.GetState(token), Runtime::JobState::Cancelled);
    EXPECT_EQ(jobs.DrainCompletions(events), 0u);
    EXPECT_EQ(events.Pump(), 0u);
    EXPECT_EQ(jobs.Stats().CancelledJobs, 1u);
}

TEST(RuntimeJobService, CancelMidFlightIsObservedAndDropsCompletion)
{
    SchedulerScope scheduler{2};
    Runtime::JobService jobs;
    Runtime::KernelEventBus events;

    std::atomic<bool> started{false};
    std::atomic<bool> observedCancellation{false};
    int completionHits = 0;
    (void)events.Subscribe<JobSuppressedCompleted>(
        [&](const JobSuppressedCompleted&) { completionHits += 1; });

    const Runtime::JobToken token = jobs.Submit(
        Runtime::MakeCpuJobDesc<JobProbeResult>(
            "cancel mid-flight",
            Runtime::DefaultWorldHandle,
            [&](const Runtime::JobCancellation& cancellation)
            {
                started.store(true, std::memory_order_release);
                while (!cancellation.IsCancelled())
                    std::this_thread::sleep_for(1ms);
                observedCancellation.store(true, std::memory_order_release);
                return JobProbeResult{.Value = 3};
            },
            [](const JobProbeResult& result)
            { return JobSuppressedCompleted{.Value = result.Value}; }));
    ASSERT_TRUE(token.IsValid());

    ASSERT_TRUE(WaitUntil([&] { return started.load(std::memory_order_acquire); }));
    EXPECT_TRUE(jobs.Cancel(token));
    Extrinsic::Core::Tasks::Scheduler::WaitForAll();

    EXPECT_TRUE(observedCancellation.load(std::memory_order_acquire));
    EXPECT_EQ(jobs.GetState(token), Runtime::JobState::Cancelled);
    EXPECT_EQ(jobs.DrainCompletions(events), 0u);
    EXPECT_EQ(events.Pump(), 0u);
    EXPECT_EQ(completionHits, 0);
}

TEST(RuntimeJobService, CancelAfterWorkerFinishSuppressesGatePublication)
{
    SchedulerScope scheduler{2};
    Runtime::JobService jobs;
    Runtime::KernelEventBus events;

    std::atomic<bool> workerFinished{false};
    int completionHits = 0;
    (void)events.Subscribe<JobSuppressedCompleted>(
        [&](const JobSuppressedCompleted&) { completionHits += 1; });

    const Runtime::JobToken token = jobs.Submit(
        Runtime::MakeCpuJobDesc<JobProbeResult>(
            "cancel after finish before gate",
            Runtime::DefaultWorldHandle,
            [&](const Runtime::JobCancellation&)
            {
                workerFinished.store(true, std::memory_order_release);
                return JobProbeResult{.Value = 5};
            },
            [](const JobProbeResult& result)
            { return JobSuppressedCompleted{.Value = result.Value}; }));
    ASSERT_TRUE(token.IsValid());

    ASSERT_TRUE(WaitUntil(
        [&] { return workerFinished.load(std::memory_order_acquire); }));
    Extrinsic::Core::Tasks::Scheduler::WaitForAll();

    ASSERT_EQ(jobs.GetState(token), Runtime::JobState::AwaitingGate);
    EXPECT_TRUE(jobs.Cancel(token));
    EXPECT_EQ(jobs.DrainCompletions(events), 0u);
    EXPECT_EQ(events.Pump(), 0u);

    EXPECT_EQ(completionHits, 0);
    EXPECT_EQ(jobs.GetState(token), Runtime::JobState::Cancelled);
    EXPECT_EQ(jobs.Stats().DroppedCompletions, 1u);
}

TEST(RuntimeJobService, CancelAllForWorldOnlyCancelsMatchingScope)
{
    SchedulerScope scheduler{1};
    std::atomic<bool> blockerStarted{false};
    std::atomic<bool> releaseBlocker{false};

    Extrinsic::Core::Tasks::Scheduler::Dispatch(
        [&]
        {
            blockerStarted.store(true, std::memory_order_release);
            while (!releaseBlocker.load(std::memory_order_acquire))
                std::this_thread::sleep_for(1ms);
        });
    ASSERT_TRUE(WaitUntil(
        [&] { return blockerStarted.load(std::memory_order_acquire); }));

    Runtime::JobService jobs;
    const Runtime::WorldHandle worldA{7u, 1u};
    const Runtime::WorldHandle worldB{8u, 1u};
    std::atomic<int> ranA{0};
    std::atomic<int> ranB{0};

    const auto makeJob = [&](Runtime::WorldHandle scope,
                             std::atomic<int>& counter,
                             const char* name)
    {
        return jobs.Submit(Runtime::MakeCpuJobDesc<JobProbeResult>(
            name,
            scope,
            [&counter](const Runtime::JobCancellation&)
            {
                counter.fetch_add(1, std::memory_order_acq_rel);
                return JobProbeResult{.Value = 1};
            },
            [](const JobProbeResult& result)
            { return JobSuppressedCompleted{.Value = result.Value}; }));
    };

    const Runtime::JobToken tokenA0 = makeJob(worldA, ranA, "world a 0");
    const Runtime::JobToken tokenA1 = makeJob(worldA, ranA, "world a 1");
    const Runtime::JobToken tokenB = makeJob(worldB, ranB, "world b");
    ASSERT_TRUE(tokenA0.IsValid());
    ASSERT_TRUE(tokenA1.IsValid());
    ASSERT_TRUE(tokenB.IsValid());

    EXPECT_EQ(jobs.CancelAllForWorld(worldA), 2u);
    releaseBlocker.store(true, std::memory_order_release);
    Extrinsic::Core::Tasks::Scheduler::WaitForAll();

    EXPECT_EQ(ranA.load(std::memory_order_acquire), 0);
    EXPECT_EQ(ranB.load(std::memory_order_acquire), 1);
    EXPECT_EQ(jobs.GetState(tokenA0), Runtime::JobState::Cancelled);
    EXPECT_EQ(jobs.GetState(tokenA1), Runtime::JobState::Cancelled);
    EXPECT_EQ(jobs.GetState(tokenB), Runtime::JobState::AwaitingGate);
}

TEST(RuntimeJobService, ReapCompletedRemovesTerminalRecords)
{
    SchedulerScope scheduler{2};
    Runtime::JobService jobs;
    Runtime::KernelEventBus events;

    std::atomic<bool> workerFinished{false};
    const Runtime::JobToken token = jobs.Submit(
        Runtime::MakeCpuJobDesc<JobProbeResult>(
            "reap completed",
            Runtime::DefaultWorldHandle,
            [&](const Runtime::JobCancellation&)
            {
                workerFinished.store(true, std::memory_order_release);
                return JobProbeResult{.Value = 9};
            },
            [](const JobProbeResult& result)
            { return JobSuppressedCompleted{.Value = result.Value}; }));
    ASSERT_TRUE(token.IsValid());
    ASSERT_TRUE(WaitUntil(
        [&] { return workerFinished.load(std::memory_order_acquire); }));
    Extrinsic::Core::Tasks::Scheduler::WaitForAll();

    EXPECT_EQ(jobs.DrainCompletions(events), 1u);
    EXPECT_TRUE(jobs.IsComplete(token));
    EXPECT_EQ(jobs.ReapCompleted(), 1u);
    EXPECT_EQ(jobs.GetState(token), Runtime::JobState::Invalid);
    EXPECT_EQ(jobs.Stats().ReapedJobs, 1u);
}

TEST(RuntimeJobService, EngineCompletionGatePublishesBeforePumpBOnMainThread)
{
    auto app = std::make_unique<JobPumpProbeApplication>();
    JobPumpProbeApplication* appPtr = app.get();

    Intrinsic::Tests::RuntimeTestKernel engine(NullWindowHeadlessConfig(), std::move(app));
    engine.Initialize();

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on "
           "headless hosts";

    engine.Run();

    EXPECT_FALSE(appPtr->TimedOut);
    EXPECT_TRUE(appPtr->ObservedBeforeVariableTick);
    EXPECT_EQ(appPtr->CompletionHits, 1u);
    EXPECT_EQ(appPtr->CompletionValue, 42);
    EXPECT_TRUE(appPtr->CompletionOnMainThread);
    EXPECT_EQ(appPtr->CompletionThread, appPtr->MainThread);
    EXPECT_NE(appPtr->WorkerThread, appPtr->MainThread);
    EXPECT_GE(appPtr->Stats.PublishedCompletions, 1u);

    engine.Shutdown();
}

// ============================================================================
// RUNTIME-194 Slice A — the completed JobService execution contract.
//
// These pin the capabilities that previously only existed on StreamingExecutor
// and DerivedJobRegistry: dependency edges, bounded main-thread apply, parked
// work, fail-closed epoch revalidation, and progress.
// ============================================================================

namespace
{
    struct CountedResult
    {
        int Value{0};
    };

    struct CountedCompleted
    {
        int Value{0};
    };

    // Submits a job that simply yields `value`, recording publication order.
    [[nodiscard]] Runtime::JobDesc MakeCountingJob(
        std::string name,
        const int value,
        std::vector<int>& publishOrder)
    {
        Runtime::JobDesc desc =
            Runtime::MakeCpuJobDesc<CountedResult>(
                std::move(name),
                Runtime::DefaultWorldHandle,
                [value](const Runtime::JobCancellation&)
                { return CountedResult{.Value = value}; },
                [](const CountedResult& result)
                { return CountedCompleted{.Value = result.Value}; });

        auto inner = std::move(desc.PublishCompletion);
        desc.PublishCompletion =
            [inner = std::move(inner), &publishOrder, value](
                Runtime::KernelEventBus& events,
                const Runtime::JobResultEnvelope& envelope) mutable -> bool
        {
            const bool published = inner(events, envelope);
            if (published)
                publishOrder.push_back(value);
            return published;
        };
        return desc;
    }
}

TEST(RuntimeJobService, DependenciesGateDispatchUntilUpstreamPublishes)
{
    SchedulerScope scheduler{2};
    Runtime::JobService jobs;
    Runtime::KernelEventBus events;

    std::vector<int> publishOrder;
    const Runtime::JobToken first =
        jobs.Submit(MakeCountingJob("dependency.first", 1, publishOrder));
    ASSERT_TRUE(first.IsValid());

    Runtime::JobDesc dependentDesc =
        MakeCountingJob("dependency.second", 2, publishOrder);
    dependentDesc.DependsOn.push_back(
        Runtime::JobDependency{.Job = first, .Reason = "needs first"});
    const Runtime::JobToken second = jobs.Submit(std::move(dependentDesc));
    ASSERT_TRUE(second.IsValid());

    // The dependent job must not be queued while its dependency is unfinished.
    EXPECT_EQ(jobs.GetState(second), Runtime::JobState::AwaitingDependencies);
    EXPECT_EQ(jobs.Stats().AwaitingDependencyJobs, 1u);

    ASSERT_TRUE(WaitUntil([&] { return jobs.GetState(first) ==
                                       Runtime::JobState::AwaitingGate; }));
    EXPECT_EQ(jobs.DrainCompletions(events), 1u);
    EXPECT_EQ(jobs.GetState(first), Runtime::JobState::Published);

    // The drain that retired the dependency also releases the dependent.
    ASSERT_TRUE(WaitUntil([&] { return jobs.GetState(second) ==
                                       Runtime::JobState::AwaitingGate; }));
    EXPECT_EQ(jobs.DrainCompletions(events), 1u);

    ASSERT_EQ(publishOrder.size(), 2u);
    EXPECT_EQ(publishOrder[0], 1);
    EXPECT_EQ(publishOrder[1], 2);
    EXPECT_EQ(jobs.Stats().AwaitingDependencyJobs, 0u);
}

TEST(RuntimeJobService, DependentIsCancelledWhenUpstreamDoesNotPublish)
{
    SchedulerScope scheduler{2};
    Runtime::JobService jobs;
    Runtime::KernelEventBus events;

    std::vector<int> publishOrder;
    const Runtime::JobToken first =
        jobs.Submit(MakeCountingJob("cancelled.first", 1, publishOrder));
    ASSERT_TRUE(first.IsValid());

    Runtime::JobDesc dependentDesc =
        MakeCountingJob("cancelled.second", 2, publishOrder);
    dependentDesc.DependsOn.push_back(
        Runtime::JobDependency{.Job = first, .Reason = "needs first"});
    const Runtime::JobToken second = jobs.Submit(std::move(dependentDesc));
    ASSERT_TRUE(second.IsValid());

    // Cancelling the upstream must not leave the dependent runnable: a chain
    // never applies half its work against inputs that were never produced.
    EXPECT_TRUE(jobs.Cancel(first));

    // Drain until the dependent is terminal rather than a fixed number of
    // times. The dependency-release pass only acts on upstreams that have
    // already reached a terminal state, and whether `first` gets there without
    // a drain (worker observed the cancel) or needs one (worker had already
    // queued its result at the gate) depends on a worker interleaving this test
    // does not control. Two unconditional drains raced that interleaving and
    // failed ~40% of runs.
    ASSERT_TRUE(WaitUntil([&]
    {
        (void)jobs.DrainCompletions(events);
        return jobs.IsComplete(second);
    }));

    EXPECT_EQ(jobs.GetState(second), Runtime::JobState::Cancelled);
    EXPECT_TRUE(publishOrder.empty());
    EXPECT_GE(jobs.Stats().DependencyCancelledJobs, 1u);
}

TEST(RuntimeJobService, BoundedApplyLimitsWorkPerDrainWithoutStarving)
{
    SchedulerScope scheduler{2};
    Runtime::JobService jobs;
    Runtime::KernelEventBus events;

    std::vector<int> publishOrder;
    constexpr int kJobCount = 5;
    for (int i = 0; i < kJobCount; ++i)
    {
        const Runtime::JobToken token = jobs.Submit(
            MakeCountingJob("bounded." + std::to_string(i), i, publishOrder));
        ASSERT_TRUE(token.IsValid());
    }

    ASSERT_TRUE(WaitUntil([&]
    {
        return jobs.Stats().InFlightJobs == 0u ||
               jobs.Stats().AwaitingGateJobs +
                       jobs.Stats().PublishedCompletions >=
                   static_cast<std::uint64_t>(kJobCount);
    }));

    // A burst of completions must not all land in one frame.
    const std::uint64_t firstDrain = jobs.DrainCompletions(events, 2u);
    EXPECT_LE(firstDrain, 2u);
    EXPECT_EQ(jobs.Stats().LastDrainBudget, 2u);

    std::uint64_t total = firstDrain;
    for (int guard = 0; guard < 16 && total < kJobCount; ++guard)
        total += jobs.DrainCompletions(events, 2u);

    // Everything still arrives exactly once. Completion order follows worker
    // finish order, not submission order — the pool is concurrent — so the
    // contract bounding must preserve is "nothing dropped, nothing duplicated",
    // not a sequence.
    EXPECT_EQ(total, static_cast<std::uint64_t>(kJobCount));
    ASSERT_EQ(publishOrder.size(), static_cast<std::size_t>(kJobCount));
    std::vector<int> seen = publishOrder;
    std::sort(seen.begin(), seen.end());
    for (int i = 0; i < kJobCount; ++i)
        EXPECT_EQ(seen[static_cast<std::size_t>(i)], i);
}

TEST(RuntimeJobService, NotReadyResultsParkInsteadOfApplyingOrBlocking)
{
    SchedulerScope scheduler{2};
    Runtime::JobService jobs;
    Runtime::KernelEventBus events;

    std::vector<int> publishOrder;
    std::atomic<bool> ready{false};

    Runtime::JobDesc desc = MakeCountingJob("parked", 7, publishOrder);
    desc.IsReadyToApply = [&ready] { return ready.load(std::memory_order_acquire); };
    const Runtime::JobToken token = jobs.Submit(std::move(desc));
    ASSERT_TRUE(token.IsValid());

    ASSERT_TRUE(WaitUntil([&] { return jobs.GetState(token) ==
                                       Runtime::JobState::AwaitingGate; }));

    EXPECT_EQ(jobs.DrainCompletions(events), 0u);
    EXPECT_EQ(jobs.GetState(token), Runtime::JobState::AwaitingApply);
    EXPECT_EQ(jobs.Stats().LastDrainParked, 1u);
    EXPECT_TRUE(publishOrder.empty());

    ready.store(true, std::memory_order_release);
    EXPECT_EQ(jobs.DrainCompletions(events), 1u);
    EXPECT_EQ(jobs.GetState(token), Runtime::JobState::Published);
    ASSERT_EQ(publishOrder.size(), 1u);
    EXPECT_EQ(publishOrder[0], 7);
}

TEST(RuntimeJobService, StaleWorldEpochDiscardsResultBeforeItCanMutate)
{
    SchedulerScope scheduler{2};
    Runtime::JobService jobs;
    Runtime::KernelEventBus events;

    const Runtime::WorldHandle world = Runtime::DefaultWorldHandle;
    const std::uint64_t epoch = jobs.AdvanceWorldGeneration(world);
    EXPECT_EQ(jobs.WorldGeneration(world), epoch);

    std::vector<int> publishOrder;
    Runtime::JobDesc desc = MakeCountingJob("stale", 3, publishOrder);
    desc.Scope = world;
    desc.CancellationGeneration = epoch;
    const Runtime::JobToken token = jobs.Submit(std::move(desc));
    ASSERT_TRUE(token.IsValid());

    ASSERT_TRUE(WaitUntil([&] { return jobs.GetState(token) ==
                                       Runtime::JobState::AwaitingGate; }));

    // World replacement between capture and apply must discard the result.
    (void)jobs.AdvanceWorldGeneration(world);

    EXPECT_EQ(jobs.DrainCompletions(events), 0u);
    EXPECT_EQ(jobs.GetState(token), Runtime::JobState::StaleDiscarded);
    EXPECT_TRUE(publishOrder.empty());
    EXPECT_EQ(jobs.Stats().LastDrainStaleDiscarded, 1u);
}

TEST(RuntimeJobService, ValidateBeforeApplyIsFailClosedAndAppliesOnlyOnce)
{
    SchedulerScope scheduler{2};
    Runtime::JobService jobs;
    Runtime::KernelEventBus events;

    std::vector<int> publishOrder;
    int validationCalls = 0;

    Runtime::JobDesc desc = MakeCountingJob("validated", 11, publishOrder);
    desc.ValidateBeforeApply = [&validationCalls]
    {
        ++validationCalls;
        return Runtime::JobApplyValidation::StaleGeneration;
    };
    const Runtime::JobToken token = jobs.Submit(std::move(desc));
    ASSERT_TRUE(token.IsValid());

    ASSERT_TRUE(WaitUntil([&] { return jobs.GetState(token) ==
                                       Runtime::JobState::AwaitingGate; }));

    EXPECT_EQ(jobs.DrainCompletions(events), 0u);
    EXPECT_EQ(validationCalls, 1);
    EXPECT_EQ(jobs.GetState(token), Runtime::JobState::StaleDiscarded);
    EXPECT_TRUE(publishOrder.empty());

    // A discarded record is terminal: further drains must not re-apply it.
    EXPECT_EQ(jobs.DrainCompletions(events), 0u);
    EXPECT_EQ(validationCalls, 1);
    EXPECT_TRUE(publishOrder.empty());
}

TEST(RuntimeJobService, ProgressIsReportableFromWorkAndReadableOnMainThread)
{
    SchedulerScope scheduler{2};
    Runtime::JobService jobs;
    Runtime::KernelEventBus events;

    std::vector<int> publishOrder;
    EXPECT_FLOAT_EQ(jobs.GetProgress(Runtime::JobToken{}).Normalized, 0.0f);

    const Runtime::JobToken token =
        jobs.Submit(MakeCountingJob("progress", 5, publishOrder));
    ASSERT_TRUE(token.IsValid());

    jobs.ReportProgress(token,
                        Runtime::JobProgress{.Normalized = 0.25f,
                                             .Determinate = true});
    const Runtime::JobProgress quarter = jobs.GetProgress(token);
    EXPECT_FLOAT_EQ(quarter.Normalized, 0.25f);
    EXPECT_TRUE(quarter.Determinate);

    jobs.ReportProgress(token,
                        Runtime::JobProgress{.Normalized = 0.0f,
                                             .Determinate = false});
    EXPECT_FALSE(jobs.GetProgress(token).Determinate);

    ASSERT_TRUE(WaitUntil([&] { return jobs.GetState(token) ==
                                       Runtime::JobState::AwaitingGate; }));
    EXPECT_EQ(jobs.DrainCompletions(events), 1u);
}

// ============================================================================
// RUNTIME-194 Slice B — the unpublished-completion finalizer.
//
// Four of the six production StreamingExecutor submit sites relied on
// `StreamingTaskDesc::FinalizeCancellationOnMainThread` to reconcile control
// state they own (an ingest state machine, a pending-request slot) when a task
// never applied. These pin the JobService replacement, whose contract is
// stronger: exactly one of PublishCompletion and FinalizeUnpublishedOnMainThread
// runs per submitted job, both on the main thread.
// ============================================================================

namespace
{
    struct FinalizerProbe
    {
        int Calls{0};
        std::thread::id Thread{};

        [[nodiscard]] auto Hook()
        {
            return [this]
            {
                ++Calls;
                Thread = std::this_thread::get_id();
            };
        }
    };
}

TEST(RuntimeJobService, CancelBeforeStartFinalizesOnMainThreadInsteadOfPublishing)
{
    SchedulerScope scheduler{2};
    Runtime::JobService jobs;
    Runtime::KernelEventBus events;

    std::vector<int> publishOrder;
    FinalizerProbe probe;

    Runtime::JobDesc desc = MakeCountingJob("finalize.precancel", 1, publishOrder);
    desc.FinalizeUnpublishedOnMainThread = probe.Hook();
    const Runtime::JobToken token = jobs.Submit(std::move(desc));
    ASSERT_TRUE(token.IsValid());

    EXPECT_TRUE(jobs.Cancel(token));

    // The worker observes the cancel and never queues a completion record, so
    // the finalizer is the only thing that can unblock the consumer.
    ASSERT_TRUE(WaitUntil([&] { return jobs.GetState(token) ==
                                       Runtime::JobState::Cancelled; }));

    EXPECT_EQ(jobs.DrainCompletions(events), 0u);
    EXPECT_EQ(probe.Calls, 1);
    EXPECT_EQ(probe.Thread, std::this_thread::get_id());
    EXPECT_TRUE(publishOrder.empty());
    EXPECT_EQ(jobs.Stats().LastDrainFinalizedUnpublished, 1u);

    // Exactly once: later drains must not re-finalize a terminal record.
    EXPECT_EQ(jobs.DrainCompletions(events), 0u);
    EXPECT_EQ(probe.Calls, 1);
    EXPECT_EQ(jobs.Stats().FinalizedUnpublishedJobs, 1u);
}

TEST(RuntimeJobService, CancelAfterWorkerFinishFinalizesExactlyOnce)
{
    SchedulerScope scheduler{2};
    Runtime::JobService jobs;
    Runtime::KernelEventBus events;

    std::vector<int> publishOrder;
    FinalizerProbe probe;

    Runtime::JobDesc desc = MakeCountingJob("finalize.postfinish", 2, publishOrder);
    desc.FinalizeUnpublishedOnMainThread = probe.Hook();
    const Runtime::JobToken token = jobs.Submit(std::move(desc));
    ASSERT_TRUE(token.IsValid());

    // Cancel once the result is already queued at the gate: the drain suppresses
    // publication, and the finalizer still owes the consumer its one call.
    ASSERT_TRUE(WaitUntil([&] { return jobs.GetState(token) ==
                                       Runtime::JobState::AwaitingGate; }));
    EXPECT_TRUE(jobs.Cancel(token));

    EXPECT_EQ(jobs.DrainCompletions(events), 0u);
    EXPECT_EQ(jobs.GetState(token), Runtime::JobState::Cancelled);
    EXPECT_EQ(probe.Calls, 1);
    EXPECT_EQ(probe.Thread, std::this_thread::get_id());
    EXPECT_TRUE(publishOrder.empty());
}

TEST(RuntimeJobService, StaleDiscardFinalizesSoConsumerStateCannotLeak)
{
    SchedulerScope scheduler{2};
    Runtime::JobService jobs;
    Runtime::KernelEventBus events;

    const Runtime::WorldHandle world = Runtime::DefaultWorldHandle;
    const std::uint64_t epoch = jobs.AdvanceWorldGeneration(world);

    std::vector<int> publishOrder;
    FinalizerProbe probe;

    Runtime::JobDesc desc = MakeCountingJob("finalize.stale", 3, publishOrder);
    desc.Scope = world;
    desc.CancellationGeneration = epoch;
    desc.FinalizeUnpublishedOnMainThread = probe.Hook();
    const Runtime::JobToken token = jobs.Submit(std::move(desc));
    ASSERT_TRUE(token.IsValid());

    ASSERT_TRUE(WaitUntil([&] { return jobs.GetState(token) ==
                                       Runtime::JobState::AwaitingGate; }));

    // This is the case a cancellation-only hook leaked: the job was never
    // cancelled, it was discarded as stale, and the consumer would have waited
    // forever on a result that is now forbidden to apply.
    (void)jobs.AdvanceWorldGeneration(world);

    EXPECT_EQ(jobs.DrainCompletions(events), 0u);
    EXPECT_EQ(jobs.GetState(token), Runtime::JobState::StaleDiscarded);
    EXPECT_EQ(probe.Calls, 1);
    EXPECT_TRUE(publishOrder.empty());
}

TEST(RuntimeJobService, DependencyCancelledJobFinalizesInTheSameDrain)
{
    SchedulerScope scheduler{2};
    Runtime::JobService jobs;
    Runtime::KernelEventBus events;

    std::vector<int> publishOrder;
    FinalizerProbe probe;

    const Runtime::JobToken first =
        jobs.Submit(MakeCountingJob("finalize.chain.first", 1, publishOrder));
    ASSERT_TRUE(first.IsValid());

    Runtime::JobDesc dependentDesc =
        MakeCountingJob("finalize.chain.second", 2, publishOrder);
    dependentDesc.DependsOn.push_back(
        Runtime::JobDependency{.Job = first, .Reason = "needs first"});
    dependentDesc.FinalizeUnpublishedOnMainThread = probe.Hook();
    const Runtime::JobToken second = jobs.Submit(std::move(dependentDesc));
    ASSERT_TRUE(second.IsValid());

    EXPECT_TRUE(jobs.Cancel(first));

    // The dependent never dispatched, so the dependency-release pass is the only
    // path that can reach it. Finalizers run after that pass within the same
    // drain, so the consumer is not left blocked for an extra frame. Drain until
    // the dependent is terminal — see the note in
    // DependentIsCancelledWhenUpstreamDoesNotPublish for why a fixed drain count
    // races the worker interleaving.
    ASSERT_TRUE(WaitUntil([&]
    {
        (void)jobs.DrainCompletions(events);
        return jobs.IsComplete(second);
    }));

    EXPECT_EQ(jobs.GetState(second), Runtime::JobState::Cancelled);
    EXPECT_EQ(probe.Calls, 1);
    EXPECT_EQ(probe.Thread, std::this_thread::get_id());
    EXPECT_TRUE(publishOrder.empty());
}

TEST(RuntimeJobService, PublishedJobNeverRunsTheUnpublishedFinalizer)
{
    SchedulerScope scheduler{2};
    Runtime::JobService jobs;
    Runtime::KernelEventBus events;

    std::vector<int> publishOrder;
    FinalizerProbe probe;

    Runtime::JobDesc desc = MakeCountingJob("finalize.published", 9, publishOrder);
    desc.FinalizeUnpublishedOnMainThread = probe.Hook();
    const Runtime::JobToken token = jobs.Submit(std::move(desc));
    ASSERT_TRUE(token.IsValid());

    ASSERT_TRUE(WaitUntil([&] { return jobs.GetState(token) ==
                                       Runtime::JobState::AwaitingGate; }));

    EXPECT_EQ(jobs.DrainCompletions(events), 1u);
    EXPECT_EQ(jobs.GetState(token), Runtime::JobState::Published);
    ASSERT_EQ(publishOrder.size(), 1u);
    EXPECT_EQ(publishOrder[0], 9);

    // Exactly one of the two runs, so a consumer can treat the finalizer as an
    // unambiguous "this will never apply" signal.
    EXPECT_EQ(probe.Calls, 0);
    EXPECT_EQ(jobs.Stats().FinalizedUnpublishedJobs, 0u);
}

TEST(RuntimeJobService, SnapshotAllEnumeratesRetainedJobsInTokenOrder)
{
    // RUNTIME-194 Slice B5 moved queue visibility here from
    // `DerivedJobRegistry::SnapshotAll()`. The editor queue view and the
    // shutdown survivor sweep both read this, so it has to enumerate every
    // retained job — including terminal ones — in an order that does not
    // reshuffle between frames.
    SchedulerScope scheduler{2};
    Runtime::JobService jobs;
    Runtime::KernelEventBus events;

    EXPECT_TRUE(jobs.SnapshotAll().empty());

    std::vector<int> publishOrder;
    const Runtime::WorldHandle world{7u, 1u};

    Runtime::JobDesc first = MakeCountingJob("snapshot.first", 1, publishOrder);
    first.Scope = world;
    const Runtime::JobToken firstToken = jobs.Submit(std::move(first));
    ASSERT_TRUE(firstToken.IsValid());

    const Runtime::JobToken secondToken =
        jobs.Submit(MakeCountingJob("snapshot.second", 2, publishOrder));
    ASSERT_TRUE(secondToken.IsValid());

    ASSERT_TRUE(WaitUntil(
        [&]
        {
            return jobs.GetState(firstToken) == Runtime::JobState::AwaitingGate &&
                   jobs.GetState(secondToken) == Runtime::JobState::AwaitingGate;
        }));

    std::vector<Runtime::JobSnapshot> pending = jobs.SnapshotAll();
    ASSERT_EQ(pending.size(), 2u);
    EXPECT_LT(pending[0].Token, pending[1].Token);
    EXPECT_EQ(pending[0].Token, firstToken);
    EXPECT_EQ(pending[0].DebugName, "snapshot.first");
    EXPECT_EQ(pending[0].Scope, world);
    EXPECT_EQ(pending[0].State, Runtime::JobState::AwaitingGate);
    EXPECT_EQ(pending[1].Token, secondToken);
    EXPECT_EQ(pending[1].DebugName, "snapshot.second");
    EXPECT_EQ(pending[1].Scope, Runtime::DefaultWorldHandle);

    // Terminal jobs stay visible until they are deliberately reaped, so a
    // queue view can show a completed row and the shutdown sweep can still see
    // what survived.
    EXPECT_EQ(jobs.DrainCompletions(events), 2u);
    const std::vector<Runtime::JobSnapshot> published = jobs.SnapshotAll();
    ASSERT_EQ(published.size(), 2u);
    EXPECT_EQ(published[0].State, Runtime::JobState::Published);
    EXPECT_EQ(published[1].State, Runtime::JobState::Published);

    EXPECT_EQ(jobs.ReapCompleted(), 2u);
    EXPECT_TRUE(jobs.SnapshotAll().empty());
}

TEST(RuntimeJobService, SnapshotAllCarriesReportedProgressAndAge)
{
    SchedulerScope scheduler{2};
    Runtime::JobService jobs;
    Runtime::KernelEventBus events;

    std::vector<int> publishOrder;
    const Runtime::JobToken token =
        jobs.Submit(MakeCountingJob("snapshot.progress", 3, publishOrder));
    ASSERT_TRUE(token.IsValid());

    jobs.ReportProgress(token, Runtime::JobProgress{
                                   .Normalized = 0.25f,
                                   .Determinate = false,
                               });

    ASSERT_TRUE(WaitUntil(
        [&]
        {
            const std::vector<Runtime::JobSnapshot> snapshot = jobs.SnapshotAll();
            return snapshot.size() == 1u &&
                   snapshot[0].ElapsedMilliseconds >= 1u;
        }));

    const std::vector<Runtime::JobSnapshot> snapshot = jobs.SnapshotAll();
    ASSERT_EQ(snapshot.size(), 1u);
    EXPECT_FLOAT_EQ(snapshot[0].Progress.Normalized, 0.25f);
    EXPECT_FALSE(snapshot[0].Progress.Determinate);

    ASSERT_TRUE(WaitUntil([&]
    {
        (void)jobs.DrainCompletions(events);
        return jobs.IsComplete(token);
    }));
}
