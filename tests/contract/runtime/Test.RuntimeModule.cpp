#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.Core.FrameGraph;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Core.Hash;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Platform.Input;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.Engine;
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
namespace Components = Extrinsic::ECS::Components;

static_assert(
    static_cast<std::uint8_t>(
        Runtime::FramePhase::Maintenance) == 5u);
template <typename T>
concept HasViewportMember = requires(T value)
{
    value.Viewport;
};
template <typename T>
concept HasRenderInputMember = requires(T value)
{
    value.RenderInput;
};
static_assert(
    !HasViewportMember<Runtime::RuntimeFrameHookContext>);
static_assert(
    !HasRenderInputMember<Runtime::RuntimeFrameHookContext>);

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
        case Runtime::FramePhase::UiBegin: return "UiBegin";
        case Runtime::FramePhase::UiBuild: return "UiBuild";
        case Runtime::FramePhase::UiEndCapture: return "UiEndCapture";
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
        bool RegisterSawUninitialized{false};
        bool ResolveSawUninitialized{false};
        bool ApplicationInitializeSawUninitialized{false};
        bool InitializedAfterInitialize{false};
        bool AnnouncementSawUninitialized{false};
        bool ApplicationShutdownSawAnnounce{false};
        bool ApplicationShutdownSawUninitialized{false};
        bool UninitializedAfterShutdown{false};
        bool ProviderShutdownSawAnnounce{false};
        bool ConsumerShutdownSawAnnounce{false};
        bool DelayedForFixedStep{false};
        bool DelayedForReplay{false};
        bool TimedOut{false};
        int CommandEventHits{0};
        int CommandEventValue{0};
        std::uint32_t VariableTicks{0};
        std::uint32_t ProducerRuns{0};
        std::uint32_t ConsumerRuns{0};
        std::uint64_t FrameGraphCompileCalls{0u};
        std::uint64_t FrameGraphPlanBuilds{0u};
        std::uint64_t FrameGraphPlanReuses{0u};
        bool FrameGraphLastCompileReusedPlan{false};
        Extrinsic::ECS::EntityHandle BaselineProbeEntity{};
        std::uint32_t SimTickMutations{0};
        std::uint32_t BaselineReaderRuns{0};
        float CurrentSubstepExpectedX{0.0f};
        float LastObservedWorldX{0.0f};
        bool BaselineReaderMissingWorld{false};
        bool BaselineReaderObservedStale{false};
        SharedProbeService* ResolvedService{};
        int ResolvedServiceValue{0};
        std::vector<std::string> FirstHookTrace{};
        std::array<int, 6> FirstHookCounts{};
        std::vector<std::string> SystemTrace{};
        std::vector<std::string> ShutdownTrace{};
        const bool* InitializedState{};
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
                        // BUG-072: order via the declarative SignalLabels field
                        // alone — RegisterSimSystemsForTick now derives the
                        // per-tick Signal edge from it, so no manual b.Signal is
                        // needed in Setup.
                        .SignalLabels = {producerSignal},
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
                  Runtime::FramePhase::UiBegin,
                  Runtime::FramePhase::UiBuild,
                  Runtime::FramePhase::UiEndCapture,
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
            m_State.InitializedState = setup.InitializedState();
            m_State.RegisterSawUninitialized =
                m_State.InitializedState != nullptr &&
                !*m_State.InitializedState;
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
                        m_State.AnnouncementSawUninitialized =
                            m_State.InitializedState != nullptr &&
                            !*m_State.InitializedState;
                        m_State.ShutdownTrace.push_back("event:shutdown");
                    });

            const Core::Hash::StringID producerSignal{
                "RuntimeModuleTest.ProducerDone"};
            if (auto registered = setup.RegisterSimSystem(
                    Runtime::SimSystemDesc{
                        .Name = "Consumer",
                        // BUG-072: wait via the declarative WaitForSignals field
                        // alone; the per-tick WaitFor edge is now derived from it.
                        .WaitForSignals = {producerSignal},
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

            const Core::Hash::StringID transformUpdate{"TransformUpdate"};
            if (auto registered = setup.RegisterSimSystem(
                    Runtime::SimSystemDesc{
                        .Name = "BaselineWorldMatrixConsumer",
                        .WaitForSignals = {transformUpdate},
                        .Setup = [](Core::FrameGraphBuilder& builder)
                        {
                            builder.Read<
                                Components::Transform::WorldMatrix>();
                        },
                        .Execute = [this](Runtime::SimSystemContext& context)
                        {
                            m_State.BaselineReaderRuns += 1u;
                            auto& raw = context.ActiveWorld.Raw();
                            if (!raw.all_of<
                                    Components::Transform::WorldMatrix>(
                                    m_State.BaselineProbeEntity))
                            {
                                m_State.BaselineReaderMissingWorld = true;
                                return;
                            }

                            m_State.LastObservedWorldX =
                                raw.get<Components::Transform::WorldMatrix>(
                                       m_State.BaselineProbeEntity)
                                    .Matrix[3][0];
                            if (m_State.LastObservedWorldX !=
                                m_State.CurrentSubstepExpectedX)
                            {
                                m_State.BaselineReaderObservedStale = true;
                            }
                        },
                    });
                !registered.has_value())
            {
                return registered;
            }

            for (const Runtime::FramePhase phase :
                 {Runtime::FramePhase::AfterCommandDrain,
                  Runtime::FramePhase::UiBegin,
                  Runtime::FramePhase::UiBuild,
                  Runtime::FramePhase::UiEndCapture,
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
            m_State.ResolveSawUninitialized =
                setup.InitializedState() == m_State.InitializedState &&
                m_State.InitializedState != nullptr &&
                !*m_State.InitializedState;
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

    class DuplicateSimSystemModule final : public Runtime::IRuntimeModule
    {
    public:
        [[nodiscard]] std::string_view Name() const noexcept override
        {
            return "DuplicateSimSystem";
        }

        [[nodiscard]] Core::Result OnRegister(
            Runtime::EngineSetup& setup) override
        {
            if (Core::Result first = setup.RegisterSimSystem(
                    Runtime::SimSystemDesc{
                        .Name = "Duplicate",
                        .Execute = [](Runtime::SimSystemContext&) {},
                    });
                !first.has_value())
            {
                return first;
            }
            return setup.RegisterSimSystem(
                Runtime::SimSystemDesc{
                    .Name = "Duplicate",
                    .Execute = [](Runtime::SimSystemContext&) {},
                });
        }

        [[nodiscard]] Core::Result OnResolve(
            Runtime::EngineSetup&) override
        {
            return Core::Ok();
        }

        void OnShutdown(Runtime::RuntimeModuleShutdownContext&) override {}
    };

    class CrossPhaseDuplicateSimSystemModule final
        : public Runtime::IRuntimeModule
    {
    public:
        [[nodiscard]] std::string_view Name() const noexcept override
        {
            return "CrossPhaseDuplicateSimSystem";
        }

        [[nodiscard]] Core::Result OnRegister(
            Runtime::EngineSetup& setup) override
        {
            return RegisterDuplicate(setup);
        }

        [[nodiscard]] Core::Result OnResolve(
            Runtime::EngineSetup& setup) override
        {
            return RegisterDuplicate(setup);
        }

        void OnShutdown(Runtime::RuntimeModuleShutdownContext&) override {}

    private:
        [[nodiscard]] static Core::Result RegisterDuplicate(
            Runtime::EngineSetup& setup)
        {
            return setup.RegisterSimSystem(
                Runtime::SimSystemDesc{
                    .Name = "DuplicateAcrossBootPhases",
                    .Execute = [](Runtime::SimSystemContext&) {},
                });
        }
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
            m_State.ApplicationInitializeSawUninitialized =
                m_State.InitializedState != nullptr &&
                !*m_State.InitializedState;
            m_State.BaselineProbeEntity =
                Extrinsic::ECS::Scene::CreateDefault(
                    *engine.Worlds().Get(engine.ActiveWorld()), "BaselineTransformProbe");
            engine.Commands().Enqueue(ProbeCommand{23});
        }

        void OnSimTick(Runtime::Engine& engine, double) override
        {
            m_State.SimTickMutations += 1u;
            m_State.CurrentSubstepExpectedX =
                10.0f + static_cast<float>(m_State.SimTickMutations);

            auto& raw = engine.Worlds().Get(engine.ActiveWorld())->Raw();
            raw.get<Components::Transform::Component>(
                   m_State.BaselineProbeEntity)
                .Position = glm::vec3(
                    m_State.CurrentSubstepExpectedX, 0.0f, 0.0f);
            raw.emplace_or_replace<Components::Transform::IsDirtyTag>(
                m_State.BaselineProbeEntity);
        }

        void OnVariableTick(Runtime::Engine& engine,
                            double,
                            double) override
        {
            m_State.VariableTicks += 1u;
            const auto planStats =
                engine.GetFrameGraph().GetPlanReuseStats();
            m_State.FrameGraphCompileCalls =
                planStats.CompileCallCount;
            m_State.FrameGraphPlanBuilds =
                planStats.PlanBuildCount;
            m_State.FrameGraphPlanReuses =
                planStats.PlanReuseCount;
            m_State.FrameGraphLastCompileReusedPlan =
                planStats.LastCompileReusedPlan;
            if (m_State.CommandEventHits > 0 &&
                m_State.ConsumerRuns >= 2u &&
                m_State.FrameGraphPlanReuses >= 1u &&
                m_State.FirstHookCounts[
                    static_cast<std::size_t>(
                        Runtime::FramePhase::Maintenance)] >= 2)
            {
                engine.RequestExit();
                return;
            }

            if (m_State.ConsumerRuns == 1u &&
                m_State.FrameGraphPlanReuses == 0u &&
                !m_State.DelayedForReplay)
            {
                m_State.DelayedForReplay = true;
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(20));
            }

            if (m_State.VariableTicks > 300u)
            {
                m_State.TimedOut = true;
                engine.RequestExit();
            }
        }

        void OnShutdown(Runtime::Engine&) override
        {
            m_State.ApplicationShutdownSawAnnounce =
                m_State.ShutdownAnnounced;
            m_State.ApplicationShutdownSawUninitialized =
                m_State.InitializedState != nullptr &&
                !*m_State.InitializedState;
        }

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
        state.InitializedAfterInitialize =
            state.InitializedState != nullptr &&
            *state.InitializedState;
        EXPECT_EQ(engine.Services().Phase(), Runtime::ServiceRegistryPhase::Locked);
        EXPECT_NE(engine.Services().Find<Runtime::CommandBus>(), nullptr);
        EXPECT_NE(engine.Services().Find<Runtime::KernelEventBus>(), nullptr);
        EXPECT_NE(engine.Services().Find<Runtime::JobService>(), nullptr);
        EXPECT_NE(engine.Services().Find<Runtime::WorldRegistry>(), nullptr);
        EXPECT_EQ(engine.Services().Find<RHI::IDevice>(),
                  &engine.GetDevice());
        Runtime::RenderExtractionCache* renderExtraction =
            engine.Services().Find<Runtime::RenderExtractionCache>();
        EXPECT_NE(renderExtraction, nullptr);
        if (renderExtraction != nullptr)
        {
            constexpr std::uint32_t stableId = 0xA11CEu;
            const std::uint64_t revisionBefore =
                engine.GetVisualizationAdapterBindingRevision();
            renderExtraction->SetVisualizationAdapterBinding(
                stableId,
                Runtime::RenderExtractionCache::VisualizationAdapterBinding{
                    .AdapterKey = 0xCAFEu,
                    .BufferBDA = 0xBEEFu,
                });
            EXPECT_EQ(engine.GetVisualizationAdapterBindingRevision(),
                      revisionBefore + 1u);
            EXPECT_TRUE(
                engine.GetVisualizationAdapterBinding(stableId).has_value());
            engine.ClearVisualizationAdapterBinding(stableId);
        }
        engine.Run();
        engine.Shutdown();
        EXPECT_EQ(engine.Services().Find<RHI::IDevice>(), nullptr);
        state.UninitializedAfterShutdown =
            state.InitializedState != nullptr &&
            !*state.InitializedState;
        state.InitializedState = nullptr;
        return state;
    }

    [[nodiscard]] bool ContainsText(const std::string_view haystack,
                                    const std::string_view needle)
    {
        return haystack.find(needle) != std::string_view::npos;
    }

    class PassiveApplication final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(Runtime::Engine&, double, double) override {}
        void OnShutdown(Runtime::Engine&) override {}
    };

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

TEST(RuntimeServiceRegistry, WithdrawRequiresExactInstanceAndWorksWhenLocked)
{
    Runtime::ServiceRegistry services;
    SharedProbeService provided{.Value = 42};
    SharedProbeService different{.Value = 17};

    services.BeginRegistration();
    ASSERT_TRUE(
        services.Provide<SharedProbeService>(provided, "Provider").has_value());
    services.BeginResolution();
    services.Lock();

    const Core::Result wrongInstance =
        services.Withdraw<SharedProbeService>(different);
    ASSERT_FALSE(wrongInstance.has_value());
    EXPECT_EQ(wrongInstance.error(), Core::ErrorCode::InvalidArgument);
    EXPECT_EQ(services.Find<SharedProbeService>(), &provided);
    EXPECT_EQ(services.Stats().ProvidedServices, 1u);

    EXPECT_TRUE(
        services.Withdraw<SharedProbeService>(provided).has_value());
    EXPECT_EQ(services.Find<SharedProbeService>(), nullptr);
    EXPECT_EQ(services.Stats().ProvidedServices, 0u);

    const Core::Result alreadyWithdrawn =
        services.Withdraw<SharedProbeService>(provided);
    ASSERT_FALSE(alreadyWithdrawn.has_value());
    EXPECT_EQ(alreadyWithdrawn.error(), Core::ErrorCode::ResourceNotFound);
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

TEST(RuntimeModule, DuplicateSimSystemIdentityTerminatesEngineBootBeforeRun)
{
    ModuleHarnessState state{};
    auto app = std::make_unique<ModuleHarnessApplication>(state);
    Runtime::Engine engine(NullWindowHeadlessConfig(), std::move(app));
    engine.AddModule(std::make_unique<DuplicateSimSystemModule>());

    // FinalizeForBoot exposes InvalidArgument to direct callers. Engine boot
    // intentionally translates any invalid finalized schedule into its global
    // fail-closed initialization policy, so no fixed-step pass can execute.
    EXPECT_DEATH(engine.Initialize(), "");
}

TEST(RuntimeModule, ResolveRegisteredDuplicateJoinsFullBootValidationSet)
{
    ModuleHarnessState state{};
    auto app = std::make_unique<ModuleHarnessApplication>(state);
    Runtime::Engine engine(NullWindowHeadlessConfig(), std::move(app));
    engine.AddModule(
        std::make_unique<CrossPhaseDuplicateSimSystemModule>());

    // BUG-071: the first identity is valid after OnRegister in isolation. The
    // second arrives through the still-live OnResolve registrar and must be
    // checked against the complete contribution set before boot can return.
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
    ASSERT_GE(state.ConsumerRuns, 2u);
    EXPECT_EQ(state.FrameGraphCompileCalls,
              state.ConsumerRuns);
    EXPECT_EQ(state.FrameGraphPlanBuilds, 1u);
    EXPECT_EQ(state.FrameGraphPlanReuses,
              state.ConsumerRuns - 1u);
    EXPECT_TRUE(state.FrameGraphLastCompileReusedPlan);
    ASSERT_GE(state.SystemTrace.size(), 2u);
    EXPECT_EQ(state.SystemTrace[0], "Z.Provider.Producer");
    EXPECT_EQ(state.SystemTrace[1], "A.Consumer.Consumer");

    EXPECT_TRUE(state.ShutdownAnnounced);
    EXPECT_TRUE(state.RegisterSawUninitialized);
    EXPECT_TRUE(state.ResolveSawUninitialized);
    EXPECT_TRUE(state.ApplicationInitializeSawUninitialized);
    EXPECT_TRUE(state.InitializedAfterInitialize);
    EXPECT_TRUE(state.AnnouncementSawUninitialized);
    EXPECT_TRUE(state.ApplicationShutdownSawAnnounce);
    EXPECT_TRUE(state.ApplicationShutdownSawUninitialized);
    EXPECT_TRUE(state.UninitializedAfterShutdown);
    EXPECT_TRUE(state.ProviderShutdownSawAnnounce);
    EXPECT_TRUE(state.ConsumerShutdownSawAnnounce);
    ASSERT_FALSE(state.ShutdownTrace.empty());
    EXPECT_EQ(state.ShutdownTrace.front(), "event:shutdown");
}

TEST(RuntimeModule,
     EnginePublishesExactDeviceAndWithdrawsAcrossReinitialize)
{
    Runtime::Engine engine(
        NullWindowHeadlessConfig(),
        std::make_unique<PassiveApplication>());

    engine.Initialize();
    EXPECT_EQ(engine.Services().Find<RHI::IDevice>(),
              &engine.GetDevice());
    engine.Shutdown();
    EXPECT_EQ(engine.Services().Find<RHI::IDevice>(), nullptr);

    engine.Initialize();
    EXPECT_EQ(engine.Services().Find<RHI::IDevice>(),
              &engine.GetDevice());
    engine.Shutdown();
    EXPECT_EQ(engine.Services().Find<RHI::IDevice>(), nullptr);
}

TEST(RuntimeModule, BaselineTransformConsumerObservesCurrentSubstepWorldMatrix)
{
    const ModuleHarnessState state = RunModuleHarness(true);

    EXPECT_FALSE(state.TimedOut);
    ASSERT_GE(state.BaselineReaderRuns, 1u);
    EXPECT_EQ(state.BaselineReaderRuns, state.SimTickMutations);
    EXPECT_FALSE(state.BaselineReaderMissingWorld);
    EXPECT_FALSE(state.BaselineReaderObservedStale);
    EXPECT_FLOAT_EQ(state.LastObservedWorldX,
                    state.CurrentSubstepExpectedX);
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

// BUG-070: restore the fail-closed guards the recovery merge dropped from the
// module schedule (equivalent to the deleted DuplicateSystemPassNamesFailClosed
// and CyclicSystemSignalsFailClosed coverage), now exercised directly through
// the recoverable FinalizeForBoot() result.
TEST(RuntimeModuleSchedule, DuplicateSimSystemIdentityFailsClosed)
{
    Runtime::RuntimeModuleSchedule schedule;
    schedule.RegisterSimSystem("Module", Runtime::SimSystemDesc{.Name = "dup"});
    schedule.RegisterSimSystem("Module", Runtime::SimSystemDesc{.Name = "dup"});

    const Core::Result result = schedule.FinalizeForBoot({});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Core::ErrorCode::InvalidArgument);
}

TEST(RuntimeModuleSchedule, DistinctIdentitiesUnderOneModuleFinalizeOk)
{
    Runtime::RuntimeModuleSchedule schedule;
    schedule.RegisterSimSystem("Module", Runtime::SimSystemDesc{.Name = "a"});
    schedule.RegisterSimSystem("Module", Runtime::SimSystemDesc{.Name = "b"});

    EXPECT_TRUE(schedule.FinalizeForBoot({}).has_value());
}

TEST(RuntimeModuleSchedule, CyclicSimSystemSignalsFailClosed)
{
    const Core::Hash::StringID signalA{"RuntimeModuleSchedule.CycleA"};
    const Core::Hash::StringID signalB{"RuntimeModuleSchedule.CycleB"};

    Runtime::RuntimeModuleSchedule schedule;
    schedule.RegisterSimSystem(
        "Module",
        Runtime::SimSystemDesc{
            .Name = "a", .WaitForSignals = {signalB}, .SignalLabels = {signalA}});
    schedule.RegisterSimSystem(
        "Module",
        Runtime::SimSystemDesc{
            .Name = "b", .WaitForSignals = {signalA}, .SignalLabels = {signalB}});

    const Core::Result result = schedule.FinalizeForBoot({});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Core::ErrorCode::InvalidState);
}

TEST(RuntimeModuleSchedule, UnprovidedWaitSignalFailsClosed)
{
    const Core::Hash::StringID missing{"RuntimeModuleSchedule.Missing"};

    Runtime::RuntimeModuleSchedule schedule;
    schedule.RegisterSimSystem(
        "Module",
        Runtime::SimSystemDesc{.Name = "waiter", .WaitForSignals = {missing}});

    const Core::Result result = schedule.FinalizeForBoot({});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Core::ErrorCode::InvalidState);
}

TEST(RuntimeModuleSchedule, SignalOrderedScheduleFinalizesOk)
{
    const Core::Hash::StringID ready{"RuntimeModuleSchedule.Ready"};

    Runtime::RuntimeModuleSchedule schedule;
    schedule.RegisterSimSystem(
        "Module",
        Runtime::SimSystemDesc{.Name = "producer", .SignalLabels = {ready}});
    schedule.RegisterSimSystem(
        "Module",
        Runtime::SimSystemDesc{.Name = "consumer", .WaitForSignals = {ready}});

    EXPECT_TRUE(schedule.FinalizeForBoot({}).has_value());
}

TEST(RuntimeModuleSchedule, DeclarativeSignalsCreatePerTickEdgeForParallelSystems)
{
    const Core::Hash::StringID ready{
        "RuntimeModuleSchedule.ParallelReady"};
    const Core::FrameGraphPassOptions parallelOptions{
        .MainThreadOnly = false,
        .AllowParallel = true,
        .DebugCategory = "RuntimeModuleTest",
    };

    bool producerRan = false;
    std::vector<bool> consumerObservedProducer{};
    std::vector<std::uint64_t> observedFrameIndices{};
    std::vector<Extrinsic::ECS::Scene::Registry*> observedWorlds{};
    Runtime::RuntimeModuleSchedule schedule;

    // Register the consumer first so neither registration nor module-name order
    // can accidentally satisfy the dependency. The declarative label must
    // create the per-tick FrameGraph edge even though both passes may run on
    // workers in parallel.
    schedule.RegisterSimSystem(
        "A.Module",
        Runtime::SimSystemDesc{
            .Name = "Consume",
            .Options = parallelOptions,
            .WaitForSignals = {ready},
            .Execute = [&](Runtime::SimSystemContext& context)
            {
                consumerObservedProducer.push_back(producerRan);
                observedFrameIndices.push_back(context.FrameIndex);
                observedWorlds.push_back(&context.ActiveWorld);
            },
        });
    schedule.RegisterSimSystem(
        "Z.Module",
        Runtime::SimSystemDesc{
            .Name = "Produce",
            .Options = parallelOptions,
            .SignalLabels = {ready},
            .Execute = [&](Runtime::SimSystemContext&) { producerRan = true; },
        });
    ASSERT_TRUE(schedule.FinalizeForBoot({}).has_value());

    Core::FrameGraph graph;
    Extrinsic::ECS::Scene::Registry activeWorld;
    Runtime::CommandBus commands;
    Runtime::KernelEventBus events;
    Runtime::JobService jobs;
    Runtime::WorldRegistry worlds;
    Runtime::ServiceRegistry services;
    schedule.RegisterSimSystemsForTick(
        Runtime::RuntimeModuleSimSystemScheduleContext{
            .Graph = graph,
            .ActiveWorld = activeWorld,
            .ActiveWorldHandle = Runtime::DefaultWorldHandle,
            .Commands = commands,
            .Events = events,
            .Jobs = jobs,
            .Worlds = worlds,
            .Services = services,
            .FrameIndex = 7,
            .FixedDeltaSeconds = 1.0 / 60.0,
        });

    ASSERT_TRUE(graph.Compile().has_value());
    ASSERT_EQ(graph.PassCount(), 2u);
    const auto& layers = graph.GetExecutionLayers();
    ASSERT_EQ(layers.size(), 2u);
    ASSERT_EQ(layers[0].size(), 1u);
    ASSERT_EQ(layers[1].size(), 1u);
    EXPECT_EQ(graph.PassName(layers[0][0]), "Z.Module.Produce");
    EXPECT_EQ(graph.PassName(layers[1][0]), "A.Module.Consume");

    ASSERT_TRUE(graph.Execute().has_value());
    ASSERT_EQ(consumerObservedProducer.size(), 1u);
    EXPECT_TRUE(consumerObservedProducer[0]);
    ASSERT_EQ(observedFrameIndices.size(), 1u);
    EXPECT_EQ(observedFrameIndices[0], 7u);
    ASSERT_EQ(observedWorlds.size(), 1u);
    EXPECT_EQ(observedWorlds[0], &activeWorld);

    producerRan = false;
    Extrinsic::ECS::Scene::Registry replayWorld;
    ASSERT_TRUE(graph.ResetForReplay().has_value());
    schedule.RegisterSimSystemsForTick(
        Runtime::RuntimeModuleSimSystemScheduleContext{
            .Graph = graph,
            .ActiveWorld = replayWorld,
            .ActiveWorldHandle = Runtime::DefaultWorldHandle,
            .Commands = commands,
            .Events = events,
            .Jobs = jobs,
            .Worlds = worlds,
            .Services = services,
            .FrameIndex = 8,
            .FixedDeltaSeconds = 1.0 / 60.0,
        });

    ASSERT_TRUE(graph.Compile().has_value());
    ASSERT_TRUE(graph.Execute().has_value());

    const auto planStats = graph.GetPlanReuseStats();
    EXPECT_EQ(planStats.CompileCallCount, 2u);
    EXPECT_EQ(planStats.PlanBuildCount, 1u);
    EXPECT_EQ(planStats.PlanReuseCount, 1u);
    EXPECT_TRUE(planStats.LastCompileReusedPlan);
    ASSERT_EQ(consumerObservedProducer.size(), 2u);
    EXPECT_TRUE(consumerObservedProducer[1]);
    ASSERT_EQ(observedFrameIndices.size(), 2u);
    EXPECT_EQ(observedFrameIndices[1], 8u);
    ASSERT_EQ(observedWorlds.size(), 2u);
    EXPECT_EQ(observedWorlds[1], &replayWorld);

    producerRan = false;
    bool appPassRan = false;
    Extrinsic::ECS::Scene::Registry changedShapeWorld;
    ASSERT_TRUE(graph.ResetForReplay().has_value());
    graph.AddPass(
        "App.ShapeTransition",
        [](Core::FrameGraphBuilder&) {},
        [&appPassRan]()
        {
            appPassRan = true;
        });
    schedule.RegisterSimSystemsForTick(
        Runtime::RuntimeModuleSimSystemScheduleContext{
            .Graph = graph,
            .ActiveWorld = changedShapeWorld,
            .ActiveWorldHandle = Runtime::DefaultWorldHandle,
            .Commands = commands,
            .Events = events,
            .Jobs = jobs,
            .Worlds = worlds,
            .Services = services,
            .FrameIndex = 9,
            .FixedDeltaSeconds = 1.0 / 60.0,
        });

    ASSERT_TRUE(graph.Compile().has_value());
    ASSERT_TRUE(graph.Execute().has_value());

    const auto changedShapeStats = graph.GetPlanReuseStats();
    EXPECT_EQ(changedShapeStats.CompileCallCount, 3u);
    EXPECT_EQ(changedShapeStats.PlanBuildCount, 2u);
    EXPECT_EQ(changedShapeStats.PlanReuseCount, 1u);
    EXPECT_FALSE(changedShapeStats.LastCompileReusedPlan);
    EXPECT_TRUE(appPassRan);
    ASSERT_EQ(consumerObservedProducer.size(), 3u);
    EXPECT_TRUE(consumerObservedProducer[2]);
    ASSERT_EQ(observedFrameIndices.size(), 3u);
    EXPECT_EQ(observedFrameIndices[2], 9u);
    ASSERT_EQ(observedWorlds.size(), 3u);
    EXPECT_EQ(observedWorlds[2], &changedShapeWorld);
}

// BUG-105: every module sim-system receives ActiveWorld in its execution
// context, so the runtime schedule must declare a structural read even when
// the module's optional setup callback only names component hazards (or is
// absent). This makes an earlier registry-structure writer a deterministic
// predecessor instead of allowing EnTT storage-map mutation to overlap the
// module callback.
TEST(RuntimeModuleSchedule, ActiveWorldAccessDeclaresStructuralRead)
{
    bool structuralWriterRan = false;
    bool moduleReaderObservedWriter = false;

    Runtime::RuntimeModuleSchedule schedule;
    schedule.RegisterSimSystem(
        "Module",
        Runtime::SimSystemDesc{
            .Name = "ActiveWorldReader",
            .Execute = [&](Runtime::SimSystemContext&)
            {
                moduleReaderObservedWriter = structuralWriterRan;
            },
        });
    ASSERT_TRUE(schedule.FinalizeForBoot({}).has_value());

    Core::FrameGraph graph;
    graph.AddPass(
        "Baseline.StructuralWriter",
        [](Core::FrameGraphBuilder& builder)
        {
            builder.StructuralWrite();
        },
        [&]()
        {
            structuralWriterRan = true;
        });

    Extrinsic::ECS::Scene::Registry activeWorld;
    Runtime::CommandBus commands;
    Runtime::KernelEventBus events;
    Runtime::JobService jobs;
    Runtime::WorldRegistry worlds;
    Runtime::ServiceRegistry services;
    schedule.RegisterSimSystemsForTick(
        Runtime::RuntimeModuleSimSystemScheduleContext{
            .Graph = graph,
            .ActiveWorld = activeWorld,
            .ActiveWorldHandle = Runtime::DefaultWorldHandle,
            .Commands = commands,
            .Events = events,
            .Jobs = jobs,
            .Worlds = worlds,
            .Services = services,
            .FrameIndex = 7,
            .FixedDeltaSeconds = 1.0 / 60.0,
        });

    ASSERT_TRUE(graph.Compile().has_value());
    ASSERT_EQ(graph.PassCount(), 2u);
    const auto& layers = graph.GetExecutionLayers();
    ASSERT_EQ(layers.size(), 2u);
    ASSERT_EQ(layers[0].size(), 1u);
    ASSERT_EQ(layers[1].size(), 1u);
    EXPECT_EQ(graph.PassName(layers[0][0]), "Baseline.StructuralWriter");
    EXPECT_EQ(graph.PassName(layers[1][0]), "Module.ActiveWorldReader");

    ASSERT_TRUE(graph.Execute().has_value());
    EXPECT_TRUE(moduleReaderObservedWriter);
}

TEST(RuntimeModuleSchedule, WaitOnExternalBaselineSignalIsSatisfied)
{
    // BUG-069/BUG-072: a module may wait on a signal emitted by the baseline ECS
    // bundle (registered outside the schedule) when that label is seeded as
    // externally provided; boot fails closed only when it is not seeded.
    const Core::Hash::StringID baseline{"TransformUpdate"};

    Runtime::RuntimeModuleSchedule seeded;
    seeded.RegisterSimSystem(
        "Module",
        Runtime::SimSystemDesc{.Name = "consumer", .WaitForSignals = {baseline}});
    const std::array<Core::Hash::StringID, 1> external{baseline};
    EXPECT_TRUE(seeded.FinalizeForBoot(external).has_value());

    Runtime::RuntimeModuleSchedule bare;
    bare.RegisterSimSystem(
        "Module",
        Runtime::SimSystemDesc{.Name = "consumer", .WaitForSignals = {baseline}});
    const Core::Result result = bare.FinalizeForBoot({});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Core::ErrorCode::InvalidState);
}

TEST(RuntimeModuleViewportInput,
     EngineSetupRejectsEmptyHookAndMissingRegistrar)
{
    Runtime::CommandBus commands;
    Runtime::KernelEventBus events;
    Runtime::JobService jobs;
    Runtime::WorldRegistry worlds;
    Runtime::ServiceRegistry services;
    (void)worlds.CreateWorld("Viewport setup");

    Runtime::EngineSetup missingRegistrar(
        commands,
        events,
        jobs,
        worlds,
        services,
        [](Runtime::SimSystemDesc) {},
        [](Runtime::FramePhase,
           Runtime::RuntimeFrameHook) {});
    EXPECT_EQ(missingRegistrar.InitializedState(), nullptr);

    const Core::Result empty =
        missingRegistrar.RegisterViewportInputHook({});
    ASSERT_FALSE(empty.has_value());
    EXPECT_EQ(empty.error(), Core::ErrorCode::InvalidArgument);

    const Core::Result unavailable =
        missingRegistrar.RegisterViewportInputHook(
            [](Runtime::RuntimeViewportInputHookContext&) {});
    ASSERT_FALSE(unavailable.has_value());
    EXPECT_EQ(unavailable.error(), Core::ErrorCode::InvalidState);

    std::uint32_t registrations = 0u;
    bool initialized = false;
    Runtime::EngineSetup available(
        commands,
        events,
        jobs,
        worlds,
        services,
        [](Runtime::SimSystemDesc) {},
        [](Runtime::FramePhase,
           Runtime::RuntimeFrameHook) {},
        {},
        [&registrations](
            Runtime::RuntimeViewportInputHook hook)
        {
            if (hook)
                ++registrations;
        },
        &initialized);
    ASSERT_EQ(available.InitializedState(), &initialized);
    EXPECT_FALSE(*available.InitializedState());
    initialized = true;
    EXPECT_TRUE(*available.InitializedState());
    EXPECT_TRUE(
        available.RegisterViewportInputHook(
                     [](Runtime::RuntimeViewportInputHookContext&) {})
            .has_value());
    EXPECT_EQ(registrations, 1u);
}

TEST(RuntimeModuleViewportInput,
     ScheduleSortsByModuleThenSharedSequenceAndClearRemovesHooks)
{
    Runtime::RuntimeModuleSchedule schedule;
    std::vector<std::string> trace;
    schedule.RegisterViewportInputHook(
        "Z.Module",
        [&trace](Runtime::RuntimeViewportInputHookContext&)
        {
            trace.emplace_back("Z:first");
        });
    schedule.RegisterViewportInputHook(
        "A.Module",
        [&trace](Runtime::RuntimeViewportInputHookContext&)
        {
            trace.emplace_back("A:first");
        });
    schedule.RegisterViewportInputHook(
        "A.Module",
        [&trace](Runtime::RuntimeViewportInputHookContext&)
        {
            trace.emplace_back("A:second");
        });
    ASSERT_TRUE(schedule.FinalizeForBoot({}).has_value());

    CoreConfig::EngineConfig config =
        NullWindowHeadlessConfig();
    Extrinsic::Platform::Input::Context input;
    Runtime::EditorInputCaptureSnapshot capture{};
    Extrinsic::Graphics::RenderFrameInput renderInput{};
    const auto dispatch =
        [&]()
        {
            schedule.RunViewportInputHooks(
                Runtime::RuntimeViewportInputHookContext{
                    .Config = config,
                    .ActiveWorldHandle =
                        Runtime::DefaultWorldHandle,
                    .Input = input,
                    .Viewport = Core::Extent2D{640, 480},
                    .EditorCapture = capture,
                    .RenderInput = renderInput,
                    .FrameDeltaSeconds = 1.0 / 60.0,
                });
        };
    dispatch();
    EXPECT_EQ(
        trace,
        (std::vector<std::string>{
            "A:first", "A:second", "Z:first"}));

    schedule.Clear();
    dispatch();
    EXPECT_EQ(trace.size(), 3u);

    schedule.RegisterViewportInputHook(
        "Same.Module",
        [&trace](Runtime::RuntimeViewportInputHookContext&)
        {
            trace.emplace_back("same:first");
        });
    schedule.RegisterViewportInputHook(
        "Same.Module",
        [&trace](Runtime::RuntimeViewportInputHookContext&)
        {
            trace.emplace_back("same:second");
        });
    ASSERT_TRUE(schedule.FinalizeForBoot({}).has_value());
    dispatch();
    ASSERT_EQ(trace.size(), 5u);
    EXPECT_EQ(trace[3], "same:first");
    EXPECT_EQ(trace[4], "same:second");
}

namespace
{
    struct DualViewportHookState
    {
        std::vector<std::string> Trace{};
        std::uint32_t VariableTicks{0u};
    };

    class DualPhaseViewportHookModule final
        : public Runtime::IRuntimeModule
    {
    public:
        explicit DualPhaseViewportHookModule(
            DualViewportHookState& state)
            : m_State(state)
        {
        }

        [[nodiscard]] std::string_view Name()
            const noexcept override
        {
            return "Viewport.DualPhase";
        }

        [[nodiscard]] Core::Result OnRegister(
            Runtime::EngineSetup& setup) override
        {
            return setup.RegisterViewportInputHook(
                [this](
                    Runtime::RuntimeViewportInputHookContext&)
                {
                    m_State.Trace.emplace_back("registration");
                });
        }

        [[nodiscard]] Core::Result OnResolve(
            Runtime::EngineSetup& setup) override
        {
            return setup.RegisterViewportInputHook(
                [this](
                    Runtime::RuntimeViewportInputHookContext&)
                {
                    m_State.Trace.emplace_back("resolution");
                });
        }

        void OnShutdown(
            Runtime::RuntimeModuleShutdownContext&) override {}

    private:
        DualViewportHookState& m_State;
    };

    class DualViewportHookApplication final
        : public Runtime::IApplication
    {
    public:
        explicit DualViewportHookApplication(
            DualViewportHookState& state)
            : m_State(state)
        {
        }

        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(
            Runtime::Engine& engine, double, double) override
        {
            ++m_State.VariableTicks;
            engine.RequestExit();
        }
        void OnShutdown(Runtime::Engine&) override {}

    private:
        DualViewportHookState& m_State;
    };
}

TEST(RuntimeModuleViewportInput,
     EngineWiresRegistrationAndResolutionRegistrars)
{
    DualViewportHookState state;
    Runtime::Engine engine(
        NullWindowHeadlessConfig(),
        std::make_unique<DualViewportHookApplication>(
            state));
    engine.AddModule(
        std::make_unique<DualPhaseViewportHookModule>(
            state));
    engine.Initialize();
    engine.Run();

    EXPECT_EQ(state.VariableTicks, 1u);
    EXPECT_EQ(
        state.Trace,
        (std::vector<std::string>{
            "registration", "resolution"}));

    engine.Shutdown();
}
