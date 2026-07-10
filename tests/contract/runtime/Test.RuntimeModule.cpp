#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.Core.FrameGraph;
import Extrinsic.Core.Hash;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

namespace Core = Extrinsic::Core;
namespace CoreConfig = Extrinsic::Core::Config;
namespace Runtime = Extrinsic::Runtime;

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
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend = CoreConfig::WindowBackend::Null;
        return config;
    }

    [[nodiscard]] std::string PhaseName(const Runtime::FramePhase phase)
    {
        switch (phase)
        {
        case Runtime::FramePhase::AfterCommandDrain: return "AfterCommandDrain";
        case Runtime::FramePhase::UiBuild: return "UiBuild";
        case Runtime::FramePhase::BeforeExtraction: return "BeforeExtraction";
        case Runtime::FramePhase::Maintenance: return "Maintenance";
        }
        return "Unknown";
    }

    struct ModuleHarnessState
    {
        bool ProviderRegistered{false};
        bool ConsumerResolved{false};
        bool CommandContextHadEvents{false};
        bool CommandContextHadJobs{false};
        bool CommandContextHadWorlds{false};
        bool CommandContextWorldWasActive{false};
        bool ShutdownAnnounced{false};
        bool ProviderShutdownSawAnnounce{false};
        bool ConsumerShutdownSawAnnounce{false};
        bool DelayedForFixedStep{false};
        bool TimedOut{false};
        int CommandEventHits{0};
        int CommandEventValue{0};
        std::uint32_t VariableTicks{0};
        std::uint32_t ProducerRuns{0};
        std::uint32_t ConsumerRuns{0};
        SharedProbeService* ResolvedService{};
        int ResolvedServiceValue{0};
        std::vector<std::string> FirstHookTrace{};
        std::array<int, 4> FirstHookCounts{};
        std::vector<std::string> SystemTrace{};
        std::vector<std::string> ShutdownTrace{};
    };

    void RecordHook(ModuleHarnessState& state,
                    const std::string_view moduleName,
                    Runtime::RuntimeFrameHookContext& context)
    {
        const auto index = static_cast<std::size_t>(context.Phase);
        if (index < state.FirstHookCounts.size() &&
            state.FirstHookCounts[index] < 2)
        {
            state.FirstHookTrace.push_back(
                PhaseName(context.Phase) + ":" + std::string(moduleName));
        }
        if (index < state.FirstHookCounts.size())
            state.FirstHookCounts[index] += 1;
    }

    class ProviderModule final : public Runtime::IRuntimeModule
    {
    public:
        ProviderModule(SharedProbeService& service,
                       ModuleHarnessState& state)
            : m_Service(service)
            , m_State(state)
        {
        }

        [[nodiscard]] std::string_view Name() const noexcept override
        {
            return "Z.Provider";
        }

        [[nodiscard]] Core::Result OnRegister(
            Runtime::EngineSetup& setup) override
        {
            if (auto provided =
                    setup.Services().Provide<SharedProbeService>(
                        m_Service, Name());
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
                    m_State.CommandContextHadJobs = context.Jobs != nullptr;
                    m_State.CommandContextHadWorlds = context.Worlds != nullptr;
                    m_State.CommandContextWorldWasActive =
                        context.Worlds != nullptr &&
                        context.Worlds->Get(context.Worlds->ActiveWorld()) ==
                            &context.ActiveWorld;
                    if (context.Events)
                        context.Events->Publish(
                            ProbeCommandHandled{command.Value});
                    return Runtime::CommandOutcome::Ok();
                });

            const Core::Hash::StringID producerSignal{
                "RuntimeModuleTest.ProducerDone"};
            if (auto registered = setup.RegisterSimSystem(
                    Runtime::SimSystemDesc{
                        .Name = "Producer",
                        .SignalLabels = {producerSignal},
                        .Setup = [producerSignal](Core::FrameGraphBuilder& b)
                        {
                            b.Signal(producerSignal);
                        },
                        .Execute = [this](Runtime::SimSystemContext&)
                        {
                            m_State.ProducerRuns += 1u;
                            m_State.SystemTrace.push_back(
                                "Z.Provider.Producer");
                        },
                    });
                !registered.has_value())
            {
                return registered;
            }

            for (const Runtime::FramePhase phase :
                 {Runtime::FramePhase::AfterCommandDrain,
                  Runtime::FramePhase::UiBuild,
                  Runtime::FramePhase::BeforeExtraction,
                  Runtime::FramePhase::Maintenance})
            {
                if (auto hook = setup.RegisterFrameHook(
                        phase,
                        [this](Runtime::RuntimeFrameHookContext& context)
                        {
                            if (context.Phase ==
                                    Runtime::FramePhase::AfterCommandDrain &&
                                !m_State.DelayedForFixedStep)
                            {
                                m_State.DelayedForFixedStep = true;
                                std::this_thread::sleep_for(
                                    std::chrono::milliseconds(20));
                            }
                            RecordHook(m_State, Name(), context);
                        });
                    !hook.has_value())
                {
                    return hook;
                }
            }

            return Core::Ok();
        }

        [[nodiscard]] Core::Result OnResolve(
            Runtime::EngineSetup&) override
        {
            return Core::Ok();
        }

        void OnShutdown(
            Runtime::RuntimeModuleShutdownContext&) override
        {
            m_State.ProviderShutdownSawAnnounce = m_State.ShutdownAnnounced;
            m_State.ShutdownTrace.push_back("shutdown:provider");
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

        [[nodiscard]] Core::Result OnRegister(
            Runtime::EngineSetup& setup) override
        {
            m_ProbeSubscription = setup.Subscribe<ProbeCommandHandled>(
                [this](const ProbeCommandHandled& event)
                {
                    m_State.CommandEventHits += 1;
                    m_State.CommandEventValue = event.Value;
                });
            m_ShutdownSubscription =
                setup.Subscribe<Runtime::RuntimeShutdownAnnounced>(
                    [this](const Runtime::RuntimeShutdownAnnounced&)
                    {
                        m_State.ShutdownAnnounced = true;
                        m_State.ShutdownTrace.push_back("event:shutdown");
                    });

            const Core::Hash::StringID producerSignal{
                "RuntimeModuleTest.ProducerDone"};
            if (auto registered = setup.RegisterSimSystem(
                    Runtime::SimSystemDesc{
                        .Name = "Consumer",
                        .WaitForSignals = {producerSignal},
                        .Setup = [producerSignal](Core::FrameGraphBuilder& b)
                        {
                            b.WaitFor(producerSignal);
                        },
                        .Execute = [this](Runtime::SimSystemContext&)
                        {
                            m_State.ConsumerRuns += 1u;
                            m_State.SystemTrace.push_back(
                                "A.Consumer.Consumer");
                        },
                    });
                !registered.has_value())
            {
                return registered;
            }

            for (const Runtime::FramePhase phase :
                 {Runtime::FramePhase::AfterCommandDrain,
                  Runtime::FramePhase::UiBuild,
                  Runtime::FramePhase::BeforeExtraction,
                  Runtime::FramePhase::Maintenance})
            {
                if (auto hook = setup.RegisterFrameHook(
                        phase,
                        [this](Runtime::RuntimeFrameHookContext& context)
                        {
                            RecordHook(m_State, Name(), context);
                        });
                    !hook.has_value())
                {
                    return hook;
                }
            }

            return Core::Ok();
        }

        [[nodiscard]] Core::Result OnResolve(
            Runtime::EngineSetup& setup) override
        {
            auto required =
                setup.Services().Require<SharedProbeService>(Name());
            if (!required.has_value())
                return Core::Err(required.error());

            m_State.ResolvedService = &required->get();
            m_State.ResolvedServiceValue = required->get().Value;
            m_State.ConsumerResolved = true;
            return Core::Ok();
        }

        void OnShutdown(
            Runtime::RuntimeModuleShutdownContext&) override
        {
            m_State.ConsumerShutdownSawAnnounce = m_State.ShutdownAnnounced;
            m_State.ShutdownTrace.push_back("shutdown:consumer");
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

        [[nodiscard]] Core::Result OnRegister(
            Runtime::EngineSetup&) override
        {
            return Core::Ok();
        }

        [[nodiscard]] Core::Result OnResolve(
            Runtime::EngineSetup& setup) override
        {
            (void)setup.Services().Require<MissingProbeService>(Name());
            return Core::Ok();
        }

        void OnShutdown(Runtime::RuntimeModuleShutdownContext&) override {}
    };

    class ModuleHarnessApplication final : public Runtime::IApplication
    {
    public:
        explicit ModuleHarnessApplication(ModuleHarnessState& state)
            : m_State(state)
        {
        }

        void OnInitialize(Runtime::Engine& engine) override
        {
            engine.Commands().Enqueue(ProbeCommand{23});
        }

        void OnSimTick(Runtime::Engine&, double) override {}

        void OnVariableTick(Runtime::Engine& engine,
                            double,
                            double) override
        {
            m_State.VariableTicks += 1u;
            if (m_State.CommandEventHits > 0 &&
                m_State.ConsumerRuns > 0 &&
                m_State.FirstHookCounts[
                    static_cast<std::size_t>(
                        Runtime::FramePhase::Maintenance)] >= 2)
            {
                engine.RequestExit();
                return;
            }

            if (m_State.VariableTicks > 300u)
            {
                m_State.TimedOut = true;
                engine.RequestExit();
            }
        }

        void OnShutdown(Runtime::Engine&) override {}

    private:
        ModuleHarnessState& m_State;
    };

    [[nodiscard]] ModuleHarnessState RunModuleHarness(
        const bool providerFirst)
    {
        ModuleHarnessState state{};
        SharedProbeService service{};
        auto app = std::make_unique<ModuleHarnessApplication>(state);
        Runtime::Engine engine(NullWindowHeadlessConfig(), std::move(app));

        if (providerFirst)
        {
            engine.AddModule(
                std::make_unique<ProviderModule>(service, state));
            engine.AddModule(std::make_unique<ConsumerModule>(state));
        }
        else
        {
            engine.AddModule(std::make_unique<ConsumerModule>(state));
            engine.AddModule(
                std::make_unique<ProviderModule>(service, state));
        }

        engine.Initialize();
        EXPECT_EQ(engine.Services().Phase(), Runtime::ServiceRegistryPhase::Locked);
        EXPECT_NE(engine.Services().Find<Runtime::CommandBus>(), nullptr);
        EXPECT_NE(engine.Services().Find<Runtime::KernelEventBus>(), nullptr);
        EXPECT_NE(engine.Services().Find<Runtime::JobService>(), nullptr);
        EXPECT_NE(engine.Services().Find<Runtime::WorldRegistry>(), nullptr);
        engine.Run();
        engine.Shutdown();
        return state;
    }

    [[nodiscard]] bool ContainsText(const std::string_view haystack,
                                    const std::string_view needle)
    {
        return haystack.find(needle) != std::string_view::npos;
    }
}

TEST(RuntimeServiceRegistry, ProvideRequireFindUseTwoPhaseContract)
{
    Runtime::ServiceRegistry services;
    SharedProbeService service{.Value = 42};

    services.BeginRegistration();
    ASSERT_TRUE(
        services.Provide<SharedProbeService>(service, "Provider").has_value());
    services.BeginResolution();

    auto required =
        services.Require<SharedProbeService>("Consumer");
    ASSERT_TRUE(required.has_value());
    EXPECT_EQ(&required->get(), &service);
    EXPECT_EQ(services.Find<SharedProbeService>(), &service);
    EXPECT_EQ(required->get().Value, 42);
    EXPECT_TRUE(services.ValidateBoot().has_value());
    services.Lock();
    EXPECT_EQ(services.Phase(), Runtime::ServiceRegistryPhase::Locked);
    EXPECT_EQ(services.Stats().ProvidedServices, 1u);
}

TEST(RuntimeServiceRegistry, MissingRequireNamesRequesterAndService)
{
    Runtime::ServiceRegistry services;
    services.BeginRegistration();
    services.BeginResolution();

    auto missing =
        services.Require<MissingProbeService>("MissingConsumer");
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error(), Core::ErrorCode::ResourceNotFound);
    EXPECT_FALSE(services.ValidateBoot().has_value());

    const std::string message{services.LastBootError()};
    EXPECT_TRUE(ContainsText(message, "MissingConsumer")) << message;
    EXPECT_TRUE(ContainsText(message, "MissingProbeService")) << message;
    EXPECT_EQ(services.Stats().MissingRequirements, 1u);
    EXPECT_EQ(services.Stats().BootErrors, 1u);
}

TEST(RuntimeModule, MissingRequiredServiceTerminatesEngineBoot)
{
    ModuleHarnessState state{};
    auto app = std::make_unique<ModuleHarnessApplication>(state);
    Runtime::Engine engine(NullWindowHeadlessConfig(), std::move(app));
    engine.AddModule(std::make_unique<MissingRequireModule>());

    EXPECT_DEATH(engine.Initialize(), "");
}

TEST(RuntimeModule, EngineComposesTestModulesThroughSetupSurface)
{
    ModuleHarnessState state = RunModuleHarness(true);

    EXPECT_FALSE(state.TimedOut);
    EXPECT_TRUE(state.ProviderRegistered);
    EXPECT_TRUE(state.ConsumerResolved);
    ASSERT_NE(state.ResolvedService, nullptr);
    EXPECT_EQ(state.ResolvedServiceValue, 17);

    EXPECT_TRUE(state.CommandContextHadEvents);
    EXPECT_TRUE(state.CommandContextHadJobs);
    EXPECT_TRUE(state.CommandContextHadWorlds);
    EXPECT_TRUE(state.CommandContextWorldWasActive);
    EXPECT_EQ(state.CommandEventHits, 1);
    EXPECT_EQ(state.CommandEventValue, 23);

    ASSERT_GE(state.ProducerRuns, 1u);
    ASSERT_GE(state.ConsumerRuns, 1u);
    ASSERT_GE(state.SystemTrace.size(), 2u);
    EXPECT_EQ(state.SystemTrace[0], "Z.Provider.Producer");
    EXPECT_EQ(state.SystemTrace[1], "A.Consumer.Consumer");

    EXPECT_TRUE(state.ShutdownAnnounced);
    EXPECT_TRUE(state.ProviderShutdownSawAnnounce);
    EXPECT_TRUE(state.ConsumerShutdownSawAnnounce);
    ASSERT_FALSE(state.ShutdownTrace.empty());
    EXPECT_EQ(state.ShutdownTrace.front(), "event:shutdown");
}

TEST(RuntimeModule, RegistrationOrderDoesNotChangeHooksOrSchedule)
{
    ModuleHarnessState providerFirst = RunModuleHarness(true);
    ModuleHarnessState consumerFirst = RunModuleHarness(false);

    EXPECT_FALSE(providerFirst.TimedOut);
    EXPECT_FALSE(consumerFirst.TimedOut);
    EXPECT_EQ(providerFirst.FirstHookTrace, consumerFirst.FirstHookTrace);
    ASSERT_GE(providerFirst.SystemTrace.size(), 2u);
    ASSERT_GE(consumerFirst.SystemTrace.size(), 2u);
    EXPECT_EQ(providerFirst.SystemTrace[0], consumerFirst.SystemTrace[0]);
    EXPECT_EQ(providerFirst.SystemTrace[1], consumerFirst.SystemTrace[1]);
    EXPECT_EQ(providerFirst.SystemTrace[0], "Z.Provider.Producer");
    EXPECT_EQ(providerFirst.SystemTrace[1], "A.Consumer.Consumer");
}
