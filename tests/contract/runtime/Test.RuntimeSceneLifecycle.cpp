#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <thread>
#include <utility>
#include <variant>

#include <gtest/gtest.h>

import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.FrameLoop;
import Extrinsic.Core.IOBackend;
import Extrinsic.Core.Tasks;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SceneInteractionModule;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.StreamingExecutor;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

namespace Core = Extrinsic::Core;
namespace Runtime = Extrinsic::Runtime;
namespace ECS = Extrinsic::ECS;
namespace ECSC = Extrinsic::ECS::Components;
namespace G = Extrinsic::Graphics::Components;
namespace Sel = Extrinsic::ECS::Components::Selection;

namespace
{
    class StubApplication final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(Runtime::Engine&, double, double) override {}
        void OnShutdown(Runtime::Engine&) override {}
    };

    class DerivedJobFrameApplication final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine& engine) override
        {
            Jobs = engine.Services().Find<Runtime::DerivedJobRegistry>();
            ASSERT_NE(Jobs, nullptr);

            Runtime::DerivedJobDesc desc{
                .Key = Runtime::DerivedJobKey{
                    .EntityId = 77u,
                    .Domain = Runtime::ProgressiveGeometryDomain::Point,
                    .OutputSemantic = Runtime::ProgressiveSlotSemantic::PointColor,
                    .OutputName = "engine_frame_probe",
                },
                .Name = "Engine.Frame.DerivedJob",
                .Kind = Runtime::RuntimeTaskKinds::GeometryProcess,
                .Priority = Core::Dag::TaskPriority::Normal,
                .Scope = engine.ActiveWorld(),
                .Execute =
                    [this]() -> Runtime::DerivedJobWorkerResult
                    {
                        WorkerRuns.fetch_add(1u, std::memory_order_relaxed);
                        return Runtime::DerivedJobOutput{
                            .PayloadToken = 42u,
                            .NormalizedProgress = 1.0f,
                            .ProgressDeterminate = true,
                            .Diagnostic = "ready",
                        };
                    },
                .ApplyOnMainThread =
                    [this](Runtime::DerivedJobApplyContext& context)
                        -> Core::Result
                    {
                        EXPECT_EQ(context.Output.PayloadToken, 42u);
                        ApplyRuns.fetch_add(1u, std::memory_order_relaxed);
                        return Core::Ok();
                    },
            };
            Handle = Jobs->Submit(std::move(desc));
        }

        void OnSimTick(Runtime::Engine&, double) override {}

        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            ++Frames;
            if (ApplyRuns.load(std::memory_order_relaxed) > 0u || Frames >= 8u)
                engine.RequestExit();
        }

        void OnShutdown(Runtime::Engine&) override {}

        Runtime::DerivedJobHandle Handle{};
        Runtime::DerivedJobRegistry* Jobs{};
        std::atomic<std::uint32_t> WorkerRuns{0u};
        std::atomic<std::uint32_t> ApplyRuns{0u};
        std::uint32_t Frames{0u};
    };

    class WaitForConditionApplication final : public Runtime::IApplication
    {
    public:
        using Clock = std::chrono::steady_clock;
        using Predicate = std::function<bool(Runtime::Engine&)>;

        explicit WaitForConditionApplication(Predicate predicate,
                                             std::chrono::milliseconds timeout =
                                                 std::chrono::seconds(10),
                                             std::chrono::milliseconds pollDelay =
                                                 std::chrono::milliseconds(1))
            : m_Predicate(std::move(predicate))
            , m_Timeout(timeout)
            , m_PollDelay(pollDelay)
        {
        }

        void OnInitialize(Runtime::Engine&) override {}
        void OnSimTick(Runtime::Engine&, double) override {}
        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            const Clock::time_point now = Clock::now();
            if (!m_Started)
            {
                m_Started = true;
                m_StartedAt = now;
                m_Deadline = now + m_Timeout;
            }

            ++m_Frames;
            if (m_Predicate && m_Predicate(engine))
            {
                m_Satisfied = true;
                m_Elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - m_StartedAt);
                engine.RequestExit();
                return;
            }

            if (now >= m_Deadline)
            {
                m_TimedOut = true;
                m_Elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - m_StartedAt);
                engine.RequestExit();
                return;
            }

            std::this_thread::sleep_for(m_PollDelay);
        }
        void OnShutdown(Runtime::Engine&) override {}

        std::uint32_t Frames() const noexcept { return m_Frames; }
        bool ConditionSatisfied() const noexcept { return m_Satisfied; }
        bool TimedOut() const noexcept { return m_TimedOut; }
        std::chrono::milliseconds Elapsed() const noexcept { return m_Elapsed; }

    private:
        Predicate m_Predicate{};
        std::chrono::milliseconds m_Timeout{};
        std::chrono::milliseconds m_PollDelay{};
        Clock::time_point m_StartedAt{};
        Clock::time_point m_Deadline{};
        std::chrono::milliseconds m_Elapsed{};
        std::uint32_t m_Frames{0u};
        bool m_Started{false};
        bool m_Satisfied{false};
        bool m_TimedOut{false};
    };

    class TempSceneFile final
    {
    public:
        TempSceneFile(std::string_view name, std::string_view contents)
            : Path(std::filesystem::temp_directory_path() / name)
        {
            std::ofstream out(Path, std::ios::binary | std::ios::trunc);
            out << contents;
        }

        ~TempSceneFile()
        {
            std::error_code ignored{};
            std::filesystem::remove(Path, ignored);
        }

        std::filesystem::path Path;
    };

    [[nodiscard]] Extrinsic::Core::Config::EngineConfig HeadlessConfig()
    {
        Extrinsic::Core::Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        return config;
    }

    [[nodiscard]] Extrinsic::Core::Config::EngineConfig NullWindowHeadlessConfig()
    {
        Extrinsic::Core::Config::EngineConfig config = HeadlessConfig();
        config.Window.Backend = Extrinsic::Core::Config::WindowBackend::Null;
        return config;
    }

    [[nodiscard]] bool SceneContainsNamedEntity(
        const ECS::Scene::Registry& scene,
        const std::string_view name)
    {
        bool found = false;
        scene.Raw().view<const ECSC::MetaData>().each(
            [&](const ECS::EntityHandle, const ECSC::MetaData& meta)
            {
                if (meta.EntityName == name)
                    found = true;
            });
        return found;
    }
}

TEST(RuntimeDerivedJobEngineWiring, RunFrameAppliesSubmittedDerivedJob)
{
    auto application = std::make_unique<DerivedJobFrameApplication>();
    DerivedJobFrameApplication* app = application.get();
    Runtime::Engine engine(NullWindowHeadlessConfig(), std::move(application));
    engine.EmplaceModule<Runtime::AsyncWorkModule>();
    engine.Initialize();

    EXPECT_TRUE(app->Handle.IsValid());
    EXPECT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable";

    engine.Run();

    ASSERT_NE(app->Jobs, nullptr);
    const Runtime::DerivedJobQueueSnapshot snapshot = app->Jobs->SnapshotAll();
    ASSERT_EQ(snapshot.Entries.size(), 1u);
    EXPECT_EQ(snapshot.Entries[0].Status, Runtime::DerivedJobStatus::Complete);
    EXPECT_EQ(app->WorkerRuns.load(std::memory_order_relaxed), 1u);
    EXPECT_EQ(app->ApplyRuns.load(std::memory_order_relaxed), 1u);
    EXPECT_GT(snapshot.Diagnostics.ApplyMainThreadCalls, 0u);

    engine.Shutdown();
}

TEST(RuntimeRenderExtraction, VisualizationAdapterBindingRevisionTracksMutations)
{
    Runtime::RenderExtractionCache cache{};

    EXPECT_EQ(cache.GetVisualizationAdapterBindingRevision(), 0u);

    cache.ClearVisualizationAdapterBinding(7u);
    EXPECT_EQ(cache.GetVisualizationAdapterBindingRevision(), 0u);

    cache.SetVisualizationAdapterBinding(
        7u,
        Runtime::RenderExtractionCache::VisualizationAdapterBinding{
            .AdapterKey = 0xA11CEu,
            .BufferBDA = 0xCAFE1000u,
        });
    EXPECT_EQ(cache.GetVisualizationAdapterBindingRevision(), 1u);

    cache.SetVisualizationAdapterBinding(
        7u,
        Runtime::RenderExtractionCache::VisualizationAdapterBinding{
            .AdapterKey = 0xB0Bu,
            .BufferBDA = 0xCAFE2000u,
        });
    EXPECT_EQ(cache.GetVisualizationAdapterBindingRevision(), 2u);

    cache.ClearVisualizationAdapterBinding(9u);
    EXPECT_EQ(cache.GetVisualizationAdapterBindingRevision(), 2u);

    cache.ClearVisualizationAdapterBinding(7u);
    EXPECT_EQ(cache.GetVisualizationAdapterBindingRevision(), 3u);
}

TEST(RuntimeSceneLifecycle, AsyncWaitAllowsCompletionBeyondLegacyFrameBudget)
{
    std::uint32_t predicateCalls = 0u;
    auto application = std::make_unique<WaitForConditionApplication>(
        [&predicateCalls](Runtime::Engine&)
        {
            ++predicateCalls;
            return predicateCalls > 256u;
        },
        std::chrono::seconds(10));
    WaitForConditionApplication* app = application.get();
    Runtime::Engine engine(NullWindowHeadlessConfig(), std::move(application));
    engine.Initialize();
    engine.Run();

    EXPECT_TRUE(app->ConditionSatisfied());
    EXPECT_FALSE(app->TimedOut());
    EXPECT_GE(app->Elapsed(), std::chrono::milliseconds(200));
    EXPECT_GT(app->Frames(), 256u);
    EXPECT_EQ(app->Frames(), predicateCalls);

    engine.Shutdown();
}

TEST(RuntimeSceneLifecycle, AsyncWaitReportsElapsedTimeout)
{
    auto application = std::make_unique<WaitForConditionApplication>(
        [](Runtime::Engine&) { return false; },
        std::chrono::milliseconds(5));
    WaitForConditionApplication* app = application.get();
    Runtime::Engine engine(NullWindowHeadlessConfig(), std::move(application));
    engine.Initialize();
    engine.Run();

    EXPECT_FALSE(app->ConditionSatisfied());
    EXPECT_TRUE(app->TimedOut());
    EXPECT_GE(app->Elapsed(), std::chrono::milliseconds(5));
    EXPECT_GT(app->Frames(), 1u);

    engine.Shutdown();
}

TEST(RuntimeSceneLifecycle, EngineRunsWithoutOptionalAsyncWorkModule)
{
    auto application = std::make_unique<WaitForConditionApplication>(
        [](Runtime::Engine&) { return true; });
    WaitForConditionApplication* app = application.get();
    Runtime::Engine engine(NullWindowHeadlessConfig(), std::move(application));
    engine.Initialize();

    EXPECT_EQ(engine.Services().Find<Runtime::StreamingExecutor>(), nullptr);
    EXPECT_EQ(engine.Services().Find<Runtime::DerivedJobRegistry>(), nullptr);
    EXPECT_EQ(engine.Services().Find<Core::IStreamingFrameHooks>(), nullptr);

    engine.Run();
    EXPECT_TRUE(app->ConditionSatisfied());
    EXPECT_FALSE(app->TimedOut());

    engine.Shutdown();
}

TEST(RuntimeSceneLifecycle, NewSceneDocumentClearsSceneSelectionAndExtractionSidecars)
{
    Runtime::Engine engine(HeadlessConfig(), std::make_unique<StubApplication>());
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.EmplaceModule<Runtime::SceneInteractionModule>();
    engine.Initialize();
    Runtime::SelectionController& selection =
        *engine.Services().Find<Runtime::SelectionController>();

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const ECS::EntityHandle entity = scene.Create();
    scene.Raw().emplace<Sel::SelectableTag>(entity);
    const std::uint32_t stableId = Runtime::SelectionController::ToStableEntityId(entity);

    selection.SetSelectedEntity(scene, entity);
    selection.RequestHoverPick(4u, 5u);
    ASSERT_TRUE(selection.ConsumePendingPick().has_value());
    selection.ConsumeHit(scene, stableId);
    selection.RequestClickPick(6u, 7u);
    ASSERT_TRUE(selection.ConsumePendingPick().has_value());
    ASSERT_EQ(selection.InFlightPickCount(), 1u);

    scene.Raw().emplace<G::RenderEdges>(entity);
    scene.Raw().emplace<G::RenderPoints>(
        entity,
        G::RenderPoints{
            .Type = G::RenderPoints::RenderType::Surfel,
            .SizeSource = 4.0f,
        });
    engine.SetVisualizationAdapterBinding(
        stableId,
        Runtime::RenderExtractionCache::VisualizationAdapterBinding{
            .AdapterKey = 0x5CE11u,
            .BufferBDA = 0xCAFE1000u,
        });
    const std::uint64_t bindingRevisionBeforeReset =
        engine.GetVisualizationAdapterBindingRevision();
    EXPECT_EQ(bindingRevisionBeforeReset, 1u);
    ASSERT_TRUE(scene.Raw().all_of<G::RenderEdges>(entity));
    ASSERT_TRUE(scene.Raw().all_of<G::RenderPoints>(entity));
    const G::RenderPoints& translatedPoints =
        scene.Raw().get<G::RenderPoints>(entity);
    EXPECT_EQ(translatedPoints.Type, G::RenderPoints::RenderType::Surfel);
    ASSERT_TRUE(std::holds_alternative<float>(translatedPoints.SizeSource));
    EXPECT_FLOAT_EQ(std::get<float>(translatedPoints.SizeSource), 4.0f);
    ASSERT_TRUE(engine.GetVisualizationAdapterBinding(stableId).has_value());

    const auto reset = engine.Services().Find<Runtime::SceneDocumentModule>()->NewSceneDocument();
    ASSERT_TRUE(reset.has_value()) << static_cast<int>(reset.error());

    EXPECT_FALSE(scene.Raw().valid(entity));
    EXPECT_EQ(selection.SelectedCount(), 0u);
    EXPECT_FALSE(selection.HasHovered());
    EXPECT_FALSE(selection.HasPendingPick());
    EXPECT_EQ(selection.InFlightPickCount(), 0u);
    EXPECT_FALSE(engine.GetVisualizationAdapterBinding(stableId).has_value());
    EXPECT_GT(engine.GetVisualizationAdapterBindingRevision(),
              bindingRevisionBeforeReset);

    engine.Shutdown();
}

TEST(RuntimeSceneLifecycle, QueuedSceneSaveWritesSnapshotAndMarksHistoryOnCompletion)
{
    TempSceneFile savedScene("runtime142_async_saved_scene.json", "");
    auto application = std::make_unique<WaitForConditionApplication>(
        [](Runtime::Engine& runningEngine)
        {
            const std::optional<Runtime::RuntimeSceneFileEvent>& event =
                runningEngine.Services().Find<Runtime::SceneDocumentModule>()->GetLastSceneFileEvent();
            return event.has_value() &&
                   event->Operation == Runtime::RuntimeSceneFileOperation::Save;
        });
    WaitForConditionApplication* app = application.get();
    Runtime::Engine engine(NullWindowHeadlessConfig(), std::move(application));
    engine.EmplaceModule<Runtime::AsyncWorkModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.Initialize();

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const ECS::EntityHandle saved = scene.Create();
    scene.Raw().emplace<ECSC::MetaData>(
        saved,
        ECSC::MetaData{.EntityName = "Saved Scene Entity"});
    scene.Raw().emplace<Sel::SelectableTag>(saved);
    const Runtime::EditorCommandHistoryResult dirty =
        engine.Services().Find<Runtime::EditorCommandHistory>()->MarkDirty("Edit Scene");
    EXPECT_TRUE(dirty.Succeeded());
    EXPECT_TRUE(engine.Services().Find<Runtime::EditorCommandHistory>()->Snapshot().Dirty);

    auto queued =
        engine.Services().Find<Runtime::SceneDocumentModule>()->QueueSceneSaveToPath(savedScene.Path.string());
    ASSERT_TRUE(queued.has_value()) << static_cast<int>(queued.error());
    EXPECT_TRUE(queued->Task.IsValid());
    EXPECT_EQ(queued->Operation, Runtime::RuntimeSceneFileOperation::Save);
    EXPECT_FALSE(engine.Services().Find<Runtime::SceneDocumentModule>()->GetLastSceneFileEvent().has_value());

    scene.Raw().get<ECSC::MetaData>(saved).EntityName = "Mutated After Queue";
    const ECS::EntityHandle late = scene.Create();
    scene.Raw().emplace<ECSC::MetaData>(
        late,
        ECSC::MetaData{.EntityName = "Late Scene Entity"});

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable";
    engine.Run();

    ASSERT_TRUE(app->ConditionSatisfied())
        << "scene save timed out after " << app->Elapsed().count()
        << " ms and " << app->Frames() << " frames";
    EXPECT_FALSE(app->TimedOut());
    const std::optional<Runtime::RuntimeSceneFileEvent>& event =
        engine.Services().Find<Runtime::SceneDocumentModule>()->GetLastSceneFileEvent();
    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(event->Operation, Runtime::RuntimeSceneFileOperation::Save);
    EXPECT_EQ(event->Task, queued->Task);
    EXPECT_EQ(event->Path, savedScene.Path.string());
    EXPECT_TRUE(event->Succeeded());
    ASSERT_TRUE(event->SaveResult.has_value());
    EXPECT_EQ(event->SaveResult->Stats.Entities, 1u);
    EXPECT_EQ(event->SaveResult->Stats.SelectableEntities, 1u);

    const Runtime::EditorCommandHistorySnapshot history =
        engine.Services().Find<Runtime::EditorCommandHistory>()->Snapshot();
    EXPECT_TRUE(history.HasActivePath);
    EXPECT_EQ(history.ActivePath, savedScene.Path.string());
    EXPECT_FALSE(history.Dirty);

    Core::IO::FileIOBackend backend;
    ECS::Scene::Registry loaded;
    auto loadedResult =
        Runtime::LoadSceneDocument(loaded, savedScene.Path.string(), backend);
    ASSERT_TRUE(loadedResult.has_value())
        << static_cast<int>(loadedResult.error());
    EXPECT_EQ(loadedResult->Stats.Entities, 1u);
    EXPECT_TRUE(SceneContainsNamedEntity(loaded, "Saved Scene Entity"));
    EXPECT_FALSE(SceneContainsNamedEntity(loaded, "Mutated After Queue"));
    EXPECT_FALSE(SceneContainsNamedEntity(loaded, "Late Scene Entity"));

    engine.Shutdown();
}

TEST(RuntimeSceneLifecycle, QueuedSceneLoadInvalidDocumentFailsClosed)
{
    TempSceneFile invalidScene("runtime142_invalid_scene.json", "not json");
    auto application = std::make_unique<WaitForConditionApplication>(
        [](Runtime::Engine& runningEngine)
        {
            return runningEngine.Services()
                .Find<Runtime::SceneDocumentModule>()
                ->GetLastSceneFileEvent()
                .has_value();
        });
    WaitForConditionApplication* app = application.get();
    Runtime::Engine engine(NullWindowHeadlessConfig(), std::move(application));
    engine.EmplaceModule<Runtime::AsyncWorkModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.EmplaceModule<Runtime::SceneInteractionModule>();
    engine.Initialize();
    Runtime::SelectionController& selection =
        *engine.Services().Find<Runtime::SelectionController>();

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const ECS::EntityHandle original = scene.Create();
    scene.Raw().emplace<Sel::SelectableTag>(original);
    selection.SetSelectedEntity(scene, original);

    auto queued =
        engine.Services().Find<Runtime::SceneDocumentModule>()->QueueSceneLoadFromPath(
            invalidScene.Path.string());
    ASSERT_TRUE(queued.has_value()) << static_cast<int>(queued.error());
    EXPECT_TRUE(queued->Task.IsValid());
    EXPECT_EQ(queued->Operation, Runtime::RuntimeSceneFileOperation::Load);
    EXPECT_FALSE(engine.Services().Find<Runtime::SceneDocumentModule>()->GetLastSceneFileEvent().has_value());

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable";
    engine.Run();

    ASSERT_TRUE(app->ConditionSatisfied())
        << "invalid scene load timed out after " << app->Elapsed().count()
        << " ms and " << app->Frames() << " frames";
    EXPECT_FALSE(app->TimedOut());
    const std::optional<Runtime::RuntimeSceneFileEvent>& event =
        engine.Services().Find<Runtime::SceneDocumentModule>()->GetLastSceneFileEvent();
    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(event->Operation, Runtime::RuntimeSceneFileOperation::Load);
    EXPECT_EQ(event->Task, queued->Task);
    EXPECT_EQ(event->Path, invalidScene.Path.string());
    EXPECT_FALSE(event->Succeeded());
    EXPECT_EQ(event->Error, Core::ErrorCode::InvalidFormat);
    EXPECT_FALSE(event->LoadResult.has_value());

    EXPECT_TRUE(scene.Raw().valid(original));
    EXPECT_EQ(selection.SelectedCount(), 1u);

    engine.Shutdown();
}

TEST(RuntimeSceneLifecycle, QueuedSceneLoadAppliesParsedSceneOnMainThread)
{
    TempSceneFile validScene(
        "runtime142_valid_scene.json",
        R"({"version":1,"entities":[{"id":0,"name":"Loaded Scene Entity"}]})");
    auto application = std::make_unique<WaitForConditionApplication>(
        [](Runtime::Engine& runningEngine)
        {
            const std::optional<Runtime::RuntimeSceneFileEvent>& event =
                runningEngine.Services().Find<Runtime::SceneDocumentModule>()->GetLastSceneFileEvent();
            return event.has_value() &&
                   event->Operation == Runtime::RuntimeSceneFileOperation::Load;
        });
    WaitForConditionApplication* app = application.get();
    Runtime::Engine engine(NullWindowHeadlessConfig(), std::move(application));
    engine.EmplaceModule<Runtime::AsyncWorkModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.EmplaceModule<Runtime::SceneInteractionModule>();
    engine.Initialize();
    Runtime::SelectionController& selection =
        *engine.Services().Find<Runtime::SelectionController>();

    auto& scene = *engine.Worlds().Get(engine.ActiveWorld());
    const ECS::EntityHandle original = scene.Create();
    scene.Raw().emplace<ECSC::MetaData>(
        original,
        ECSC::MetaData{.EntityName = "Original Scene Entity"});
    scene.Raw().emplace<Sel::SelectableTag>(original);
    selection.SetSelectedEntity(scene, original);

    auto queued =
        engine.Services().Find<Runtime::SceneDocumentModule>()->QueueSceneLoadFromPath(
            validScene.Path.string());
    ASSERT_TRUE(queued.has_value()) << static_cast<int>(queued.error());
    EXPECT_TRUE(queued->Task.IsValid());
    EXPECT_FALSE(engine.Services().Find<Runtime::SceneDocumentModule>()->GetLastSceneFileEvent().has_value());

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable";
    engine.Run();

    ASSERT_TRUE(app->ConditionSatisfied())
        << "scene load timed out after " << app->Elapsed().count()
        << " ms and " << app->Frames() << " frames";
    EXPECT_FALSE(app->TimedOut());
    const std::optional<Runtime::RuntimeSceneFileEvent>& event =
        engine.Services().Find<Runtime::SceneDocumentModule>()->GetLastSceneFileEvent();
    ASSERT_TRUE(event.has_value());
    EXPECT_TRUE(event->Succeeded());
    ASSERT_TRUE(event->LoadResult.has_value());
    EXPECT_EQ(event->LoadResult->Stats.Entities, 1u);
    EXPECT_EQ(event->Task, queued->Task);
    EXPECT_TRUE(SceneContainsNamedEntity(scene, "Loaded Scene Entity"));
    EXPECT_FALSE(SceneContainsNamedEntity(scene, "Original Scene Entity"));
    EXPECT_EQ(selection.SelectedCount(), 0u);

    engine.Shutdown();
}

TEST(RuntimeSceneLifecycle, QueuedSceneLoadRejectsActiveWorldSwitchBeforeApply)
{
    TempSceneFile validScene(
        "runtime179_world_scoped_scene_load.json",
        R"({"version":1,"entities":[{"id":0,"name":"Wrong World Load"}]})");
    Runtime::Engine engine(
        NullWindowHeadlessConfig(),
        std::make_unique<StubApplication>());
    engine.EmplaceModule<Runtime::AsyncWorkModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.Initialize();
    Runtime::SceneDocumentModule& document =
        *engine.Services().Find<Runtime::SceneDocumentModule>();
    Runtime::StreamingExecutor& streaming =
        *engine.Services().Find<Runtime::StreamingExecutor>();

    const Runtime::WorldHandle submissionWorld = engine.ActiveWorld();
    ECS::Scene::Registry* const submissionScene =
        engine.Worlds().Get(submissionWorld);
    ASSERT_NE(submissionScene, nullptr);
    const ECS::EntityHandle submissionMarker = submissionScene->Create();
    submissionScene->Raw().emplace<ECSC::MetaData>(
        submissionMarker,
        ECSC::MetaData{.EntityName = "Submission World Marker"});

    const Runtime::WorldHandle replacementWorld =
        engine.Worlds().CreateWorld("Replacement");
    ECS::Scene::Registry* const replacementScene =
        engine.Worlds().Get(replacementWorld);
    ASSERT_NE(replacementScene, nullptr);
    const ECS::EntityHandle replacementMarker = replacementScene->Create();
    replacementScene->Raw().emplace<ECSC::MetaData>(
        replacementMarker,
        ECSC::MetaData{.EntityName = "Replacement World Marker"});

    auto queued = document.QueueSceneLoadFromPath(
        validScene.Path.string());
    ASSERT_TRUE(queued.has_value()) << static_cast<int>(queued.error());
    streaming.PumpBackground(1u);
    if (Core::Tasks::Scheduler::IsInitialized())
        Core::Tasks::Scheduler::WaitForAll();
    streaming.DrainCompletions();
    ASSERT_EQ(
        streaming.GetState(queued->Task),
        Runtime::StreamingTaskState::WaitingForMainThreadApply);

    ASSERT_TRUE(
        engine.Worlds().RequestSetActiveWorld(replacementWorld).has_value());
    EXPECT_EQ(
        engine.Worlds()
            .ApplyMaintenance(engine.Events(), engine.Jobs())
            .AppliedActiveWorldChanges,
        1u);
    EXPECT_EQ(engine.ActiveWorld(), replacementWorld);
    ASSERT_EQ(engine.Worlds().Get(submissionWorld), submissionScene);
    ASSERT_EQ(engine.Worlds().Get(replacementWorld), replacementScene);

    // Direct validation resets the binding before the queued completion can
    // commit, even though ActiveWorldChanged is still waiting in the event
    // queue. The stale callback must not repopulate last-event state.
    EXPECT_FALSE(document.GetLastSceneFileEvent().has_value());
    streaming.ApplyMainThreadResults();
    EXPECT_FALSE(document.GetLastSceneFileEvent().has_value());

    EXPECT_TRUE(SceneContainsNamedEntity(
        *submissionScene,
        "Submission World Marker"));
    EXPECT_TRUE(SceneContainsNamedEntity(
        *replacementScene,
        "Replacement World Marker"));
    EXPECT_FALSE(SceneContainsNamedEntity(*submissionScene, "Wrong World Load"));
    EXPECT_FALSE(SceneContainsNamedEntity(*replacementScene, "Wrong World Load"));

    engine.Shutdown();
}

TEST(RuntimeSceneLifecycle, QueuedSceneLoadRejectsAwayAndBackBindingEpoch)
{
    TempSceneFile validScene(
        "runtime179_world_binding_epoch_scene_load.json",
        R"({"version":1,"entities":[{"id":0,"name":"Stale Epoch Load"}]})");

    Runtime::Engine engine(
        NullWindowHeadlessConfig(),
        std::make_unique<StubApplication>());
    engine.EmplaceModule<Runtime::AsyncWorkModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.Initialize();
    Runtime::SceneDocumentModule& document =
        *engine.Services().Find<Runtime::SceneDocumentModule>();
    Runtime::WorldRegistry& worlds = engine.Worlds();
    const Runtime::WorldHandle submissionWorld =
        engine.ActiveWorld();
    const Runtime::WorldHandle awayWorld = worlds.CreateWorld("Away");
    ECS::Scene::Registry* const activeScene =
        worlds.Get(submissionWorld);
    ASSERT_NE(activeScene, nullptr);
    Runtime::StreamingExecutor& streaming =
        *engine.Services().Find<Runtime::StreamingExecutor>();
    auto queued = document.QueueSceneLoadFromPath(validScene.Path.string());
    ASSERT_TRUE(queued.has_value()) << static_cast<int>(queued.error());

    streaming.PumpBackground(1u);
    if (Core::Tasks::Scheduler::IsInitialized())
        Core::Tasks::Scheduler::WaitForAll();
    streaming.DrainCompletions();
    ASSERT_EQ(
        streaming.GetState(queued->Task),
        Runtime::StreamingTaskState::WaitingForMainThreadApply);

    ASSERT_TRUE(worlds.RequestSetActiveWorld(awayWorld).has_value());
    EXPECT_EQ(
        worlds.ApplyMaintenance(engine.Events(), engine.Jobs())
            .AppliedActiveWorldChanges,
        1u);
    EXPECT_FALSE(document.GetLastSceneFileEvent().has_value());

    ASSERT_TRUE(worlds.RequestSetActiveWorld(submissionWorld).has_value());
    EXPECT_EQ(
        worlds.ApplyMaintenance(engine.Events(), engine.Jobs())
            .AppliedActiveWorldChanges,
        1u);
    EXPECT_FALSE(document.GetLastSceneFileEvent().has_value());

    streaming.ApplyMainThreadResults();
    const std::optional<Runtime::RuntimeSceneFileEvent>& event =
        document.GetLastSceneFileEvent();
    EXPECT_FALSE(event.has_value());
    EXPECT_FALSE(SceneContainsNamedEntity(*activeScene, "Stale Epoch Load"));
    engine.Shutdown();
}

TEST(RuntimeSceneLifecycle, RetiredQueuedSceneSavePublishesTerminalEvent)
{
    TempSceneFile savedScene(
        "runtime179_retired_scene_save.json",
        "");

    Runtime::Engine engine(
        NullWindowHeadlessConfig(),
        std::make_unique<StubApplication>());
    engine.EmplaceModule<Runtime::AsyncWorkModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.Initialize();
    Runtime::SceneDocumentModule& document =
        *engine.Services().Find<Runtime::SceneDocumentModule>();
    Runtime::StreamingExecutor& streaming =
        *engine.Services().Find<Runtime::StreamingExecutor>();
    const Runtime::WorldHandle world = engine.ActiveWorld();
    auto queued =
        document.QueueSceneSaveToPath(savedScene.Path.string());
    ASSERT_TRUE(queued.has_value()) << static_cast<int>(queued.error());
    ASSERT_FALSE(document.GetLastSceneFileEvent().has_value());

    EXPECT_EQ(streaming.RetireWorld(world), 1u);
    EXPECT_EQ(
        streaming.GetState(queued->Task),
        Runtime::StreamingTaskState::Cancelled);
    EXPECT_FALSE(document.GetLastSceneFileEvent().has_value());

    streaming.ApplyMainThreadResults();
    const std::optional<Runtime::RuntimeSceneFileEvent>& event =
        document.GetLastSceneFileEvent();
    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(event->Sequence, 1u);
    EXPECT_EQ(event->Operation, Runtime::RuntimeSceneFileOperation::Save);
    EXPECT_EQ(event->Task, queued->Task);
    EXPECT_EQ(event->Path, savedScene.Path.string());
    EXPECT_EQ(event->Error, Core::ErrorCode::InvalidState);
    EXPECT_FALSE(event->SaveResult.has_value());

    streaming.ApplyMainThreadResults();
    ASSERT_TRUE(document.GetLastSceneFileEvent().has_value());
    EXPECT_EQ(document.GetLastSceneFileEvent()->Sequence, 1u);
    engine.Shutdown();
}
