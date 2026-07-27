#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <string_view>
#include <utility>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.Core.Tasks;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

namespace Runtime = Extrinsic::Runtime;
namespace Core = Extrinsic::Core;

namespace
{
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

        [[nodiscard]] Runtime::JobService* JobSurface() const
        {
            return Services.Find<Runtime::JobService>();
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
        SchedulerScope()
        {
            if (Core::Tasks::Scheduler::IsInitialized())
                Core::Tasks::Scheduler::Shutdown();
            Core::Tasks::Scheduler::Initialize(1u);
        }

        ~SchedulerScope()
        {
            Core::Tasks::Scheduler::WaitForAll();
            Core::Tasks::Scheduler::Shutdown();
        }

        SchedulerScope(const SchedulerScope&) = delete;
        SchedulerScope& operator=(const SchedulerScope&) = delete;
    };

    struct ShutdownLookupProbeState
    {
        bool ResolvedBorrowedJobService{false};
        bool ShutdownRan{false};
        bool SawWithdrawnJobService{false};
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

        [[nodiscard]] Core::Result OnRegister(Runtime::EngineSetup&) override
        {
            return Core::Ok();
        }

        [[nodiscard]] Core::Result OnResolve(
            Runtime::EngineSetup& setup) override
        {
            m_State.ResolvedBorrowedJobService =
                setup.Services().Find<Runtime::JobService>() == &setup.Jobs();
            return m_State.ResolvedBorrowedJobService
                ? Core::Ok()
                : Core::Err(Core::ErrorCode::ResourceNotFound);
        }

        void OnShutdown(
            Runtime::RuntimeModuleShutdownContext& context) override
        {
            m_State.ShutdownRan = true;
            m_State.SawWithdrawnJobService =
                context.Services.Find<Runtime::JobService>() == nullptr;
        }

    private:
        ShutdownLookupProbeState& m_State;
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

TEST(RuntimeAsyncWorkModule, PublishesBorrowedJobServiceAndCanReinitialize)
{
    AsyncWorkModuleHarness harness{};
    ASSERT_TRUE(harness.Initialize().has_value());
    EXPECT_EQ(harness.JobSurface(), &harness.Jobs);

    harness.Shutdown();
    EXPECT_EQ(harness.JobSurface(), nullptr);

    ASSERT_TRUE(harness.Initialize().has_value());
    EXPECT_EQ(harness.JobSurface(), &harness.Jobs);
}

TEST(RuntimeAsyncWorkModule, RegistrationConflictPreservesExistingProvider)
{
    AsyncWorkModuleHarness harness{};
    Runtime::JobService externalJobs{};
    harness.Services.BeginRegistration();
    ASSERT_TRUE(harness.Services
                    .Provide<Runtime::JobService>(
                        externalJobs, "conflicting-test-provider")
                    .has_value());

    const Core::Result result = harness.Module.OnRegister(harness.Setup);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Core::ErrorCode::InvalidState);
    EXPECT_EQ(harness.JobSurface(), &externalJobs);

    harness.Services.Reset();
}

TEST(RuntimeAsyncWorkModule, ShutdownWithdrawsServiceBeforeLaterModulesRun)
{
    ShutdownLookupProbeState state{};
    Runtime::Engine engine(HeadlessConfig());
    engine.EmplaceModule<ShutdownLookupProbeModule>(state);
    engine.EmplaceModule<Runtime::AsyncWorkModule>();

    engine.Initialize();
    ASSERT_TRUE(state.ResolvedBorrowedJobService);
    engine.Shutdown();

    EXPECT_TRUE(state.ShutdownRan);
    EXPECT_TRUE(state.SawWithdrawnJobService);
}

TEST(RuntimeAsyncWorkModule, ShutdownCancelsParkedJobServiceSurvivor)
{
    AsyncWorkModuleHarness harness{};
    SchedulerScope scheduler{};
    ASSERT_TRUE(harness.Initialize().has_value());

    std::atomic<std::uint32_t> publishCalls{0u};
    std::atomic<std::uint32_t> finalizerCalls{0u};
    Runtime::JobDesc parkedJob{
        .DebugName = "shutdown parked JobService survivor",
        .Scope = Runtime::DefaultWorldHandle,
        .Work = [](const Runtime::JobCancellation&)
            -> Runtime::JobResultEnvelope
        {
            return Runtime::JobResultEnvelope::Make<std::uint64_t>(77u);
        },
        .IsReadyToApply = []()
        {
            return false;
        },
        .PublishCompletion =
            [&publishCalls](Runtime::KernelEventBus&,
                            const Runtime::JobResultEnvelope&) -> bool
        {
            publishCalls.fetch_add(1u, std::memory_order_relaxed);
            return true;
        },
        .FinalizeUnpublishedOnMainThread = [&finalizerCalls]()
        {
            finalizerCalls.fetch_add(1u, std::memory_order_relaxed);
        },
    };

    const Runtime::JobToken token = harness.Jobs.Submit(std::move(parkedJob));
    ASSERT_TRUE(token.IsValid());
    Core::Tasks::Scheduler::WaitForAll();
    ASSERT_EQ(harness.Jobs.GetState(token), Runtime::JobState::AwaitingGate);

    harness.Shutdown();

    EXPECT_EQ(harness.Jobs.DrainCompletions(harness.Events), 0u);
    EXPECT_EQ(harness.Jobs.GetState(token), Runtime::JobState::Cancelled);
    EXPECT_EQ(publishCalls.load(std::memory_order_relaxed), 0u);
    EXPECT_EQ(finalizerCalls.load(std::memory_order_relaxed), 1u);
}
