#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <utility>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.Core.FrameLoop;
import Extrinsic.Core.Tasks;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.StreamingExecutor;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

namespace Runtime = Extrinsic::Runtime;
namespace Core = Extrinsic::Core;

namespace
{
    using namespace std::chrono_literals;

    class AsyncWorkModuleHarness final
    {
    public:
        AsyncWorkModuleHarness()
            : Setup(
                  Commands,
                  Events,
                  Jobs,
                  Worlds,
                  Services,
                  [](Runtime::SimSystemDesc) {},
                  [](Runtime::FramePhase, Runtime::RuntimeFrameHook) {})
        {
        }

        ~AsyncWorkModuleHarness()
        {
            Shutdown();
        }

        [[nodiscard]] Core::Result Initialize()
        {
            Services.BeginRegistration();
            Core::Result result = Module.OnRegister(Setup);
            Registered = result.has_value();
            return result;
        }

        void Shutdown()
        {
            if (!Registered)
                return;

            Runtime::RuntimeModuleShutdownContext context{
                .Commands = Commands,
                .Events = Events,
                .Jobs = Jobs,
                .Worlds = Worlds,
                .Services = Services,
            };
            Module.OnShutdown(context);
            Services.Reset();
            Registered = false;
        }

        [[nodiscard]] Runtime::StreamingExecutor* Streaming() const
        {
            return Services.Find<Runtime::StreamingExecutor>();
        }

        [[nodiscard]] Runtime::DerivedJobRegistry* DerivedJobs() const
        {
            return Services.Find<Runtime::DerivedJobRegistry>();
        }

        [[nodiscard]] Core::IStreamingFrameHooks* FrameHooks() const
        {
            return Services.Find<Core::IStreamingFrameHooks>();
        }

        void AnnounceWorldRetirement(const Runtime::WorldHandle world)
        {
            Events.Publish(Runtime::WorldWillBeDestroyed{
                .World = world,
                .DebugName = "async-work-module-contract",
            });
            (void)Events.Pump();
        }

        Runtime::CommandBus Commands{};
        Runtime::KernelEventBus Events{};
        Runtime::JobService Jobs{};
        Runtime::WorldRegistry Worlds{};
        Runtime::ServiceRegistry Services{};
        Runtime::AsyncWorkModule Module{};
        Runtime::EngineSetup Setup;
        bool Registered{false};
    };

    class SchedulerScope final
    {
    public:
        explicit SchedulerScope(const unsigned workers = 1u)
        {
            if (Core::Tasks::Scheduler::IsInitialized())
                Core::Tasks::Scheduler::Shutdown();
            Core::Tasks::Scheduler::Initialize(workers);
        }

        ~SchedulerScope()
        {
            Core::Tasks::Scheduler::WaitForAll();
            Core::Tasks::Scheduler::Shutdown();
        }

        SchedulerScope(const SchedulerScope&) = delete;
        SchedulerScope& operator=(const SchedulerScope&) = delete;
    };

    class WorkerInterlock final
    {
    public:
        ~WorkerInterlock()
        {
            Release();
        }

        void EnterAndWait()
        {
            std::unique_lock lock(Mutex);
            Entered = true;
            Condition.notify_all();
            Condition.wait(lock, [this] { return Released; });
        }

        [[nodiscard]] bool WaitUntilEntered(
            const std::chrono::milliseconds timeout = 2s)
        {
            std::unique_lock lock(Mutex);
            return Condition.wait_for(
                lock,
                timeout,
                [this] { return Entered; });
        }

        void Release()
        {
            {
                std::scoped_lock lock(Mutex);
                Released = true;
            }
            Condition.notify_all();
        }

    private:
        std::mutex Mutex{};
        std::condition_variable Condition{};
        bool Entered{false};
        bool Released{false};
    };

    struct ShutdownLookupProbeState
    {
        bool ResolvedAsyncServices{false};
        bool ShutdownRan{false};
        bool SawOnlyWithdrawnAsyncServices{false};
    };

    class ShutdownLookupProbeModule final : public Runtime::IRuntimeModule
    {
    public:
        explicit ShutdownLookupProbeModule(ShutdownLookupProbeState& state)
            : m_State(state)
        {
        }

        [[nodiscard]] std::string_view Name() const noexcept override
        {
            // Engine sorts modules by name and shuts them down in reverse.
            // This probe must run after Runtime.AsyncWorkModule.
            return "A.AsyncWorkShutdownLookupProbe";
        }

        [[nodiscard]] Core::Result OnRegister(
            Runtime::EngineSetup&) override
        {
            return Core::Ok();
        }

        [[nodiscard]] Core::Result OnResolve(
            Runtime::EngineSetup& setup) override
        {
            m_State.ResolvedAsyncServices =
                setup.Services().Find<Runtime::StreamingExecutor>() != nullptr &&
                setup.Services().Find<Runtime::DerivedJobRegistry>() != nullptr &&
                setup.Services().Find<Core::IStreamingFrameHooks>() != nullptr;
            return m_State.ResolvedAsyncServices
                ? Core::Ok()
                : Core::Err(Core::ErrorCode::ResourceNotFound);
        }

        void OnShutdown(
            Runtime::RuntimeModuleShutdownContext& context) override
        {
            m_State.ShutdownRan = true;
            m_State.SawOnlyWithdrawnAsyncServices =
                context.Services.Find<Runtime::StreamingExecutor>() == nullptr &&
                context.Services.Find<Runtime::DerivedJobRegistry>() == nullptr &&
                context.Services.Find<Core::IStreamingFrameHooks>() == nullptr;
        }

    private:
        ShutdownLookupProbeState& m_State;
    };

    class NoopApplication final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(Runtime::Engine&, double, double) override {}
        void OnShutdown(Runtime::Engine&) override {}
    };

    [[nodiscard]] Core::Config::EngineConfig HeadlessConfig()
    {
        Core::Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend = Core::Config::WindowBackend::Null;
        return config;
    }
}

TEST(RuntimeAsyncWorkModule, RegistersDiscoverableServicesAndCanReinitialize)
{
    AsyncWorkModuleHarness harness{};
    ASSERT_TRUE(harness.Initialize().has_value());

    Runtime::StreamingExecutor* firstStreaming = harness.Streaming();
    Runtime::DerivedJobRegistry* firstDerived = harness.DerivedJobs();
    Core::IStreamingFrameHooks* firstHooks = harness.FrameHooks();
    EXPECT_NE(firstStreaming, nullptr);
    EXPECT_NE(firstDerived, nullptr);
    EXPECT_NE(firstHooks, nullptr);

    harness.AnnounceWorldRetirement(Runtime::DefaultWorldHandle);
    ASSERT_TRUE(firstStreaming->IsWorldRetired(Runtime::DefaultWorldHandle));
    EXPECT_FALSE(firstStreaming
                     ->Submit(Runtime::StreamingTaskDesc{
                         .Name = "retired before module reset",
                         .Scope = Runtime::DefaultWorldHandle,
                         .Execute = []() -> Runtime::StreamingResult
                         {
                             return Runtime::StreamingCpuPayloadReady{};
                         },
                     })
                     .IsValid());

    harness.Shutdown();
    EXPECT_EQ(harness.Streaming(), nullptr);
    EXPECT_EQ(harness.DerivedJobs(), nullptr);
    EXPECT_EQ(harness.FrameHooks(), nullptr);

    ASSERT_TRUE(harness.Initialize().has_value());
    ASSERT_NE(harness.Streaming(), nullptr);
    ASSERT_NE(harness.DerivedJobs(), nullptr);
    ASSERT_NE(harness.FrameHooks(), nullptr);

    // Recreating the module recreates executor lifetime state; a world scope
    // retired by the previous lifetime must not remain poisoned.
    const Runtime::StreamingTaskHandle handle =
        harness.Streaming()->Submit(Runtime::StreamingTaskDesc{
            .Name = "reinitialized async work",
            .Scope = Runtime::DefaultWorldHandle,
            .Execute = []() -> Runtime::StreamingResult
            {
                return Runtime::StreamingCpuPayloadReady{.PayloadToken = 1u};
            },
        });
    EXPECT_TRUE(handle.IsValid());
}

TEST(RuntimeAsyncWorkModule, RegistrationConflictDoesNotPublishPartialServices)
{
    AsyncWorkModuleHarness harness{};
    Runtime::StreamingExecutor externalStreaming{};
    Runtime::DerivedJobRegistry externalDerived{externalStreaming};
    harness.Services.BeginRegistration();
    ASSERT_TRUE(harness.Services
                    .Provide<Runtime::DerivedJobRegistry>(
                        externalDerived, "conflicting-test-provider")
                    .has_value());

    const Core::Result result = harness.Module.OnRegister(harness.Setup);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Core::ErrorCode::InvalidState);
    EXPECT_EQ(harness.Streaming(), nullptr);
    EXPECT_EQ(harness.DerivedJobs(), &externalDerived);
    EXPECT_EQ(harness.FrameHooks(), nullptr);

    harness.Services.Reset();
}

TEST(RuntimeAsyncWorkModule, ShutdownWithdrawsServicesBeforeLaterModulesRun)
{
    ShutdownLookupProbeState state{};
    Runtime::Engine engine(
        HeadlessConfig(), std::make_unique<NoopApplication>());
    engine.EmplaceModule<ShutdownLookupProbeModule>(state);
    engine.EmplaceModule<Runtime::AsyncWorkModule>();

    engine.Initialize();
    ASSERT_TRUE(state.ResolvedAsyncServices);
    engine.Shutdown();

    EXPECT_TRUE(state.ShutdownRan);
    EXPECT_TRUE(state.SawOnlyWithdrawnAsyncServices);
}

TEST(RuntimeAsyncWorkModule, ShutdownDrainsReadyDerivedReadbackBeforeReturn)
{
    AsyncWorkModuleHarness harness{};
    ASSERT_TRUE(harness.Initialize().has_value());

    std::atomic<std::uint32_t> workerCalls{0u};
    std::atomic<std::uint32_t> applyCalls{0u};
    Runtime::DerivedJobDesc readbackJob{
        .Name = "shutdown readback drain",
        .Scope = Runtime::DefaultWorldHandle,
        .IsReadbackJob = true,
        .ReadbackByteSize = 4u,
        .Execute = [&workerCalls]() -> Runtime::DerivedJobWorkerResult
        {
            workerCalls.fetch_add(1u, std::memory_order_relaxed);
            return Runtime::DerivedJobOutput{
                .PayloadToken = 76u,
                .Diagnostic = "worker complete; readback ready",
            };
        },
        .IsReadbackReady = []()
        {
            return true;
        },
        .ApplyOnMainThread =
            [&applyCalls](Runtime::DerivedJobApplyContext&) -> Core::Result
        {
            applyCalls.fetch_add(1u, std::memory_order_relaxed);
            return Core::Ok();
        },
    };

    const Runtime::DerivedJobHandle handle =
        harness.DerivedJobs()->Submit(std::move(readbackJob));
    ASSERT_TRUE(handle.IsValid());

    harness.FrameHooks()->PumpBackground(1u);
    harness.Shutdown();

    EXPECT_EQ(workerCalls.load(std::memory_order_relaxed), 1u);
    EXPECT_EQ(applyCalls.load(std::memory_order_relaxed), 1u);
}

TEST(RuntimeAsyncWorkModule, ShutdownCancelsUnreadiedDerivedReadback)
{
    AsyncWorkModuleHarness harness{};
    ASSERT_TRUE(harness.Initialize().has_value());

    std::atomic<bool> readbackReady{false};
    std::atomic<std::uint32_t> applyCalls{0u};
    Runtime::DerivedJobDesc readbackJob{
        .Name = "shutdown unreadied readback",
        .Scope = Runtime::DefaultWorldHandle,
        .IsReadbackJob = true,
        .ReadbackByteSize = 4u,
        .Execute = []() -> Runtime::DerivedJobWorkerResult
        {
            return Runtime::DerivedJobOutput{.PayloadToken = 77u};
        },
        .IsReadbackReady = [&readbackReady]()
        {
            return readbackReady.load(std::memory_order_relaxed);
        },
        .ApplyOnMainThread =
            [&applyCalls](Runtime::DerivedJobApplyContext&) -> Core::Result
        {
            applyCalls.fetch_add(1u, std::memory_order_relaxed);
            return Core::Ok();
        },
    };

    const Runtime::DerivedJobHandle handle =
        harness.DerivedJobs()->Submit(std::move(readbackJob));
    ASSERT_TRUE(handle.IsValid());

    harness.FrameHooks()->PumpBackground(1u);
    harness.Shutdown();

    readbackReady.store(true, std::memory_order_relaxed);
    EXPECT_EQ(applyCalls.load(std::memory_order_relaxed), 0u);
}

TEST(RuntimeAsyncWorkModule, WorldRetirementCancelsQueuedAndReadyWork)
{
    AsyncWorkModuleHarness harness{};
    ASSERT_TRUE(harness.Initialize().has_value());
    Runtime::StreamingExecutor* streaming = harness.Streaming();
    Runtime::DerivedJobRegistry* derived = harness.DerivedJobs();
    Core::IStreamingFrameHooks* hooks = harness.FrameHooks();
    ASSERT_NE(streaming, nullptr);
    ASSERT_NE(derived, nullptr);
    ASSERT_NE(hooks, nullptr);

    std::atomic<std::uint32_t> queuedCancellationFinalizers{0u};
    const Runtime::WorldHandle queuedWorld{17u, 3u};
    const Runtime::StreamingTaskHandle queued =
        streaming->Submit(Runtime::StreamingTaskDesc{
            .Name = "queued world-scoped task",
            .Scope = queuedWorld,
            .Execute = []() -> Runtime::StreamingResult
            {
                return Runtime::StreamingCpuPayloadReady{.PayloadToken = 10u};
            },
            .FinalizeCancellationOnMainThread =
                [&queuedCancellationFinalizers]
            {
                queuedCancellationFinalizers.fetch_add(
                    1u,
                    std::memory_order_relaxed);
            },
        });
    ASSERT_TRUE(queued.IsValid());

    harness.AnnounceWorldRetirement(queuedWorld);
    EXPECT_EQ(streaming->GetState(queued),
              Runtime::StreamingTaskState::Cancelled);
    hooks->ApplyMainThreadResults();
    EXPECT_EQ(
        queuedCancellationFinalizers.load(std::memory_order_relaxed),
        1u);
    hooks->ApplyMainThreadResults();
    EXPECT_EQ(
        queuedCancellationFinalizers.load(std::memory_order_relaxed),
        1u);
    EXPECT_TRUE(streaming->IsWorldRetired(queuedWorld));
    EXPECT_FALSE(streaming
                     ->Submit(Runtime::StreamingTaskDesc{
                         .Name = "rejected retired-world task",
                         .Scope = queuedWorld,
                         .Execute = []() -> Runtime::StreamingResult
                         {
                             return Runtime::StreamingCpuPayloadReady{};
                         },
                     })
                     .IsValid());

    // Retirement keys include the generation: index reuse for a newly created
    // world must not inherit the previous world's terminal state.
    const Runtime::WorldHandle reusedIndexWorld{
        queuedWorld.Index,
        queuedWorld.Generation + 1u,
    };
    EXPECT_TRUE(streaming
                    ->Submit(Runtime::StreamingTaskDesc{
                        .Name = "new generation accepted",
                        .Scope = reusedIndexWorld,
                        .Execute = []() -> Runtime::StreamingResult
                        {
                            return Runtime::StreamingCpuPayloadReady{};
                        },
                    })
                    .IsValid());

    std::atomic<std::uint32_t> rawApplyCalls{0u};
    std::atomic<std::uint32_t> rawCancellationFinalizers{0u};
    const Runtime::WorldHandle readyRawWorld{18u, 4u};
    const Runtime::StreamingTaskHandle readyRaw =
        streaming->Submit(Runtime::StreamingTaskDesc{
            .Name = "ready raw task",
            .Scope = readyRawWorld,
            .Execute = []() -> Runtime::StreamingResult
            {
                return Runtime::StreamingCpuPayloadReady{.PayloadToken = 11u};
            },
            .ApplyOnMainThread =
                [&rawApplyCalls](Runtime::StreamingResult&&)
            {
                rawApplyCalls.fetch_add(1u, std::memory_order_relaxed);
            },
            .FinalizeCancellationOnMainThread =
                [&rawCancellationFinalizers]
            {
                rawCancellationFinalizers.fetch_add(
                    1u,
                    std::memory_order_relaxed);
            },
        });
    ASSERT_TRUE(readyRaw.IsValid());
    hooks->PumpBackground(8u);
    hooks->DrainCompletions();
    ASSERT_EQ(streaming->GetState(readyRaw),
              Runtime::StreamingTaskState::WaitingForMainThreadApply);

    harness.AnnounceWorldRetirement(readyRawWorld);
    hooks->ApplyMainThreadResults();
    EXPECT_EQ(streaming->GetState(readyRaw),
              Runtime::StreamingTaskState::Cancelled);
    EXPECT_EQ(rawApplyCalls.load(std::memory_order_relaxed), 0u);
    EXPECT_EQ(
        rawCancellationFinalizers.load(std::memory_order_relaxed),
        1u);

    std::atomic<std::uint32_t> readbackApplyCalls{0u};
    std::atomic<std::uint32_t> readbackCancellationFinalizers{0u};
    const Runtime::WorldHandle readbackWorld{22u, 8u};
    const Runtime::StreamingTaskHandle waitingReadback =
        streaming->Submit(Runtime::StreamingTaskDesc{
            .Name = "waiting raw readback task",
            .Scope = readbackWorld,
            .Execute = []() -> Runtime::StreamingResult
            {
                return Runtime::StreamingReadbackRequest{
                    .PayloadToken = 14u,
                    .ByteSize = 4u,
                };
            },
            .ApplyOnMainThread =
                [&readbackApplyCalls](Runtime::StreamingResult&&)
            {
                readbackApplyCalls.fetch_add(
                    1u,
                    std::memory_order_relaxed);
            },
            .FinalizeCancellationOnMainThread =
                [&readbackCancellationFinalizers]
            {
                readbackCancellationFinalizers.fetch_add(
                    1u,
                    std::memory_order_relaxed);
            },
        });
    ASSERT_TRUE(waitingReadback.IsValid());
    hooks->PumpBackground(8u);
    hooks->DrainCompletions();
    ASSERT_EQ(
        streaming->GetState(waitingReadback),
        Runtime::StreamingTaskState::WaitingForReadback);

    harness.AnnounceWorldRetirement(readbackWorld);
    hooks->ApplyMainThreadResults();
    EXPECT_EQ(
        streaming->GetState(waitingReadback),
        Runtime::StreamingTaskState::Cancelled);
    EXPECT_EQ(
        readbackApplyCalls.load(std::memory_order_relaxed),
        0u);
    EXPECT_EQ(
        readbackCancellationFinalizers.load(std::memory_order_relaxed),
        1u);

    std::atomic<std::uint32_t> derivedApplyCalls{0u};
    std::atomic<bool> derivedReadbackReady{false};
    const Runtime::WorldHandle readyDerivedWorld{19u, 5u};
    const Runtime::DerivedJobHandle readyDerived =
        derived->Submit(Runtime::DerivedJobDesc{
            .Name = "ready derived readback task",
            .Scope = readyDerivedWorld,
            .IsReadbackJob = true,
            .ReadbackByteSize = 16u,
            .Execute = []() -> Runtime::DerivedJobWorkerResult
            {
                return Runtime::DerivedJobOutput{.PayloadToken = 12u};
            },
            .IsReadbackReady = [&derivedReadbackReady]()
            {
                return derivedReadbackReady.load(std::memory_order_relaxed);
            },
            .ApplyOnMainThread =
                [&derivedApplyCalls](Runtime::DerivedJobApplyContext&)
                    -> Core::Result
            {
                derivedApplyCalls.fetch_add(1u, std::memory_order_relaxed);
                return Core::Ok();
            },
        });
    ASSERT_TRUE(readyDerived.IsValid());
    hooks->PumpBackground(8u);
    hooks->DrainCompletions();
    const auto readyDerivedSnapshot = derived->Snapshot(readyDerived);
    ASSERT_TRUE(readyDerivedSnapshot.has_value());
    ASSERT_EQ(readyDerivedSnapshot->ExecutionState,
              Runtime::StreamingTaskState::WaitingForReadback);

    harness.AnnounceWorldRetirement(readyDerivedWorld);
    derivedReadbackReady.store(true, std::memory_order_relaxed);
    hooks->DrainCompletions();
    hooks->ApplyMainThreadResults();
    EXPECT_EQ(derived->GetStatus(readyDerived),
              Runtime::DerivedJobStatus::Cancelled);
    EXPECT_EQ(derivedApplyCalls.load(std::memory_order_relaxed), 0u);
}

TEST(RuntimeAsyncWorkModule, WorldRegistryAnnouncementRetiresBeforeDestruction)
{
    AsyncWorkModuleHarness harness{};
    ASSERT_TRUE(harness.Initialize().has_value());

    const Runtime::WorldHandle active =
        harness.Worlds.CreateWorld("active");
    const Runtime::WorldHandle doomed =
        harness.Worlds.CreateWorld("doomed");
    ASSERT_EQ(harness.Worlds.ActiveWorld(), active);
    const Runtime::StreamingTaskHandle task =
        harness.Streaming()->Submit(Runtime::StreamingTaskDesc{
            .Name = "world-registry retirement ordering",
            .Scope = doomed,
            .Execute = []() -> Runtime::StreamingResult
            {
                return Runtime::StreamingCpuPayloadReady{};
            },
        });
    ASSERT_TRUE(task.IsValid());
    ASSERT_TRUE(harness.Worlds.RequestDestroyWorld(doomed).has_value());

    const Runtime::WorldRegistryMaintenanceStats announced =
        harness.Worlds.ApplyMaintenance(harness.Events, harness.Jobs);
    EXPECT_EQ(announced.DestroyAnnouncements, 1u);
    EXPECT_EQ(announced.DestroyedWorlds, 0u);
    EXPECT_TRUE(harness.Worlds.Contains(doomed));
    EXPECT_FALSE(harness.Streaming()->IsWorldRetired(doomed));

    (void)harness.Events.Pump();
    EXPECT_TRUE(harness.Streaming()->IsWorldRetired(doomed));
    EXPECT_EQ(
        harness.Streaming()->GetState(task),
        Runtime::StreamingTaskState::Cancelled);
    EXPECT_TRUE(harness.Worlds.Contains(doomed));

    const Runtime::WorldRegistryMaintenanceStats destroyed =
        harness.Worlds.ApplyMaintenance(harness.Events, harness.Jobs);
    EXPECT_EQ(destroyed.DestroyAnnouncements, 0u);
    EXPECT_EQ(destroyed.DestroyedWorlds, 1u);
    EXPECT_FALSE(harness.Worlds.Contains(doomed));
}

TEST(RuntimeAsyncWorkModule, WorldRetirementSuppressesRunningWorkApply)
{
    SchedulerScope scheduler{1u};
    AsyncWorkModuleHarness harness{};
    ASSERT_TRUE(harness.Initialize().has_value());

    WorkerInterlock interlock{};
    std::atomic<std::uint32_t> applyCalls{0u};
    std::atomic<std::uint32_t> cancellationFinalizers{0u};
    const Runtime::WorldHandle runningWorld{20u, 6u};
    const Runtime::StreamingTaskHandle running =
        harness.Streaming()->Submit(Runtime::StreamingTaskDesc{
            .Name = "running world-scoped task",
            .Scope = runningWorld,
            .Execute = [&interlock]() -> Runtime::StreamingResult
            {
                interlock.EnterAndWait();
                return Runtime::StreamingCpuPayloadReady{.PayloadToken = 13u};
            },
            .ApplyOnMainThread =
                [&applyCalls](Runtime::StreamingResult&&)
            {
                applyCalls.fetch_add(1u, std::memory_order_relaxed);
            },
            .FinalizeCancellationOnMainThread =
                [&cancellationFinalizers]
            {
                cancellationFinalizers.fetch_add(
                    1u,
                    std::memory_order_relaxed);
            },
        });
    ASSERT_TRUE(running.IsValid());

    harness.FrameHooks()->PumpBackground(1u);
    ASSERT_TRUE(interlock.WaitUntilEntered());
    EXPECT_EQ(harness.Streaming()->GetState(running),
              Runtime::StreamingTaskState::Running);

    harness.AnnounceWorldRetirement(runningWorld);
    EXPECT_EQ(harness.Streaming()->GetState(running),
              Runtime::StreamingTaskState::Cancelled);
    harness.FrameHooks()->ApplyMainThreadResults();
    EXPECT_EQ(
        cancellationFinalizers.load(std::memory_order_relaxed),
        1u);

    interlock.Release();
    Core::Tasks::Scheduler::WaitForAll();
    harness.FrameHooks()->DrainCompletions();
    harness.FrameHooks()->ApplyMainThreadResults();
    EXPECT_EQ(harness.Streaming()->GetState(running),
              Runtime::StreamingTaskState::Cancelled);
    EXPECT_EQ(applyCalls.load(std::memory_order_relaxed), 0u);
    EXPECT_EQ(
        cancellationFinalizers.load(std::memory_order_relaxed),
        1u);
}

TEST(RuntimeAsyncWorkModule, RunningDerivedCancellationSurvivesLateWorkerFailure)
{
    SchedulerScope scheduler{1u};
    AsyncWorkModuleHarness harness{};
    ASSERT_TRUE(harness.Initialize().has_value());

    WorkerInterlock interlock{};
    std::atomic<std::uint32_t> applyCalls{0u};
    const Runtime::DerivedJobHandle running =
        harness.DerivedJobs()->Submit(Runtime::DerivedJobDesc{
            .Name = "cancelled running derived failure",
            .Scope = Runtime::WorldHandle{21u, 7u},
            .Execute = [&interlock]() -> Runtime::DerivedJobWorkerResult
            {
                interlock.EnterAndWait();
                return std::unexpected(Core::ErrorCode::AssetDecodeFailed);
            },
            .ApplyOnMainThread =
                [&applyCalls](Runtime::DerivedJobApplyContext&) -> Core::Result
            {
                applyCalls.fetch_add(1u, std::memory_order_relaxed);
                return Core::Ok();
            },
        });
    ASSERT_TRUE(running.IsValid());

    harness.FrameHooks()->PumpBackground(1u);
    ASSERT_TRUE(interlock.WaitUntilEntered());
    const auto runningSnapshot = harness.DerivedJobs()->Snapshot(running);
    ASSERT_TRUE(runningSnapshot.has_value());
    EXPECT_EQ(
        runningSnapshot->ExecutionState,
        Runtime::StreamingTaskState::Running);

    harness.DerivedJobs()->Cancel(running);
    EXPECT_EQ(
        harness.DerivedJobs()->GetStatus(running),
        Runtime::DerivedJobStatus::Cancelled);

    interlock.Release();
    Core::Tasks::Scheduler::WaitForAll();
    harness.FrameHooks()->DrainCompletions();
    harness.FrameHooks()->ApplyMainThreadResults();

    const auto cancelledSnapshot = harness.DerivedJobs()->Snapshot(running);
    ASSERT_TRUE(cancelledSnapshot.has_value());
    EXPECT_EQ(
        cancelledSnapshot->Status,
        Runtime::DerivedJobStatus::Cancelled);
    EXPECT_EQ(
        cancelledSnapshot->ExecutionState,
        Runtime::StreamingTaskState::Cancelled);
    EXPECT_EQ(cancelledSnapshot->Diagnostic, "cancelled");
    EXPECT_EQ(applyCalls.load(std::memory_order_relaxed), 0u);
}
