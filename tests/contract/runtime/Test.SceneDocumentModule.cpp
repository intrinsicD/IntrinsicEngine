#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#include <unistd.h>
#endif

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Geometry.Properties;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;
import Extrinsic.Core.IOBackend;
import Extrinsic.Core.Tasks;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.Light;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Hierarchy.Mutation;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Graphics.Component.RenderGeometry;
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
        R"({"version":6,"entities":[{"id":0,"name":"Late"}]})");
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

TEST(SceneDocumentModule, PipelinedReplacementWithoutAssetWorkflowConsumesNewScene)
{
    class PipelinedDocumentProbe final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override { AddLights(3u); }

        void Frame(double, double) override
        {
            auto& engine = Kernel();
            ++Frames;
            if (Frames == 3u)
            {
                OldLights = engine.GetRenderer().GetLastRenderGraphStats()
                    .Contract.LightingResolvedLightCount;
                AssetWorkflowAbsent = engine.Services()
                    .Find<Runtime::AssetWorkflowModule>() == nullptr;
                auto* documents = engine.Services()
                    .Find<Runtime::SceneDocumentModule>();
                ReplacementSucceeded = documents != nullptr &&
                    documents->NewSceneDocument().has_value();
                if (ReplacementSucceeded)
                    AddLights(2u);
            }
            if (Frames == 4u)
            {
                NewLights = engine.GetRenderer().GetLastRenderGraphStats()
                    .Contract.LightingResolvedLightCount;
                engine.RequestExit();
            }
        }

        std::uint32_t Frames{0u};
        std::uint32_t OldLights{0u};
        std::uint32_t NewLights{0u};
        bool AssetWorkflowAbsent{false};
        bool ReplacementSucceeded{false};

    private:
        void AddLights(std::uint32_t count)
        {
            auto& engine = Kernel();
            auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
            for (std::uint32_t i = 0u; i < count; ++i)
            {
                const auto entity = scene.Create();
                scene.Raw().emplace<ECSC::Transform::WorldMatrix>(entity);
                scene.Raw().emplace<ECSC::Lights::DirectionalLight>(entity);
            }
        }
    };

    auto config = HeadlessConfig();
    config.Render.SynchronousExtraction = false;
    auto probe = std::make_unique<PipelinedDocumentProbe>();
    const PipelinedDocumentProbe* observed = probe.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(probe));
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.Initialize();
    engine.Run();

    EXPECT_TRUE(observed->AssetWorkflowAbsent);
    EXPECT_TRUE(observed->ReplacementSucceeded);
    EXPECT_EQ(observed->Frames, 4u);
    EXPECT_EQ(observed->OldLights, 3u);
    EXPECT_EQ(observed->NewLights, 2u);
    engine.Shutdown();
}

TEST(SceneDocumentModule, QueuedSaveKeepsLaterEditsDirty)
{
    TempSceneFile file("runtime-save-revision.scene.json", "");
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.EmplaceModule<Runtime::AsyncWorkModule>();
    auto& documents = engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.Initialize();
    auto* history = engine.Services().Find<Runtime::EditorCommandHistory>();
    ASSERT_NE(history, nullptr);
    (void)history->MarkDirty("before snapshot");
    const auto snapshotRevision = history->Snapshot().Revision;
    const auto queued = documents.QueueSceneSaveToPath(file.Path.string());
    ASSERT_TRUE(queued.has_value());
    ASSERT_TRUE(WaitUntilAwaitingGate(engine.Jobs(), queued->Task));
    (void)history->MarkDirty("after snapshot");
    ASSERT_TRUE(DrainUntilTerminal(engine.Jobs(), engine.Events(), queued->Task));
    EXPECT_EQ(engine.Jobs().GetState(queued->Task), Runtime::JobState::Published);
    EXPECT_TRUE(history->IsDirty());
    EXPECT_EQ(history->Snapshot().SavedRevision, snapshotRevision);
    engine.Shutdown();
}

TEST(SceneDocumentModule, QueuedSavePreservesAttributeBindings)
{
    TempSceneFile file("runtime-save-bindings.scene.json", "");
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.EmplaceModule<Runtime::AsyncWorkModule>();
    auto& documents = engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.Initialize();
    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const auto entity = scene.Create();
    scene.Raw().emplace<Extrinsic::Graphics::Components::RenderBounds>(entity);
    auto& properties = scene.Raw().emplace<ECSC::GeometrySources::Vertices>(entity).Properties;
    properties.Resize(2u);
    (void)properties.GetOrAdd<glm::vec3>("v:position", glm::vec3{1.0f});
    (void)properties.GetOrAdd<glm::vec3>("v:custom_normal", glm::vec3{0.0f, 0.0f, 1.0f});
    const Runtime::VertexChannelSourceBinding normal{
        .Enabled = true,
        .Property = {Runtime::GeometryElementDomain::PointCloudPoint, "v:custom_normal", Geometry::PropertyValueKind::Vec3},
    };
    scene.Raw().emplace<Runtime::VertexChannelBindingSet>(entity, Runtime::VertexChannelBindingSet{.Normal = normal});
    const auto queued = documents.QueueSceneSaveToPath(file.Path.string());
    ASSERT_TRUE(queued.has_value());
    ASSERT_TRUE(DrainUntilTerminal(engine.Jobs(), engine.Events(), queued->Task));
    Core::IO::FileIOBackend backend;
    ECS::Scene::Registry loaded;
    const auto result = Runtime::LoadSceneDocument(loaded, file.Path.string(), backend);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Stats.AttributeBindingEntities, 1u);
    const auto bound = loaded.Raw().view<const Runtime::VertexChannelBindingSet, const ECSC::GeometrySources::Vertices>();
    ASSERT_NE(bound.begin(), bound.end());
    for (const auto loadedEntity : bound)
    {
        EXPECT_TRUE(loaded.Raw().all_of<Extrinsic::Graphics::Components::RenderBounds>(loadedEntity));
        EXPECT_EQ(bound.get<const Runtime::VertexChannelBindingSet>(loadedEntity).Normal, normal);
        const auto& restored = bound.get<const ECSC::GeometrySources::Vertices>(loadedEntity).Properties;
        const auto values = restored.Get<glm::vec3>("v:custom_normal");
        ASSERT_TRUE(values);
        EXPECT_EQ(values.Vector(), std::vector<glm::vec3>(2u, glm::vec3{0.0f, 0.0f, 1.0f}));
    }
    engine.Shutdown();
}

TEST(SceneDocumentModule, QueuedSavePreservesSiblingOrder)
{
    TempSceneFile file("runtime-queued-save-siblings.scene.json", "");
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::make_unique<StubApplication>());
    auto& documents = engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.Initialize();
    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const auto create = [&](const std::string& name) {
        const auto entity = scene.Create();
        scene.Raw().emplace<ECSC::MetaData>(entity, ECSC::MetaData{.EntityName = name});
        return entity;
    };
    const auto parent = create("Parent");
    const auto a = create("A");
    const auto b = create("B");
    const auto c = create("C");
    ECS::Hierarchy::Attach(scene.Raw(), a, parent);
    ECS::Hierarchy::Attach(scene.Raw(), c, parent);
    ECS::Hierarchy::Attach(scene.Raw(), b, parent);
    const auto save = documents.QueueSceneSaveToPath(file.Path.string());
    ASSERT_TRUE(save.has_value());
    ASSERT_TRUE(DrainUntilTerminal(engine.Jobs(), engine.Events(), save->Task));
    ASSERT_EQ(engine.Jobs().GetState(save->Task), Runtime::JobState::Published);
    ECS::Scene::Registry loaded;
    Core::IO::FileIOBackend backend;
    ASSERT_TRUE(Runtime::LoadSceneDocument(loaded, file.Path.string(), backend).has_value());
    auto loadedParent = ECS::InvalidEntityHandle;
    loaded.Raw().view<const ECSC::MetaData>().each([&](const ECS::EntityHandle entity, const ECSC::MetaData& metadata) {
        if (metadata.EntityName == "Parent") loadedParent = entity;
    });
    ASSERT_NE(loadedParent, ECS::InvalidEntityHandle);
    const auto& hierarchy = loaded.Raw().get<ECSC::Hierarchy::Component>(loadedParent);
    EXPECT_EQ(hierarchy.ChildCount, 3u);
    auto child = hierarchy.FirstChild;
    auto previous = ECS::InvalidEntityHandle;
    for (const std::string_view name : {"B", "C", "A"})
    {
        ASSERT_TRUE(loaded.IsValid(child));
        EXPECT_EQ(loaded.Raw().get<ECSC::MetaData>(child).EntityName, name);
        const auto& link = loaded.Raw().get<ECSC::Hierarchy::Component>(child);
        EXPECT_EQ(link.Parent, loadedParent);
        EXPECT_EQ(link.PrevSibling, previous);
        previous = child;
        child = link.NextSibling;
    }
    EXPECT_EQ(child, ECS::InvalidEntityHandle);
    engine.Shutdown();
}

TEST(SceneDocumentModule, FailedNativeSavePreservesExistingFile)
{
#if defined(__unix__) || defined(__APPLE__)
    const auto directory = std::filesystem::temp_directory_path() /
        ("runtime-native-save-" + std::to_string(::getpid()));
    ASSERT_TRUE(std::filesystem::create_directory(directory));
    const std::string sentinel = "previous complete scene document";
    TempSceneFile file((directory / "scene.json").string(), sentinel);
    EXPECT_EXIT({
        ModuleHarness harness;
        if (!harness.Start()) ::_exit(1);
        auto* scene = harness.Worlds.Get(harness.World);
        const auto entity = scene->Create();
        scene->Raw().emplace<ECSC::MetaData>(entity, ECSC::MetaData{.EntityName = std::string(16384u, 'x')});
        auto* history = harness.Services.Find<Runtime::EditorCommandHistory>();
        if (!history) ::_exit(2);
        (void)history->MarkDirty("unsaved scene");
        struct rlimit limit{};
        if (::getrlimit(RLIMIT_FSIZE, &limit) != 0) ::_exit(3);
        limit.rlim_cur = 1024u;
        if (std::signal(SIGXFSZ, SIG_IGN) == SIG_ERR || ::setrlimit(RLIMIT_FSIZE, &limit) != 0) ::_exit(4);
        const auto saved = harness.Module->SaveSceneToPath(file.Path.string());
        ::_exit(!saved && saved.error() == Core::ErrorCode::FileWriteError && history->IsDirty() ? 0 : 5);
    }, ::testing::ExitedWithCode(0), "");
    std::ifstream input(file.Path, std::ios::binary);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()), sentinel);
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        EXPECT_EQ(entry.path(), file.Path) << "failed saves must remove their sibling temporary file";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
#else
    GTEST_SKIP() << "deterministic native write failure requires RLIMIT_FSIZE";
#endif
}

TEST(SceneDocumentModule, QueuedSavesPublishInSubmissionOrder)
{
    TempSceneFile file("runtime-save-order.scene.json", "");
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.EmplaceModule<Runtime::AsyncWorkModule>();
    auto& documents = engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.Initialize();
    auto* history = engine.Services().Find<Runtime::EditorCommandHistory>();
    ASSERT_NE(history, nullptr);
    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const auto entity = scene.Create();
    auto& metadata = scene.Raw().emplace<ECSC::MetaData>(entity, ECSC::MetaData{.EntityName = "first"});
    (void)history->MarkDirty("first");
    const auto first = documents.QueueSceneSaveToPath(file.Path.string());
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(WaitUntilAwaitingGate(engine.Jobs(), first->Task));
    metadata.EntityName = "second";
    (void)history->MarkDirty("second");
    const auto secondRevision = history->Snapshot().Revision;
    const auto second = documents.QueueSceneSaveToPath(file.Path.string());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(engine.Jobs().GetState(second->Task), Runtime::JobState::AwaitingDependencies);
    ASSERT_TRUE(DrainUntilTerminal(engine.Jobs(), engine.Events(), first->Task));
    ASSERT_TRUE(DrainUntilTerminal(engine.Jobs(), engine.Events(), second->Task));
    EXPECT_EQ(engine.Jobs().GetState(second->Task), Runtime::JobState::Published);
    EXPECT_EQ(history->Snapshot().SavedRevision, secondRevision);
    EXPECT_FALSE(history->IsDirty());
    Core::IO::FileIOBackend backend;
    ECS::Scene::Registry loaded;
    ASSERT_TRUE(Runtime::LoadSceneDocument(loaded, file.Path.string(), backend).has_value());
    EXPECT_TRUE(ContainsNamedEntity(loaded, "second"));
    EXPECT_FALSE(ContainsNamedEntity(loaded, "first"));
    engine.Shutdown();
}

TEST(SceneDocumentModule, SynchronousSaveRejectsEveryPathUntilQueuedSaveSettles)
{
    TempSceneFile queuedFile("runtime-sync-busy-queued.scene.json", "queued destination");
    TempSceneFile otherFile("runtime-sync-busy-other.scene.json", "other destination");
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.EmplaceModule<Runtime::AsyncWorkModule>();
    auto& documents = engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.Initialize();
    auto* history = engine.Services().Find<Runtime::EditorCommandHistory>();
    ASSERT_NE(history, nullptr);
    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const auto entity = scene.Create();
    auto& metadata = scene.Raw().emplace<ECSC::MetaData>(entity, ECSC::MetaData{.EntityName = "queued"});
    (void)history->MarkDirty("queued snapshot");
    const auto queued = documents.QueueSceneSaveToPath(queuedFile.Path.string());
    ASSERT_TRUE(queued.has_value());
    ASSERT_TRUE(WaitUntilAwaitingGate(engine.Jobs(), queued->Task));
    metadata.EntityName = "later edit";
    (void)history->MarkDirty("later edit");
    const auto before = history->Snapshot();
    ASSERT_TRUE(before.Dirty);
    const auto readBytes = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };
    for (const auto* file : {&queuedFile, &otherFile})
    {
        SCOPED_TRACE(file->Path.string());
        const auto bytes = readBytes(file->Path);
        ASSERT_FALSE(bytes.empty());
        const auto rejected = documents.SaveSceneToPath(file->Path.string());
        EXPECT_FALSE(rejected.has_value());
        if (!rejected) EXPECT_EQ(rejected.error(), Core::ErrorCode::InvalidState);
        EXPECT_EQ(readBytes(file->Path), bytes);
        const auto after = history->Snapshot();
        EXPECT_EQ(after.Revision, before.Revision);
        EXPECT_EQ(after.SavedRevision, before.SavedRevision);
        EXPECT_EQ(after.Dirty, before.Dirty);
        EXPECT_EQ(after.HasActivePath, before.HasActivePath);
        EXPECT_EQ(after.ActivePath, before.ActivePath);
        EXPECT_EQ(after.UndoCount, before.UndoCount);
        EXPECT_EQ(after.RedoCount, before.RedoCount);
        EXPECT_FALSE(documents.GetLastSceneFileEvent().has_value());
    }
    ASSERT_TRUE(DrainUntilTerminal(engine.Jobs(), engine.Events(), queued->Task));
    ASSERT_EQ(engine.Jobs().GetState(queued->Task), Runtime::JobState::Published);
    EXPECT_TRUE(history->IsDirty());
    ASSERT_TRUE(documents.SaveSceneToPath(otherFile.Path.string()).has_value());
    EXPECT_FALSE(history->IsDirty());
    EXPECT_EQ(history->Snapshot().SavedRevision, before.Revision);
    EXPECT_EQ(history->Snapshot().ActivePath, otherFile.Path.string());
    Core::IO::FileIOBackend backend;
    ECS::Scene::Registry loaded;
    ASSERT_TRUE(Runtime::LoadSceneDocument(loaded, otherFile.Path.string(), backend).has_value());
    EXPECT_TRUE(ContainsNamedEntity(loaded, "later edit"));
    EXPECT_FALSE(ContainsNamedEntity(loaded, "queued"));
    engine.Shutdown();
}

TEST(SceneDocumentModule, CancelledSaveChainAllowsAFreshSave)
{
    TempSceneFile file("runtime-save-cancel-chain.scene.json", "");
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.EmplaceModule<Runtime::AsyncWorkModule>();
    auto& documents = engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.Initialize();
    const auto first = documents.QueueSceneSaveToPath(file.Path.string());
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(WaitUntilAwaitingGate(engine.Jobs(), first->Task));
    const auto second = documents.QueueSceneSaveToPath(file.Path.string());
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(engine.Jobs().Cancel(first->Task));
    ASSERT_TRUE(DrainUntilTerminal(engine.Jobs(), engine.Events(), second->Task));
    EXPECT_EQ(engine.Jobs().GetState(second->Task), Runtime::JobState::Cancelled);
    const auto fresh = documents.QueueSceneSaveToPath(file.Path.string());
    ASSERT_TRUE(fresh.has_value());
    ASSERT_TRUE(DrainUntilTerminal(engine.Jobs(), engine.Events(), fresh->Task));
    EXPECT_EQ(engine.Jobs().GetState(fresh->Task), Runtime::JobState::Published);
    engine.Shutdown();
}

TEST(SceneDocumentModule, ReplacementRetainsUnfinishedSaveDependencyUntilFinalization)
{
    for (const bool switchWorld : {false, true})
    {
        SCOPED_TRACE(switchWorld ? "active world rebind" : "same-world document replacement");
        TempSceneFile file("runtime-save-replacement-chain.scene.json", "");
        Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(), std::make_unique<StubApplication>());
        engine.EmplaceModule<Runtime::AsyncWorkModule>();
        auto& documents = engine.EmplaceModule<Runtime::SceneDocumentModule>();
        engine.Initialize();
        auto* history = engine.Services().Find<Runtime::EditorCommandHistory>();
        ASSERT_NE(history, nullptr);
        const auto first = documents.QueueSceneSaveToPath(file.Path.string());
        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(WaitUntilAwaitingGate(engine.Jobs(), first->Task));
        if (switchWorld)
        {
            const auto replacement = engine.Worlds().CreateWorld("replacement");
            ASSERT_TRUE(engine.Worlds().RequestSetActiveWorld(replacement).has_value());
            ASSERT_EQ(engine.Worlds().ApplyMaintenance(engine.Events(), engine.Jobs()).AppliedActiveWorldChanges, 1u);
            (void)documents.GetLastSceneFileEvent();
        }
        else
            ASSERT_TRUE(documents.NewSceneDocument().has_value());
        (void)history->MarkDirty("replacement contents");
        const auto second = documents.QueueSceneSaveToPath(file.Path.string());
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(engine.Jobs().GetState(second->Task), Runtime::JobState::AwaitingDependencies);
        ASSERT_TRUE(DrainUntilTerminal(engine.Jobs(), engine.Events(), first->Task));
        ASSERT_TRUE(DrainUntilTerminal(engine.Jobs(), engine.Events(), second->Task));
        EXPECT_EQ(engine.Jobs().GetState(second->Task), Runtime::JobState::Cancelled);
        ASSERT_TRUE(documents.GetLastSceneFileEvent());
        EXPECT_EQ(documents.GetLastSceneFileEvent()->Task, second->Task);
        EXPECT_EQ(documents.GetLastSceneFileEvent()->Error, Core::ErrorCode::InvalidState);
        EXPECT_TRUE(history->IsDirty());
        const auto fresh = documents.QueueSceneSaveToPath(file.Path.string());
        ASSERT_TRUE(fresh.has_value());
        ASSERT_TRUE(DrainUntilTerminal(engine.Jobs(), engine.Events(), fresh->Task));
        EXPECT_EQ(engine.Jobs().GetState(fresh->Task), Runtime::JobState::Published);
        EXPECT_FALSE(history->IsDirty());
        engine.Shutdown();
    }
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
