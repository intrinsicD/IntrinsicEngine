#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.FrameGraph;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldRegistry;

using Extrinsic::Runtime::CommandBus;
using Extrinsic::Runtime::Engine;
using Extrinsic::Runtime::EngineSetup;
using Extrinsic::Runtime::EngineWillShutDown;
using Extrinsic::Runtime::EventBus;
using Extrinsic::Runtime::EventSubscriptionHandle;
using Extrinsic::Runtime::FrameHookContext;
using Extrinsic::Runtime::FramePhase;
using Extrinsic::Runtime::IApplication;
using Extrinsic::Runtime::IRuntimeModule;
using Extrinsic::Runtime::JobService;
using Extrinsic::Runtime::ModuleRegistrationSink;
using Extrinsic::Runtime::ServiceRegistry;
using Extrinsic::Runtime::SimSystemDesc;
using Extrinsic::Runtime::WorldRegistry;

namespace
{
    // ── Test doubles ─────────────────────────────────────────────────────

    class NoopApplication final : public IApplication
    {
    public:
        void OnInitialize(Engine&) override {}
        void OnSimTick(Engine&, double) override {}
        void OnVariableTick(Engine&, double, double) override {}
        void OnShutdown(Engine&) override {}
    };

    [[nodiscard]] Extrinsic::Core::Config::EngineConfig MinimalEngineConfig()
    {
        Extrinsic::Core::Config::EngineConfig config{};
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        return config;
    }

    // A trivial always-present service one module provides and another binds.
    struct CounterService
    {
        int Value{0};
    };

    class ProviderModule final : public IRuntimeModule
    {
    public:
        [[nodiscard]] std::string_view Name() const noexcept override
        {
            return "ProviderModule";
        }
        void OnRegister(EngineSetup& setup) override
        {
            setup.Services.Provide<CounterService>(Service);
        }

        CounterService Service{.Value = 42};
    };

    class ConsumerModule final : public IRuntimeModule
    {
    public:
        [[nodiscard]] std::string_view Name() const noexcept override
        {
            return "ConsumerModule";
        }
        void OnRegister(EngineSetup&) override {}
        void OnResolve(ServiceRegistry& services) override
        {
            Resolved = &services.Require<CounterService>();
        }

        CounterService* Resolved{nullptr};
    };

    // Two systems in two separate modules that share a resource token so the
    // FrameGraph must order the writer before the reader regardless of the
    // order the modules were added to the engine (ADR-0024 D1/D3).
    struct OrderResourceToken
    {
    };

    struct OrderingProbe
    {
        std::atomic<bool> WriterRan{false};
        std::atomic<bool> ReaderSawWriterFirst{false};
        std::atomic<int>  WriterRuns{0};
        std::atomic<int>  ReaderRuns{0};
    };

    class WriterSystemModule final : public IRuntimeModule
    {
    public:
        explicit WriterSystemModule(OrderingProbe& probe) : m_Probe(probe) {}
        [[nodiscard]] std::string_view Name() const noexcept override
        {
            return "WriterSystemModule";
        }
        void OnRegister(EngineSetup& setup) override
        {
            setup.RegisterSimSystem(SimSystemDesc{
                "ordering.writer",
                [](Extrinsic::Core::FrameGraphBuilder& builder)
                { builder.Write<OrderResourceToken>(); },
                [this](Extrinsic::ECS::Scene::Registry&)
                {
                    m_Probe.WriterRan.store(true, std::memory_order_release);
                    m_Probe.WriterRuns.fetch_add(1, std::memory_order_acq_rel);
                }});
        }

    private:
        OrderingProbe& m_Probe;
    };

    class ReaderSystemModule final : public IRuntimeModule
    {
    public:
        explicit ReaderSystemModule(OrderingProbe& probe) : m_Probe(probe) {}
        [[nodiscard]] std::string_view Name() const noexcept override
        {
            return "ReaderSystemModule";
        }
        void OnRegister(EngineSetup& setup) override
        {
            setup.RegisterSimSystem(SimSystemDesc{
                "ordering.reader",
                [](Extrinsic::Core::FrameGraphBuilder& builder)
                { builder.Read<OrderResourceToken>(); },
                [this](Extrinsic::ECS::Scene::Registry&)
                {
                    m_Probe.ReaderSawWriterFirst.store(
                        m_Probe.WriterRan.load(std::memory_order_acquire),
                        std::memory_order_release);
                    m_Probe.ReaderRuns.fetch_add(1, std::memory_order_acq_rel);
                }});
        }

    private:
        OrderingProbe& m_Probe;
    };

    class ShutdownObserverModule final : public IRuntimeModule
    {
    public:
        [[nodiscard]] std::string_view Name() const noexcept override
        {
            return "ShutdownObserverModule";
        }
        void OnRegister(EngineSetup& setup) override
        {
            m_Subscription = setup.Events.Subscribe<EngineWillShutDown>(
                [this](const EngineWillShutDown&) { SawAnnounce = true; });
        }
        void OnShutdown() override { SawAnnounceAtShutdown = SawAnnounce; }

        EventSubscriptionHandle m_Subscription{};
        bool                    SawAnnounce{false};
        bool                    SawAnnounceAtShutdown{false};
    };

    // Flatten a compiled FrameGraph into its pass names in execution order.
    [[nodiscard]] std::vector<std::string> CompiledModuleSchedule(Engine& engine)
    {
        Extrinsic::Core::FrameGraph graph;
        engine.GetModuleRegistrationForTest().ApplySimSystems(graph, engine.GetScene());
        EXPECT_TRUE(graph.Compile().has_value());
        std::vector<std::string> order;
        for (const std::vector<std::uint32_t>& layer : graph.GetExecutionLayers())
            for (const std::uint32_t pass : layer)
                order.emplace_back(graph.PassName(pass));
        return order;
    }
}

// ── ServiceRegistry (ADR-0024 D3) ────────────────────────────────────────

TEST(RuntimeServiceRegistry, ProvideThenRequireReturnsTheProvidedReference)
{
    ServiceRegistry services;
    CounterService service{.Value = 7};

    services.Provide<CounterService>(service);

    EXPECT_EQ(&services.Require<CounterService>(), &service);
    EXPECT_EQ(services.Find<CounterService>(), &service);
    EXPECT_EQ(services.Require<CounterService>().Value, 7);
    EXPECT_EQ(services.ServiceCount(), 1u);
}

TEST(RuntimeServiceRegistry, FindOfUnprovidedServiceReturnsNullWithoutAborting)
{
    ServiceRegistry services;
    EXPECT_EQ(services.Find<CounterService>(), nullptr);
}

TEST(RuntimeServiceRegistry, RequireOfUnprovidedServiceAbortsBoot)
{
    // ARCH-011 fail-closed (ADR-0024 D3). The Engine's OnResolve loop sets the
    // active requester and calls Require; an unprovided service aborts boot
    // with a diagnostic naming the requester and the missing type. Exercised
    // at the ServiceRegistry level so the death test stays single-threaded
    // (the full Engine boot path forks a scheduler-threaded process, which
    // gtest death tests discourage).
    ServiceRegistry services;
    services.SetActiveRequester("ConsumerModule");
    EXPECT_DEATH((void)services.Require<CounterService>(), "");
}

// ── ModuleRegistrationSink hooks (ADR-0024 D13) ──────────────────────────

TEST(RuntimeModuleRegistration, FrameHooksBucketByPhaseAndInvokeInRegistrationOrder)
{
    ModuleRegistrationSink sink;
    std::vector<int> log;

    sink.AddFrameHook(FramePhase::Maintenance,
                      [&log](FrameHookContext&) { log.push_back(3); });
    sink.AddFrameHook(FramePhase::AfterCommandDrain,
                      [&log](FrameHookContext&) { log.push_back(1); });
    sink.AddFrameHook(FramePhase::AfterCommandDrain,
                      [&log](FrameHookContext&) { log.push_back(2); });

    EXPECT_EQ(sink.FrameHookCount(FramePhase::AfterCommandDrain), 2u);
    EXPECT_EQ(sink.FrameHookCount(FramePhase::UiBuild), 0u);
    EXPECT_EQ(sink.FrameHookCount(FramePhase::BeforeExtraction), 0u);
    EXPECT_EQ(sink.FrameHookCount(FramePhase::Maintenance), 1u);

    // The hooks under test push to `log` only; a minimal substrate satisfies
    // the FrameHookContext's capability references without being touched.
    Extrinsic::ECS::Scene::Registry scene;
    CommandBus    commands;
    EventBus      events;
    JobService    jobs;
    WorldRegistry worlds;
    FrameHookContext context{scene, {}, commands, events, jobs, worlds, 0.0, 0.0};

    sink.InvokeFrameHooks(FramePhase::AfterCommandDrain, context);
    EXPECT_EQ(log, (std::vector<int>{1, 2}));

    sink.InvokeFrameHooks(FramePhase::UiBuild, context);   // no-op, no hooks
    EXPECT_EQ(log, (std::vector<int>{1, 2}));

    sink.InvokeFrameHooks(FramePhase::Maintenance, context);
    EXPECT_EQ(log, (std::vector<int>{1, 2, 3}));
}

// ── Two-phase composition on the Engine (ADR-0024 D1/D3/D12) ──────────────

TEST(RuntimeModuleContract, TwoPhaseBootResolvesProvidedServiceRegardlessOfAddOrder)
{
    for (const bool providerFirst : {true, false})
    {
        Engine engine(MinimalEngineConfig(), std::make_unique<NoopApplication>());

        ProviderModule* provider = nullptr;
        ConsumerModule* consumer = nullptr;
        if (providerFirst)
        {
            provider = &engine.EmplaceModule<ProviderModule>();
            consumer = &engine.EmplaceModule<ConsumerModule>();
        }
        else
        {
            // Consumer added BEFORE provider: OnResolve still finds the service
            // because it runs after EVERY module's OnRegister (D3).
            consumer = &engine.EmplaceModule<ConsumerModule>();
            provider = &engine.EmplaceModule<ProviderModule>();
        }

        engine.Initialize();

        ASSERT_NE(consumer->Resolved, nullptr);
        EXPECT_EQ(consumer->Resolved, &provider->Service);
        EXPECT_EQ(consumer->Resolved->Value, 42);
        EXPECT_GE(engine.Services().ServiceCount(), 1u);

        engine.Shutdown();
    }
}

TEST(RuntimeModuleContract, SimSystemScheduleIsIndependentOfRegistrationOrder)
{
    // One engine at a time (each Initialize()/Shutdown() cycles the shared
    // Core::Tasks scheduler), capturing the compiled schedule for each
    // AddModule permutation before tearing the engine down.
    const auto scheduleFor = [](const bool writerFirst)
    {
        OrderingProbe probe;
        Engine engine(MinimalEngineConfig(), std::make_unique<NoopApplication>());
        if (writerFirst)
        {
            engine.EmplaceModule<WriterSystemModule>(probe);
            engine.EmplaceModule<ReaderSystemModule>(probe);
        }
        else
        {
            engine.EmplaceModule<ReaderSystemModule>(probe);
            engine.EmplaceModule<WriterSystemModule>(probe);
        }
        engine.Initialize();
        std::vector<std::string> schedule = CompiledModuleSchedule(engine);
        engine.Shutdown();
        return schedule;
    };

    const std::vector<std::string> scheduleWriterFirst = scheduleFor(true);
    const std::vector<std::string> scheduleReaderFirst = scheduleFor(false);

    // Identical compiled schedules despite reversed AddModule order, and the
    // writer is ordered before the reader by the declared Read/Write tokens.
    EXPECT_EQ(scheduleWriterFirst, scheduleReaderFirst);
    ASSERT_EQ(scheduleWriterFirst.size(), 2u);
    EXPECT_EQ(scheduleWriterFirst.front(), "ordering.writer");
    EXPECT_EQ(scheduleWriterFirst.back(), "ordering.reader");
}

TEST(RuntimeModuleContract, ModuleSimSystemExecutesWithDeclaredDependencyOrder)
{
    OrderingProbe probe;
    Engine engine(MinimalEngineConfig(), std::make_unique<NoopApplication>());
    // Reader added first; the FrameGraph must still run it after the writer.
    engine.EmplaceModule<ReaderSystemModule>(probe);
    engine.EmplaceModule<WriterSystemModule>(probe);
    engine.Initialize();

    Extrinsic::Core::FrameGraph graph;
    engine.GetModuleRegistrationForTest().ApplySimSystems(graph, engine.GetScene());
    ASSERT_TRUE(graph.Compile().has_value());
    ASSERT_TRUE(graph.Execute().has_value());

    EXPECT_EQ(probe.WriterRuns.load(std::memory_order_acquire), 1);
    EXPECT_EQ(probe.ReaderRuns.load(std::memory_order_acquire), 1);
    EXPECT_TRUE(probe.WriterRan.load(std::memory_order_acquire));
    EXPECT_TRUE(probe.ReaderSawWriterFirst.load(std::memory_order_acquire));

    engine.Shutdown();
}

TEST(RuntimeModuleContract, ShutdownAnnouncesBeforeModuleOnShutdown)
{
    Engine engine(MinimalEngineConfig(), std::make_unique<NoopApplication>());
    ShutdownObserverModule& observer =
        engine.EmplaceModule<ShutdownObserverModule>();

    engine.Initialize();
    EXPECT_FALSE(observer.SawAnnounce);   // no shutdown announce during boot

    engine.Shutdown();
    EXPECT_TRUE(observer.SawAnnounce);
    EXPECT_TRUE(observer.SawAnnounceAtShutdown);   // announce pumped before OnShutdown
}
