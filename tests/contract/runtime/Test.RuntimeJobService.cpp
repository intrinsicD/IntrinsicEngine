#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

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
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend = CoreConfig::WindowBackend::Null;
        return config;
    }

    class JobPumpProbeApplication final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine& engine) override
        {
            MainThread = std::this_thread::get_id();
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

        void OnSimTick(Runtime::Engine&, double) override {}

        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
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

        void OnShutdown(Runtime::Engine&) override {}

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

    Runtime::Engine engine(NullWindowHeadlessConfig(), std::move(app));
    engine.Initialize();

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on headless hosts";

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
