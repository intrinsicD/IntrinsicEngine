#include <gtest/gtest.h>

#include <concepts>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Platform.Input;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.FramePacingDiagnostics;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ModuleSchedule;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

namespace Core = Extrinsic::Core;
namespace CoreConfig = Extrinsic::Core::Config;
namespace Runtime = Extrinsic::Runtime;
namespace RHI = Extrinsic::RHI;

static_assert(static_cast<std::uint8_t>(Runtime::FramePhase::Maintenance) == 4u);

template <typename T>
concept HasPhaseMember = requires(T value)
{
    value.Phase;
};

template <typename T>
concept HasStatsMethod = requires(T value)
{
    value.Stats();
};

template <typename T>
concept HasBootErrorsList = requires(T value)
{
    value.BootErrors();
};

template <typename T>
concept HasRegisterSimSystem = requires
{
    &T::RegisterSimSystem;
};

static_assert(!HasPhaseMember<Runtime::RuntimeFrameHookContext>);
static_assert(!HasStatsMethod<Runtime::ServiceRegistry>);
static_assert(!HasBootErrorsList<Runtime::ServiceRegistry>);
static_assert(!HasRegisterSimSystem<Runtime::EngineSetup>);
static_assert(std::same_as<
              decltype(std::declval<Runtime::RuntimeModuleSchedule&>().FinalizeForBoot()),
              void>);

namespace
{
    struct SharedProbeService
    {
        int Value{17};
    };

    struct MissingProbeService
    {
    };

    struct ProbeCommand
    {
        int Value{0};
    };

    struct ProbeCommandHandled
    {
        int Value{0};
    };

    [[nodiscard]] CoreConfig::EngineConfig NullWindowHeadlessConfig()
    {
        CoreConfig::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled       = false;
        config.Camera.Enabled               = false;
        config.Window.Backend               = CoreConfig::WindowBackend::Null;
        return config;
    }

    struct ModuleHarnessState
    {
        bool ProviderRegistered{false};
        bool ConsumerResolved{false};
        bool RegisterSawUninitialized{false};
        bool ResolveSawUninitialized{false};
        bool AppResolveSawUninitialized{false};
        bool AppShutdownSawAnnounce{false};
        bool AnnouncementSawUninitialized{false};
        bool ProviderShutdownSawAnnounce{false};
        bool ConsumerShutdownSawAnnounce{false};
        bool ResolveFrameRegistrarUnavailable{false};
        bool ResolveViewportRegistrarUnavailable{false};
        bool CommandContextHadEvents{false};
        bool CommandContextHadJobs{false};
        bool CommandContextHadWorlds{false};
        bool CommandContextWorldWasActive{false};
        bool ShutdownAnnounced{false};
        bool ServiceVisibleWhileLocked{false};
        bool ServiceGoneAfterShutdown{false};
        int CommandEventHits{0};
        int CommandEventValue{0};
        std::uint32_t Frames{0};
        SharedProbeService* ResolvedService{};
        const bool* InitializedState{};
        std::vector<std::string> HookTrace{};
        std::vector<std::string> ShutdownTrace{};
    };

    class ProviderModule final : public Runtime::IRuntimeModule
    {
    public:
        ProviderModule(SharedProbeService& service, ModuleHarnessState& state)
            : m_Service(service)
            , m_State(state)
        {
        }

        [[nodiscard]] std::string_view Name() const noexcept override
        {
            return "Z.Provider";
        }

        [[nodiscard]] Core::Result OnRegister(Runtime::EngineSetup& setup) override
        {
            if (Core::Result provided =
                    setup.Services().Provide<SharedProbeService>(m_Service, Name());
                !provided.has_value())
            {
                return provided;
            }
            m_State.ProviderRegistered = true;

            setup.RegisterCommandHandler<ProbeCommand>(
                [this](Runtime::CommandContext& context,
                       const ProbeCommand& command) -> Runtime::CommandOutcome
                {
                    m_State.CommandContextHadEvents = context.Events != nullptr;
                    m_State.CommandContextHadJobs   = context.Jobs != nullptr;
                    m_State.CommandContextHadWorlds = context.Worlds != nullptr;
                    m_State.CommandContextWorldWasActive =
                        context.Worlds != nullptr &&
                        context.Worlds->Get(context.Worlds->ActiveWorld()) ==
                            &context.ActiveWorld;
                    if (context.Events != nullptr)
                        context.Events->Publish(ProbeCommandHandled{command.Value});
                    return Runtime::CommandOutcome::Ok();
                });

            if (Core::Result hook = setup.RegisterFrameHook(
                    Runtime::FramePhase::UiBuild,
                    [this](Runtime::RuntimeFrameHookContext&)
                    { m_State.HookTrace.emplace_back("Z.Provider:UiBuild"); });
                !hook.has_value())
            {
                return hook;
            }
            return setup.RegisterFrameHook(
                Runtime::FramePhase::Maintenance,
                [this](Runtime::RuntimeFrameHookContext&)
                { m_State.HookTrace.emplace_back("Z.Provider:Maintenance"); });
        }

        void OnShutdown(Runtime::RuntimeModuleShutdownContext& context) override
        {
            m_State.ProviderShutdownSawAnnounce = m_State.ShutdownAnnounced;
            m_State.ShutdownTrace.emplace_back("shutdown:provider");
            EXPECT_TRUE(context.Services.Withdraw(m_Service).has_value());
        }

    private:
        SharedProbeService& m_Service;
        ModuleHarnessState& m_State;
    };

    class ConsumerModule final : public Runtime::IRuntimeModule
    {
    public:
        explicit ConsumerModule(ModuleHarnessState& state)
            : m_State(state)
        {
        }

        [[nodiscard]] std::string_view Name() const noexcept override
        {
            return "A.Consumer";
        }

        [[nodiscard]] Core::Result OnRegister(Runtime::EngineSetup& setup) override
        {
            m_State.InitializedState          = setup.InitializedState();
            m_State.RegisterSawUninitialized =
                m_State.InitializedState != nullptr && !*m_State.InitializedState;
            m_ProbeSubscription = setup.Subscribe<ProbeCommandHandled>(
                [this](const ProbeCommandHandled& event)
                {
                    ++m_State.CommandEventHits;
                    m_State.CommandEventValue = event.Value;
                });
            m_ShutdownSubscription = setup.Subscribe<Runtime::RuntimeShutdownAnnounced>(
                [this](const Runtime::RuntimeShutdownAnnounced&)
                {
                    m_State.ShutdownAnnounced = true;
                    m_State.AnnouncementSawUninitialized =
                        m_State.InitializedState != nullptr && !*m_State.InitializedState;
                    m_State.ShutdownTrace.emplace_back("event:shutdown");
                });
            return setup.RegisterFrameHook(
                Runtime::FramePhase::UiBuild,
                [this](Runtime::RuntimeFrameHookContext&)
                { m_State.HookTrace.emplace_back("A.Consumer:UiBuild"); });
        }

        [[nodiscard]] Core::Result OnResolve(Runtime::EngineSetup& setup) override
        {
            m_State.ResolveSawUninitialized =
                setup.InitializedState() == m_State.InitializedState &&
                m_State.InitializedState != nullptr && !*m_State.InitializedState;
            auto required = setup.Services().Require<SharedProbeService>(Name());
            if (!required.has_value())
                return Core::Err(required.error());

            m_State.ResolvedService = &required->get();
            m_State.ConsumerResolved = true;

            const Core::Result frame = setup.RegisterFrameHook(
                Runtime::FramePhase::Maintenance,
                [](Runtime::RuntimeFrameHookContext&) {});
            m_State.ResolveFrameRegistrarUnavailable =
                !frame.has_value() && frame.error() == Core::ErrorCode::InvalidState;

            const Core::Result viewport = setup.RegisterViewportInputHook(
                [](Runtime::RuntimeViewportInputHookContext&) {});
            m_State.ResolveViewportRegistrarUnavailable =
                !viewport.has_value() && viewport.error() == Core::ErrorCode::InvalidState;
            return Core::Ok();
        }

        void OnShutdown(Runtime::RuntimeModuleShutdownContext&) override
        {
            m_State.ConsumerShutdownSawAnnounce = m_State.ShutdownAnnounced;
            m_State.ShutdownTrace.emplace_back("shutdown:consumer");
        }

    private:
        ModuleHarnessState& m_State;
        Runtime::KernelEventSubscription m_ProbeSubscription{};
        Runtime::KernelEventSubscription m_ShutdownSubscription{};
    };

    class MissingRequireModule final : public Runtime::IRuntimeModule
    {
    public:
        [[nodiscard]] std::string_view Name() const noexcept override
        {
            return "MissingRequire";
        }

        [[nodiscard]] Core::Result OnRegister(Runtime::EngineSetup&) override
        {
            return Core::Ok();
        }

        [[nodiscard]] Core::Result OnResolve(Runtime::EngineSetup& setup) override
        {
            (void)setup.Services().Require<MissingProbeService>(Name());
            return Core::Ok();
        }

        void OnShutdown(Runtime::RuntimeModuleShutdownContext&) override {}
    };

    class HarnessApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        explicit HarnessApp(ModuleHarnessState& state)
            : m_State(state)
        {
        }

        void Resolve() override
        {
            m_State.AppResolveSawUninitialized =
                m_State.InitializedState != nullptr && !*m_State.InitializedState;
            Kernel().Commands().Enqueue(ProbeCommand{23});
        }

        void Frame(double, double) override
        {
            ++m_State.Frames;
            Kernel().RequestExit();
        }

        void Shutdown() override
        {
            m_State.AppShutdownSawAnnounce = m_State.ShutdownAnnounced;
            m_State.ShutdownTrace.emplace_back("shutdown:application");
        }

    private:
        ModuleHarnessState& m_State;
    };

    [[nodiscard]] ModuleHarnessState RunHarness(const bool providerFirst)
    {
        ModuleHarnessState state{};
        SharedProbeService service{};
        auto app = std::make_unique<HarnessApp>(state);
        Intrinsic::Tests::RuntimeTestKernel engine(NullWindowHeadlessConfig(), std::move(app));

        if (providerFirst)
        {
            engine.AddModule(std::make_unique<ProviderModule>(service, state));
            engine.AddModule(std::make_unique<ConsumerModule>(state));
        }
        else
        {
            engine.AddModule(std::make_unique<ConsumerModule>(state));
            engine.AddModule(std::make_unique<ProviderModule>(service, state));
        }

        engine.Initialize();
        state.ServiceVisibleWhileLocked =
            engine.Services().Phase() == Runtime::ServiceRegistryPhase::Locked &&
            engine.Services().Find<SharedProbeService>() == &service;
        engine.Run();
        engine.Shutdown();
        state.ServiceGoneAfterShutdown =
            engine.Services().Find<SharedProbeService>() == nullptr;
        return state;
    }
}

TEST(RuntimeModule, EngineComposesRetainedHooksServicesAndLifecycle)
{
    const ModuleHarnessState state = RunHarness(true);

    EXPECT_TRUE(state.ProviderRegistered);
    EXPECT_TRUE(state.ConsumerResolved);
    EXPECT_TRUE(state.RegisterSawUninitialized);
    EXPECT_TRUE(state.ResolveSawUninitialized);
    EXPECT_TRUE(state.AppResolveSawUninitialized);
    EXPECT_TRUE(state.ResolveFrameRegistrarUnavailable);
    EXPECT_TRUE(state.ResolveViewportRegistrarUnavailable);
    EXPECT_TRUE(state.CommandContextHadEvents);
    EXPECT_TRUE(state.CommandContextHadJobs);
    EXPECT_TRUE(state.CommandContextHadWorlds);
    EXPECT_TRUE(state.CommandContextWorldWasActive);
    EXPECT_EQ(state.CommandEventHits, 1);
    EXPECT_EQ(state.CommandEventValue, 23);
    EXPECT_EQ(state.Frames, 1u);
    EXPECT_TRUE(state.ServiceVisibleWhileLocked);
    EXPECT_TRUE(state.ServiceGoneAfterShutdown);
    EXPECT_TRUE(state.AppShutdownSawAnnounce);
    EXPECT_TRUE(state.AnnouncementSawUninitialized);
    EXPECT_TRUE(state.ProviderShutdownSawAnnounce);
    EXPECT_TRUE(state.ConsumerShutdownSawAnnounce);
    EXPECT_EQ(state.HookTrace,
              (std::vector<std::string>{
                  "A.Consumer:UiBuild",
                  "Z.Provider:UiBuild",
                  "Z.Provider:Maintenance",
              }));
    EXPECT_EQ(state.ShutdownTrace,
              (std::vector<std::string>{
                  "event:shutdown",
                  "shutdown:application",
                  "shutdown:provider",
                  "shutdown:consumer",
              }));
}

TEST(RuntimeModule, RegistrationOrderDoesNotChangeRetainedHookOrder)
{
    const ModuleHarnessState providerFirst = RunHarness(true);
    const ModuleHarnessState consumerFirst = RunHarness(false);
    EXPECT_EQ(providerFirst.HookTrace, consumerFirst.HookTrace);
    EXPECT_EQ(providerFirst.ShutdownTrace, consumerFirst.ShutdownTrace);
}

TEST(RuntimeModule, MissingRequiredServiceTerminatesEngineBoot)
{
    EXPECT_DEATH(
        {
            Intrinsic::Tests::RuntimeTestKernel engine(NullWindowHeadlessConfig());
            engine.AddModule(std::make_unique<MissingRequireModule>());
            engine.Initialize();
        },
        "");
}

TEST(RuntimeModule, EnginePublishesOnlyConsumedBuiltInServices)
{
    Intrinsic::Tests::RuntimeTestKernel engine(NullWindowHeadlessConfig());
    engine.Initialize();

    EXPECT_EQ(engine.Services().Find<Runtime::CommandBus>(), nullptr);
    EXPECT_EQ(engine.Services().Find<Runtime::KernelEventBus>(), nullptr);
    EXPECT_EQ(engine.Services().Find<Runtime::WorldRegistry>(), nullptr);
    EXPECT_EQ(engine.Services().Find<Runtime::JobService>(), &engine.Jobs());
    EXPECT_EQ(engine.Services().Find<RHI::IDevice>(), &engine.GetDevice());
    EXPECT_NE(engine.Services().Find<Runtime::RenderExtractionCache>(), nullptr);

    engine.Shutdown();
    EXPECT_EQ(engine.Services().Find<Runtime::JobService>(), nullptr);
    EXPECT_EQ(engine.Services().Find<RHI::IDevice>(), nullptr);
}

TEST(ServiceRegistry, RetainsExactProvideRequireWithdrawAndBootDiagnostics)
{
    Runtime::ServiceRegistry services;
    SharedProbeService service{};
    SharedProbeService other{};

    services.BeginRegistration();
    EXPECT_TRUE(services.Provide(service, "provider").has_value());
    EXPECT_EQ(services.Find<SharedProbeService>(), &service);
    EXPECT_FALSE(services.Provide(other, "duplicate").has_value());
    EXPECT_TRUE(services.HasBootErrors());
    EXPECT_FALSE(services.ValidateBoot().has_value());
    EXPECT_FALSE(services.LastBootError().empty());

    services.Reset();
    EXPECT_FALSE(services.HasBootErrors());
    EXPECT_EQ(services.Find<SharedProbeService>(), nullptr);
    EXPECT_TRUE(services.Provide(service, "provider").has_value());
    services.BeginResolution();
    auto required = services.Require<SharedProbeService>("consumer");
    ASSERT_TRUE(required.has_value());
    EXPECT_EQ(&required->get(), &service);
    EXPECT_FALSE(services.Provide(other, "late").has_value());

    services.Reset();
    services.BeginResolution();
    auto missing = services.Require<MissingProbeService>("consumer");
    EXPECT_FALSE(missing.has_value());
    EXPECT_TRUE(services.HasBootErrors());
    EXPECT_FALSE(services.ValidateBoot().has_value());

    services.Reset();
    EXPECT_TRUE(services.Provide(service, "provider").has_value());
    services.Lock();
    EXPECT_TRUE(services.Withdraw(service).has_value());
    EXPECT_EQ(services.Find<SharedProbeService>(), nullptr);
}

TEST(EngineSetup, RetainsOnlyRegistrationPhaseFrameAndViewportRegistrars)
{
    Runtime::CommandBus commands;
    Runtime::KernelEventBus events;
    Runtime::JobService jobs;
    Runtime::WorldRegistry worlds;
    Runtime::ServiceRegistry services;
    (void)worlds.CreateWorld("setup");

    Runtime::EngineSetup missing(
        commands, events, jobs, worlds, services, {});
    EXPECT_EQ(missing.InitializedState(), nullptr);
    EXPECT_EQ(missing.RegisterFrameHook(Runtime::FramePhase::UiBuild, {}).error(),
              Core::ErrorCode::InvalidArgument);
    EXPECT_EQ(missing.RegisterFrameHook(
                  Runtime::FramePhase::UiBuild,
                  [](Runtime::RuntimeFrameHookContext&) {})
                  .error(),
              Core::ErrorCode::InvalidState);
    EXPECT_EQ(missing.RegisterViewportInputHook({}).error(),
              Core::ErrorCode::InvalidArgument);
    EXPECT_EQ(missing.RegisterViewportInputHook(
                  [](Runtime::RuntimeViewportInputHookContext&) {})
                  .error(),
              Core::ErrorCode::InvalidState);

    std::uint32_t frameRegistrations    = 0u;
    std::uint32_t viewportRegistrations = 0u;
    bool initialized                    = false;
    Runtime::EngineSetup available(
        commands,
        events,
        jobs,
        worlds,
        services,
        [&frameRegistrations](Runtime::FramePhase, Runtime::RuntimeFrameHook hook)
        {
            if (hook)
                ++frameRegistrations;
        },
        {},
        [&viewportRegistrations](Runtime::RuntimeViewportInputHook hook)
        {
            if (hook)
                ++viewportRegistrations;
        },
        &initialized);
    EXPECT_EQ(available.InitializedState(), &initialized);
    EXPECT_TRUE(available.RegisterFrameHook(
                    Runtime::FramePhase::UiBuild,
                    [](Runtime::RuntimeFrameHookContext&) {})
                    .has_value());
    EXPECT_TRUE(available.RegisterViewportInputHook(
                    [](Runtime::RuntimeViewportInputHookContext&) {})
                    .has_value());
    EXPECT_EQ(frameRegistrations, 1u);
    EXPECT_EQ(viewportRegistrations, 1u);
}

TEST(RuntimeModuleSchedule, SortsRetainedFrameHooksAndClearRemovesThem)
{
    Runtime::RuntimeModuleSchedule schedule;
    std::vector<std::string> trace;
    schedule.RegisterFrameHook(
        "Z.Module", Runtime::FramePhase::UiBuild,
        [&trace](Runtime::RuntimeFrameHookContext&) { trace.emplace_back("Z"); });
    schedule.RegisterFrameHook(
        "A.Module", Runtime::FramePhase::UiBuild,
        [&trace](Runtime::RuntimeFrameHookContext&) { trace.emplace_back("A:first"); });
    schedule.RegisterFrameHook(
        "A.Module", Runtime::FramePhase::UiBuild,
        [&trace](Runtime::RuntimeFrameHookContext&) { trace.emplace_back("A:second"); });
    schedule.RegisterFrameHook(
        "A.Module", Runtime::FramePhase::Maintenance,
        [&trace](Runtime::RuntimeFrameHookContext&) { trace.emplace_back("maintenance"); });
    schedule.FinalizeForBoot();

    Extrinsic::ECS::Scene::Registry activeWorld;
    Runtime::CommandBus commands;
    Runtime::KernelEventBus events;
    Runtime::JobService jobs;
    Runtime::WorldRegistry worlds;
    Runtime::ServiceRegistry services;
    Runtime::EditorInputCaptureSnapshot capture{};
    Runtime::RuntimeFramePacingDiagnostics pacing{};
    const auto dispatch = [&](const Runtime::FramePhase phase)
    {
        schedule.RunFrameHooks(Runtime::RuntimeModuleFrameHookDispatchContext{
            .Phase             = phase,
            .ActiveWorld       = activeWorld,
            .ActiveWorldHandle = Runtime::DefaultWorldHandle,
            .Commands          = commands,
            .Events            = events,
            .Jobs              = jobs,
            .Worlds            = worlds,
            .Services          = services,
            .EditorCapture     = capture,
            .Pacing            = pacing,
        });
    };

    dispatch(Runtime::FramePhase::UiBuild);
    dispatch(Runtime::FramePhase::Maintenance);
    EXPECT_EQ(trace,
              (std::vector<std::string>{"A:first", "A:second", "Z", "maintenance"}));

    schedule.Clear();
    dispatch(Runtime::FramePhase::UiBuild);
    EXPECT_EQ(trace.size(), 4u);
}

TEST(RuntimeModuleSchedule, SortsRetainedViewportHooksByModuleAndSequence)
{
    Runtime::RuntimeModuleSchedule schedule;
    std::vector<std::string> trace;
    schedule.RegisterViewportInputHook(
        "Z.Module", [&trace](Runtime::RuntimeViewportInputHookContext&)
        { trace.emplace_back("Z"); });
    schedule.RegisterViewportInputHook(
        "A.Module", [&trace](Runtime::RuntimeViewportInputHookContext&)
        { trace.emplace_back("A:first"); });
    schedule.RegisterViewportInputHook(
        "A.Module", [&trace](Runtime::RuntimeViewportInputHookContext&)
        { trace.emplace_back("A:second"); });
    schedule.FinalizeForBoot();

    CoreConfig::EngineConfig config = NullWindowHeadlessConfig();
    Extrinsic::Platform::Input::Context input;
    Runtime::EditorInputCaptureSnapshot capture{};
    Extrinsic::Graphics::RenderFrameInput renderInput{};
    schedule.RunViewportInputHooks(Runtime::RuntimeViewportInputHookContext{
        .Config             = config,
        .ActiveWorldHandle  = Runtime::DefaultWorldHandle,
        .Input              = input,
        .Viewport           = Core::Extent2D{640, 480},
        .EditorCapture      = capture,
        .RenderInput        = renderInput,
        .FrameDeltaSeconds  = 1.0 / 60.0,
    });
    EXPECT_EQ(trace, (std::vector<std::string>{"A:first", "A:second", "Z"}));
}
