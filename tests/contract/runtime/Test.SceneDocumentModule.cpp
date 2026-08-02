#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;
import Extrinsic.Core.Tasks;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

namespace
{
    namespace Core = Extrinsic::Core;
    namespace ECS = Extrinsic::ECS;
    namespace ECSC = Extrinsic::ECS::Components;
    namespace Runtime = Extrinsic::Runtime;

    using namespace std::chrono_literals;

    template <typename Predicate>
    [[nodiscard]] bool WaitUntil(
        Predicate&& predicate,
        const std::chrono::milliseconds timeout = 5s)
    {
        const auto deadline =
            std::chrono::steady_clock::now() + timeout;
        while (!predicate())
        {
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            std::this_thread::sleep_for(1ms);
        }
        return true;
    }

    // Whether the worker body has finished and the record is parked at the
    // main-thread apply gate. Queued document operations must reach this state
    // before a test can decide what the next drain observes.
    [[nodiscard]] bool WaitUntilAwaitingGate(
        Runtime::JobService& jobs,
        const Runtime::JobToken task)
    {
        return WaitUntil(
            [&jobs, task]
            {
                return jobs.GetState(task) ==
                       Runtime::JobState::AwaitingGate;
            });
    }

    // Drain until the job is terminal rather than a fixed number of times: a
    // completion drain only advances records the worker has already queued, so
    // a fixed count races the worker (RUNTIME-194 Slice B0).
    [[nodiscard]] bool DrainUntilTerminal(
        Runtime::JobService& jobs,
        Runtime::KernelEventBus& events,
        const Runtime::JobToken task)
    {
        return WaitUntil(
            [&jobs, &events, task]
            {
                (void)jobs.DrainCompletions(events);
                return jobs.IsComplete(task);
            });
    }

    class TempSceneFile final
    {
    public:
        TempSceneFile(
            const std::string_view name,
            const std::string_view contents)
            : Path(std::filesystem::temp_directory_path() /
                   std::string(name))
        {
            std::ofstream out(
                Path, std::ios::binary | std::ios::trunc);
            out << contents;
        }

        ~TempSceneFile()
        {
            std::error_code ignored{};
            std::filesystem::remove(Path, ignored);
        }

        std::filesystem::path Path;
    };

    class StubApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override {}
        void Frame(double, double) override
        {
            auto& engine = Kernel();
            RanFrame     = true;
            if (Runtime::SceneDocumentModule* documents =
                    engine.Services()
                        .Find<Runtime::SceneDocumentModule>();
                documents != nullptr)
            {
                DocumentOperationSucceeded =
                    documents->NewSceneDocument().has_value();
            }
            ExactHistoryPublished =
                engine.Services()
                    .Find<Runtime::EditorCommandHistory>() !=
                nullptr;
            engine.RequestExit();
        }
        void Shutdown() override {}

        bool RanFrame{false};
        bool DocumentOperationSucceeded{false};
        bool ExactHistoryPublished{false};
    };

    [[nodiscard]] Core::Config::EngineConfig HeadlessConfig()
    {
        Core::Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend =
            Core::Config::WindowBackend::Null;
        return config;
    }

    struct ModuleHarness
    {
        ModuleHarness()
        {
            World = Worlds.CreateWorld("SceneDocumentTest");
        }

        [[nodiscard]] Runtime::EngineSetup MakeSetup()
        {
            return Runtime::EngineSetup{
                Commands,
                Events,
                Jobs,
                Worlds,
                Services,
                [](Runtime::FramePhase,
                   Runtime::RuntimeFrameHook) {},
            };
        }

        // Registers the module without resolving it, so the fail-closed path
        // for a module whose dependencies are not yet wired stays reachable.
        [[nodiscard]] Core::Result Register()
        {
            Services.BeginRegistration();
            if (!Module)
            {
                Module =
                    std::make_unique<
                        Runtime::SceneDocumentModule>();
            }
            Runtime::EngineSetup setup = MakeSetup();
            if (Core::Result registered =
                    Module->OnRegister(setup);
                !registered.has_value())
            {
                return registered;
            }
            Started = true;
            return Core::Ok();
        }

        [[nodiscard]] Core::Result Start()
        {
            Services.BeginRegistration();
            if (!Module)
            {
                Module =
                    std::make_unique<
                        Runtime::SceneDocumentModule>();
            }
            Runtime::EngineSetup setup = MakeSetup();
            if (Core::Result registered =
                    Module->OnRegister(setup);
                !registered.has_value())
            {
                return registered;
            }
            Services.BeginResolution();
            if (Core::Result resolved =
                    Module->OnResolve(setup);
                !resolved.has_value())
            {
                Runtime::RuntimeModuleShutdownContext context{
                    .Commands = Commands,
                    .Events = Events,
                    .Jobs = Jobs,
                    .Worlds = Worlds,
                    .Services = Services,
                };
                Module->OnShutdown(context);
                return resolved;
            }
            Services.Lock();
            Started = true;
            return Core::Ok();
        }

        void Stop()
        {
            if (!Started || !Module)
                return;
            Events.Publish(Runtime::RuntimeShutdownAnnounced{});
            (void)Events.Pump();
            Runtime::RuntimeModuleShutdownContext context{
                .Commands = Commands,
                .Events = Events,
                .Jobs = Jobs,
                .Worlds = Worlds,
                .Services = Services,
            };
            Module->OnShutdown(context);
            Started = false;
        }

        ~ModuleHarness()
        {
            Stop();
            Services.Reset();
        }

        Runtime::CommandBus Commands{};
        Runtime::KernelEventBus Events{};
        Runtime::JobService Jobs{};
        Runtime::WorldRegistry Worlds{};
        Runtime::ServiceRegistry Services{};
        std::unique_ptr<Runtime::SceneDocumentModule> Module{};
        Runtime::WorldHandle World{};
        bool Started{false};
    };

    [[nodiscard]] bool ContainsNamedEntity(
        const ECS::Scene::Registry& registry,
        const std::string_view name)
    {
        bool found = false;
        registry.Raw().view<const ECSC::MetaData>().each(
            [&](const ECS::EntityHandle,
                const ECSC::MetaData& metadata)
            {
                if (metadata.EntityName == name)
                    found = true;
            });
        return found;
    }
}

TEST(SceneDocumentModule,
     PublishesExactServicesAndWithdraws)
{
    ModuleHarness harness;
    EXPECT_EQ(
        harness.Services
            .Find<Runtime::SceneDocumentModule>(),
        nullptr);
    ASSERT_TRUE(harness.Start().has_value());

    EXPECT_EQ(
        harness.Services
            .Find<Runtime::SceneDocumentModule>(),
        harness.Module.get());
    Runtime::EditorCommandHistory* const history =
        harness.Services
            .Find<Runtime::EditorCommandHistory>();
    ASSERT_NE(history, nullptr);
    EXPECT_TRUE(
        harness.Module->NewSceneDocument().has_value());

    harness.Stop();
    EXPECT_EQ(
        harness.Services
            .Find<Runtime::SceneDocumentModule>(),
        nullptr);
    EXPECT_EQ(
        harness.Services
            .Find<Runtime::EditorCommandHistory>(),
        nullptr);

    ASSERT_TRUE(harness.Start().has_value());
    const Runtime::EditorCommandHistorySnapshot snapshot =
        harness.Services
            .Find<Runtime::EditorCommandHistory>()
            ->Snapshot();
    EXPECT_EQ(snapshot.Revision, 0u);
    EXPECT_FALSE(snapshot.HasActivePath);
}

TEST(SceneDocumentModule,
     QueuedDocumentIoFailsClosedBeforeResolution)
{
    // The module publishes itself during registration, so a caller can reach
    // the queued surface before the kernel's execution service has been
    // resolved. RUNTIME-194 removed the optional async executor, and this is
    // the one remaining path where no service is bound: it must still fail
    // closed rather than submit to nothing.
    ModuleHarness harness;
    ASSERT_TRUE(harness.Register().has_value());
    Runtime::SceneDocumentModule* const documents =
        harness.Services.Find<Runtime::SceneDocumentModule>();
    ASSERT_EQ(documents, harness.Module.get());

    const auto queuedSave =
        documents->QueueSceneSaveToPath("not-submitted.scene");
    ASSERT_FALSE(queuedSave.has_value());
    EXPECT_EQ(
        queuedSave.error(), Core::ErrorCode::InvalidState);

    const auto queuedLoad =
        documents->QueueSceneLoadFromPath("not-submitted.scene");
    ASSERT_FALSE(queuedLoad.has_value());
    EXPECT_EQ(
        queuedLoad.error(), Core::ErrorCode::InvalidState);

    EXPECT_EQ(harness.Jobs.Stats().SubmittedJobs, 0u);
}

TEST(SceneDocumentModule,
     DuplicateHistoryPublicationRollsBackPartialModuleService)
{
    ModuleHarness harness;
    harness.Services.BeginRegistration();
    Runtime::EditorCommandHistory conflictingHistory{};
    ASSERT_TRUE(
        harness.Services
            .Provide<Runtime::EditorCommandHistory>(
                conflictingHistory, "Conflict")
            .has_value());

    harness.Module =
        std::make_unique<Runtime::SceneDocumentModule>();
    Runtime::EngineSetup setup = harness.MakeSetup();
    const Core::Result registered =
        harness.Module->OnRegister(setup);
    EXPECT_FALSE(registered.has_value());
    EXPECT_EQ(
        harness.Services
            .Find<Runtime::SceneDocumentModule>(),
        nullptr);
    EXPECT_EQ(
        harness.Services
            .Find<Runtime::EditorCommandHistory>(),
        &conflictingHistory);
}

TEST(SceneDocumentModule,
     ReplacementParticipantsAreDeterministicAndHandleScoped)
{
    ModuleHarness harness;
    ASSERT_TRUE(harness.Start().has_value());
    ECS::Scene::Registry* const scene =
        harness.Worlds.Get(harness.World);
    ASSERT_NE(scene, nullptr);
    const ECS::EntityHandle outgoing = scene->Create();
    std::vector<std::string> calls;

    auto beta =
        harness.Module->RegisterReplacementParticipant(
            Runtime::SceneReplacementParticipantDesc{
                .Name = "Beta",
                .BeforeReplace =
                    [&calls, outgoing](
                        const Runtime::SceneReplacementContext&
                            context)
                    {
                        EXPECT_EQ(
                            context.Registry.IsValid(outgoing),
                            context.Kind ==
                                Runtime::SceneReplacementKind::
                                    New);
                        calls.push_back("Beta.Before");
                    },
                .AfterReplace =
                    [&calls, outgoing](
                        const Runtime::SceneReplacementContext&
                            context)
                    {
                        EXPECT_FALSE(
                            context.Registry.IsValid(outgoing));
                        calls.push_back("Beta.After");
                    },
            });
    auto alpha =
        harness.Module->RegisterReplacementParticipant(
            Runtime::SceneReplacementParticipantDesc{
                .Name = "Alpha",
                .BeforeReplace =
                    [&calls](
                        const Runtime::SceneReplacementContext&)
                    {
                        calls.push_back("Alpha.Before");
                    },
                .AfterReplace =
                    [&calls](
                        const Runtime::SceneReplacementContext&)
                    {
                        calls.push_back("Alpha.After");
                    },
            });
    ASSERT_TRUE(alpha.has_value());
    ASSERT_TRUE(beta.has_value());

    const auto duplicate =
        harness.Module->RegisterReplacementParticipant(
            Runtime::SceneReplacementParticipantDesc{
                .Name = "Alpha",
                .BeforeReplace = {},
                .AfterReplace = {},
            });
    EXPECT_FALSE(duplicate.has_value());

    ASSERT_TRUE(
        harness.Module->NewSceneDocument().has_value());
    EXPECT_EQ(
        calls,
        (std::vector<std::string>{
            "Alpha.Before",
            "Beta.Before",
            "Alpha.After",
            "Beta.After"}));

    ASSERT_TRUE(
        harness.Module
            ->UnregisterReplacementParticipant(*alpha)
            .has_value());
    calls.clear();
    ASSERT_TRUE(
        harness.Module->CloseSceneDocument().has_value());
    EXPECT_EQ(
        calls,
        (std::vector<std::string>{
            "Beta.Before", "Beta.After"}));
    EXPECT_FALSE(
        harness.Module
            ->UnregisterReplacementParticipant(*alpha)
            .has_value());

    // The document owner quiesces first on shutdown announcement. Later
    // participant owners must still be able to release their exact handles
    // before reverse module teardown destroys the provider state.
    harness.Events.Publish(
        Runtime::RuntimeShutdownAnnounced{});
    (void)harness.Events.Pump();
    EXPECT_TRUE(
        harness.Module
            ->UnregisterReplacementParticipant(*beta)
            .has_value());
}

TEST(SceneDocumentModule,
     ParseFailureRunsNoParticipantAndPreservesDocumentState)
{
    TempSceneFile saved(
        "runtime172-preserved.scene.json", "");
    TempSceneFile invalid(
        "runtime172-invalid.scene.json", "not json");
    ModuleHarness harness;
    ASSERT_TRUE(harness.Start().has_value());
    ECS::Scene::Registry* const scene =
        harness.Worlds.Get(harness.World);
    ASSERT_NE(scene, nullptr);
    const ECS::EntityHandle entity = scene->Create();
    scene->Raw().emplace<ECSC::MetaData>(
        entity, ECSC::MetaData{.EntityName = "Preserved"});
    ASSERT_TRUE(
        harness.Module
            ->SaveSceneToPath(saved.Path.string())
            .has_value());
    Runtime::EditorCommandHistory* const history =
        harness.Services
            .Find<Runtime::EditorCommandHistory>();
    ASSERT_NE(history, nullptr);
    (void)history->MarkDirty("Preserved edit");
    const Runtime::EditorCommandHistorySnapshot before =
        history->Snapshot();

    std::uint32_t callbacks = 0u;
    ASSERT_TRUE(
        harness.Module->RegisterReplacementParticipant(
            Runtime::SceneReplacementParticipantDesc{
                .Name = "Probe",
                .BeforeReplace =
                    [&callbacks](
                        const Runtime::SceneReplacementContext&)
                    {
                        ++callbacks;
                    },
                .AfterReplace =
                    [&callbacks](
                        const Runtime::SceneReplacementContext&)
                    {
                        ++callbacks;
                    },
            }).has_value());

    const auto loaded =
        harness.Module->LoadSceneFromPath(
            invalid.Path.string());
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error(), Core::ErrorCode::InvalidFormat);
    EXPECT_EQ(callbacks, 0u);
    EXPECT_TRUE(scene->IsValid(entity));
    EXPECT_EQ(
        history->Snapshot().ActivePath,
        saved.Path.string());
    const Runtime::EditorCommandHistorySnapshot after =
        history->Snapshot();
    EXPECT_EQ(after.Revision, before.Revision);
    EXPECT_EQ(after.SavedRevision, before.SavedRevision);
    EXPECT_EQ(after.ActivePath, before.ActivePath);
    EXPECT_FALSE(
        harness.Module->GetLastSceneFileEvent().has_value());
}

TEST(SceneDocumentModule,
     DirectValidationResetsOneBindingWithoutStateResurrection)
{
    TempSceneFile saved(
        "runtime172-world-binding.scene.json", "");
    ModuleHarness harness;
    ASSERT_TRUE(harness.Start().has_value());
    ASSERT_TRUE(
        harness.Module
            ->SaveSceneToPath(saved.Path.string())
            .has_value());
    Runtime::EditorCommandHistory* const history =
        harness.Services
            .Find<Runtime::EditorCommandHistory>();
    ASSERT_NE(history, nullptr);
    (void)history->MarkDirty("World A edit");

    const Runtime::WorldHandle away =
        harness.Worlds.CreateWorld("Away");
    ASSERT_TRUE(
        harness.Worlds.RequestSetActiveWorld(away)
            .has_value());
    EXPECT_EQ(
        harness.Worlds
            .ApplyMaintenance(harness.Events, harness.Jobs)
            .AppliedActiveWorldChanges,
        1u);

    // Do not pump the delayed event. The public operation must observe the
    // registry directly and reset the durable state immediately.
    EXPECT_FALSE(
        harness.Module->GetLastSceneFileEvent().has_value());
    EXPECT_EQ(history->Snapshot().Revision, 0u);
    EXPECT_FALSE(history->Snapshot().HasActivePath);

    ASSERT_TRUE(
        harness.Worlds
            .RequestSetActiveWorld(harness.World)
            .has_value());
    EXPECT_EQ(
        harness.Worlds
            .ApplyMaintenance(harness.Events, harness.Jobs)
            .AppliedActiveWorldChanges,
        1u);
    EXPECT_FALSE(
        harness.Module->GetLastSceneFileEvent().has_value());
    EXPECT_EQ(history->Snapshot().Revision, 0u);

    // The former active world may retire after the switch without disturbing
    // the rebound document owner.
    ASSERT_TRUE(
        harness.Worlds.RequestDestroyWorld(away)
            .has_value());
    (void)harness.Worlds.ApplyMaintenance(
        harness.Events, harness.Jobs);
    (void)harness.Worlds.ApplyMaintenance(
        harness.Events, harness.Jobs);
    EXPECT_EQ(harness.Worlds.Get(away), nullptr);
    EXPECT_FALSE(
        harness.Module->GetLastSceneFileEvent().has_value());

    const Runtime::WorldHandle unrelated =
        harness.Worlds.CreateWorld("Unrelated");
    ASSERT_TRUE(
        harness.Worlds.RequestDestroyWorld(unrelated)
            .has_value());
    (void)harness.Worlds.ApplyMaintenance(
        harness.Events, harness.Jobs);
    (void)harness.Worlds.ApplyMaintenance(
        harness.Events, harness.Jobs);
    EXPECT_FALSE(
        harness.Module->GetLastSceneFileEvent().has_value());
    EXPECT_EQ(
        harness.Worlds
            .RequestDestroyWorld(harness.World)
            .error(),
        Core::ErrorCode::ResourceBusy);
}

TEST(SceneDocumentModule,
     LateQueuedCompletionCannotOutliveModuleState)
{
    TempSceneFile valid(
        "runtime172-lifetime.scene.json",
        R"({"version":2,"entities":[{"id":0,"name":"Late"}]})");
    const bool schedulerWasInitialized =
        Core::Tasks::Scheduler::IsInitialized();
    if (!schedulerWasInitialized)
        Core::Tasks::Scheduler::Initialize(1u);

    ModuleHarness harness;
    ASSERT_TRUE(harness.Start().has_value());
    ECS::Scene::Registry* const scene =
        harness.Worlds.Get(harness.World);
    ASSERT_NE(scene, nullptr);
    const ECS::EntityHandle marker = scene->Create();
    scene->Raw().emplace<ECSC::MetaData>(
        marker, ECSC::MetaData{.EntityName = "Marker"});

    const auto queued =
        harness.Module->QueueSceneLoadFromPath(
            valid.Path.string());
    ASSERT_TRUE(queued.has_value());
    ASSERT_TRUE(
        WaitUntilAwaitingGate(harness.Jobs, queued->Task));

    harness.Stop();
    harness.Module.reset();
    ASSERT_TRUE(DrainUntilTerminal(
        harness.Jobs, harness.Events, queued->Task));

    EXPECT_TRUE(scene->IsValid(marker));
    EXPECT_TRUE(ContainsNamedEntity(*scene, "Marker"));
    EXPECT_FALSE(ContainsNamedEntity(*scene, "Late"));

    if (!schedulerWasInitialized)
        Core::Tasks::Scheduler::Shutdown();
}

TEST(SceneDocumentModule,
     WorldRebindResetsFileEventAndItsSequence)
{
    TempSceneFile first(
        "runtime172-event-sequence-first.scene.json", "");
    TempSceneFile second(
        "runtime172-event-sequence-second.scene.json", "");
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<StubApplication>());
    engine.EmplaceModule<Runtime::AsyncWorkModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.Initialize();

    Runtime::SceneDocumentModule& documents =
        *engine.Services()
             .Find<Runtime::SceneDocumentModule>();
    const auto complete =
        [&engine](const Runtime::JobToken task)
        {
            ASSERT_TRUE(DrainUntilTerminal(
                engine.Jobs(), engine.Events(), task));
            ASSERT_EQ(
                engine.Jobs().GetState(task),
                Runtime::JobState::Published);
        };

    const auto firstQueued =
        documents.QueueSceneSaveToPath(first.Path.string());
    ASSERT_TRUE(firstQueued.has_value());
    complete(firstQueued->Task);
    ASSERT_TRUE(
        documents.GetLastSceneFileEvent().has_value());
    EXPECT_EQ(
        documents.GetLastSceneFileEvent()->Sequence, 1u);

    const Runtime::WorldHandle replacement =
        engine.Worlds().CreateWorld("Replacement");
    ASSERT_TRUE(
        engine.Worlds()
            .RequestSetActiveWorld(replacement)
            .has_value());
    EXPECT_EQ(
        engine.Worlds()
            .ApplyMaintenance(engine.Events(), engine.Jobs())
            .AppliedActiveWorldChanges,
        1u);

    // Direct access observes the changed active binding before the queued
    // ActiveWorldChanged notification is pumped.
    EXPECT_FALSE(
        documents.GetLastSceneFileEvent().has_value());
    const auto secondQueued =
        documents.QueueSceneSaveToPath(second.Path.string());
    ASSERT_TRUE(secondQueued.has_value());
    complete(secondQueued->Task);
    ASSERT_TRUE(
        documents.GetLastSceneFileEvent().has_value());
    EXPECT_EQ(
        documents.GetLastSceneFileEvent()->Sequence, 1u);
    engine.Shutdown();
}

TEST(SceneDocumentModule,
     RealEngineRunProvidesOperationalDocumentOwner)
{
    {
        auto omittedApplication =
            std::make_unique<StubApplication>();
        Intrinsic::Tests::RuntimeTestKernel omitted(HeadlessConfig(),
                                                    std::move(omittedApplication));
        omitted.Initialize();
        EXPECT_EQ(
            omitted.Services()
                .Find<Runtime::SceneDocumentModule>(),
            nullptr);
        EXPECT_EQ(
            omitted.Services()
                .Find<Runtime::EditorCommandHistory>(),
            nullptr);
        EXPECT_TRUE(omitted.ActiveWorld().IsValid());
        omitted.Run();
        omitted.Shutdown();
    }

    auto application = std::make_unique<StubApplication>();
    StubApplication* const app = application.get();
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::move(application));
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.Initialize();

    ASSERT_NE(
        engine.Services()
            .Find<Runtime::SceneDocumentModule>(),
        nullptr);
    ASSERT_NE(
        engine.Services()
            .Find<Runtime::EditorCommandHistory>(),
        nullptr);
    engine.Run();

    EXPECT_TRUE(app->RanFrame);
    EXPECT_TRUE(app->DocumentOperationSucceeded);
    EXPECT_TRUE(app->ExactHistoryPublished);
    engine.Shutdown();
}

TEST(SceneDocumentModule,
     EngineReplacementParticipantsResetAcrossReinitialize)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<StubApplication>());
    engine.EmplaceModule<Runtime::SceneDocumentModule>();

    for (std::uint32_t boot = 0u; boot < 2u; ++boot)
    {
        engine.Initialize();
        Runtime::SceneDocumentModule* const documents =
            engine.Services()
                .Find<Runtime::SceneDocumentModule>();
        ASSERT_NE(documents, nullptr);
        EXPECT_TRUE(
            documents->NewSceneDocument().has_value());
        engine.Shutdown();
        EXPECT_EQ(
            engine.Services()
                .Find<Runtime::SceneDocumentModule>(),
            nullptr);
    }
}
