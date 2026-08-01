#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <glm/glm.hpp>

#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Tasks;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.PointCloudConsolidationConfig;
import Extrinsic.Runtime.PointCloudConsolidationModule;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SelectionController;
import Geometry.Properties;

namespace CoreConfig = Extrinsic::Core::Config;
namespace Dirty = Extrinsic::ECS::Components::DirtyTags;
namespace ECS = Extrinsic::ECS;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace Runtime = Extrinsic::Runtime;

namespace
{
    using namespace std::chrono_literals;

    [[nodiscard]] CoreConfig::EngineConfig HeadlessConfig(
        const unsigned workers = 2u)
    {
        CoreConfig::EngineConfig config{};
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend = CoreConfig::WindowBackend::Null;
        config.Simulation.WorkerThreadCount = workers;
        return config;
    }

    [[nodiscard]] std::vector<glm::vec3> NoisyPlane()
    {
        std::vector<glm::vec3> points{};
        for (int y = 0; y < 5; ++y)
        {
            for (int x = 0; x < 5; ++x)
            {
                const float noise =
                    ((x * 13 + y * 7) % 5 - 2) * 0.025f;
                points.emplace_back(
                    (static_cast<float>(x) - 2.0f) * 0.2f,
                    (static_cast<float>(y) - 2.0f) * 0.2f,
                    noise);
            }
        }
        return points;
    }

    [[nodiscard]] ECS::EntityHandle AddPointCloud(
        ECS::Scene::Registry& scene,
        const std::vector<glm::vec3>& positions)
    {
        const ECS::EntityHandle entity = scene.Create();
        auto& vertices = scene.Raw().emplace<GS::Vertices>(entity);
        vertices.Properties.Resize(positions.size());
        auto position = vertices.Properties.GetOrAdd<glm::vec3>(
            std::string{GS::PropertyNames::kPosition},
            glm::vec3{0.0f});
        position.Vector() = positions;
        auto provenance = vertices.Properties.GetOrAdd<std::uint32_t>(
            "p:source_index", 0u);
        for (std::size_t index = 0u; index < positions.size(); ++index)
            provenance[index] = static_cast<std::uint32_t>(index);
        return entity;
    }

    [[nodiscard]] Runtime::PointCloudConsolidationRequest MakeRequest(
        const ECS::EntityHandle entity)
    {
        return Runtime::PointCloudConsolidationRequest{
            .StableEntityId =
                Runtime::SelectionController::ToStableEntityId(entity),
            .Config = Runtime::PointCloudConsolidationConfig{
                .Strategy =
                    Runtime::PointCloudConsolidationStrategy::Wlop,
                .SupportRadius = 0.65,
                .RepulsionWeight = 0.0,
                .MaxIterations = 1u,
                .ConvergenceTolerance = 1.0,
                .TargetPointCount = 16u,
                .Seed = 17u,
                .NormalRefinementRounds = 1u,
            },
        };
    }

    class ConsolidationSuccessApp final
        : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override
        {
            auto& engine = Kernel();
            Service = engine.Services().Find<
                Runtime::PointCloudConsolidationService>();
            Scene = engine.Worlds().Get(engine.ActiveWorld());
            if (Service == nullptr || !Service->Available() || Scene == nullptr)
            {
                MissingService = true;
                engine.RequestExit();
                return;
            }
            Entity = AddPointCloud(*Scene, NoisyPlane());
            CompletionSubscription = Service->SubscribeCompleted(
                [this](const Runtime::PointCloudConsolidationResult& result)
                {
                    Completion = result;
                    CompletionThread = std::this_thread::get_id();
                });
            MainThread = std::this_thread::get_id();
            Correlation = Service->Run(MakeRequest(Entity));
        }

        void Frame(double, double) override
        {
            auto& engine = Kernel();
            ++Ticks;
            if (Completion.has_value())
            {
                Stats = Service->Stats();
                engine.RequestExit();
            }
            else if (Ticks > 240u)
            {
                TimedOut = true;
                engine.RequestExit();
            }
        }

        void Shutdown() override
        {
            if (Service != nullptr)
                Service->Unsubscribe(CompletionSubscription);
        }

        Runtime::PointCloudConsolidationService* Service{};
        ECS::Scene::Registry* Scene{};
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        Runtime::KernelEventSubscription CompletionSubscription{};
        Runtime::CommandCorrelationId Correlation{};
        Runtime::PointCloudConsolidationModuleStats Stats{};
        std::optional<Runtime::PointCloudConsolidationResult> Completion{};
        std::thread::id MainThread{};
        std::thread::id CompletionThread{};
        std::uint32_t Ticks{0u};
        bool MissingService{false};
        bool TimedOut{false};
    };

    class ConsolidationStaleSourceApp final
        : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override
        {
            auto& engine = Kernel();
            Service = engine.Services().Find<
                Runtime::PointCloudConsolidationService>();
            Scene = engine.Worlds().Get(engine.ActiveWorld());
            if (Service == nullptr || !Service->Available() || Scene == nullptr)
            {
                MissingService = true;
                engine.RequestExit();
                return;
            }
            Entity = AddPointCloud(*Scene, NoisyPlane());
            CompletionSubscription = Service->SubscribeCompleted(
                [this](const Runtime::PointCloudConsolidationResult& result)
                {
                    Completion = result;
                });

            Extrinsic::Core::Tasks::Scheduler::Dispatch(
                [this]
                {
                    BlockerStarted.store(true, std::memory_order_release);
                    while (!ReleaseBlocker.load(std::memory_order_acquire))
                        std::this_thread::sleep_for(1ms);
                });
            Correlation = Service->Run(MakeRequest(Entity));
        }

        void Frame(double, double) override
        {
            auto& engine = Kernel();
            ++Ticks;
            if (!Mutated &&
                BlockerStarted.load(std::memory_order_acquire))
            {
                const std::vector<Runtime::JobSnapshot> jobs =
                    engine.Jobs().SnapshotAll();
                const auto found = std::find_if(
                    jobs.begin(),
                    jobs.end(),
                    [](const Runtime::JobSnapshot& job)
                    {
                        return job.DebugName ==
                            "Runtime.PointCloudConsolidation.CPU";
                    });
                if (found != jobs.end())
                {
                    auto position =
                        Scene->Raw()
                            .get<GS::Vertices>(Entity)
                            .Properties.Get<glm::vec3>(
                                GS::PropertyNames::kPosition);
                    position[0].z += 10.0f;
                    Mutated = true;
                    ReleaseBlocker.store(true, std::memory_order_release);
                }
            }

            if (Completion.has_value())
            {
                engine.RequestExit();
            }
            else if (Ticks > 240u)
            {
                TimedOut = true;
                ReleaseBlocker.store(true, std::memory_order_release);
                engine.RequestExit();
            }
        }

        void Shutdown() override
        {
            ReleaseBlocker.store(true, std::memory_order_release);
            if (Service != nullptr)
                Service->Unsubscribe(CompletionSubscription);
        }

        Runtime::PointCloudConsolidationService* Service{};
        ECS::Scene::Registry* Scene{};
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        Runtime::KernelEventSubscription CompletionSubscription{};
        Runtime::CommandCorrelationId Correlation{};
        std::optional<Runtime::PointCloudConsolidationResult> Completion{};
        std::atomic<bool> BlockerStarted{false};
        std::atomic<bool> ReleaseBlocker{false};
        std::uint32_t Ticks{0u};
        bool MissingService{false};
        bool Mutated{false};
        bool TimedOut{false};
    };

    class ConsolidationDeterminismApp final
        : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override
        {
            auto& engine = Kernel();
            Service = engine.Services().Find<
                Runtime::PointCloudConsolidationService>();
            Scene = engine.Worlds().Get(engine.ActiveWorld());
            if (Service == nullptr || !Service->Available() || Scene == nullptr)
            {
                MissingService = true;
                engine.RequestExit();
                return;
            }

            FirstEntity = AddPointCloud(*Scene, NoisyPlane());
            SecondEntity = AddPointCloud(*Scene, NoisyPlane());
            CompletionSubscription = Service->SubscribeCompleted(
                [this](const Runtime::PointCloudConsolidationResult& result)
                {
                    Completions.push_back(result);
                });
            FirstCorrelation = Service->Run(MakeRequest(FirstEntity));
            SecondCorrelation = Service->Run(MakeRequest(SecondEntity));
        }

        void Frame(double, double) override
        {
            auto& engine = Kernel();
            ++Ticks;
            if (Completions.size() == 2u)
                engine.RequestExit();
            else if (Ticks > 240u)
            {
                TimedOut = true;
                engine.RequestExit();
            }
        }

        void Shutdown() override
        {
            if (Service != nullptr)
                Service->Unsubscribe(CompletionSubscription);
        }

        Runtime::PointCloudConsolidationService* Service{};
        ECS::Scene::Registry* Scene{};
        ECS::EntityHandle FirstEntity{ECS::InvalidEntityHandle};
        ECS::EntityHandle SecondEntity{ECS::InvalidEntityHandle};
        Runtime::KernelEventSubscription CompletionSubscription{};
        Runtime::CommandCorrelationId FirstCorrelation{};
        Runtime::CommandCorrelationId SecondCorrelation{};
        std::vector<Runtime::PointCloudConsolidationResult> Completions{};
        std::uint32_t Ticks{0u};
        bool MissingService{false};
        bool TimedOut{false};
    };
}

TEST(PointCloudConsolidationModule,
     CommitsThroughGeometrySourcesAndOwnsUndoRedo)
{
    auto app = std::make_unique<ConsolidationSuccessApp>();
    ConsolidationSuccessApp* appPtr = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine{
        HeadlessConfig(), std::move(app)};
    engine.EmplaceModule<Runtime::PointCloudConsolidationModule>();
    engine.EmplaceModule<Runtime::SceneDocumentModule>();
    engine.Initialize();
    engine.Run();

    EXPECT_FALSE(appPtr->MissingService);
    EXPECT_FALSE(appPtr->TimedOut);
    ASSERT_TRUE(appPtr->Correlation.IsValid());
    ASSERT_TRUE(appPtr->Completion.has_value());
    ASSERT_TRUE(appPtr->Completion->Succeeded())
        << appPtr->Completion->Message;
    EXPECT_EQ(appPtr->Completion->Correlation, appPtr->Correlation);
    EXPECT_EQ(appPtr->Completion->OutputPointCount, 16u);
    EXPECT_EQ(appPtr->Completion->ImplementationId, "cpu_reference");
    EXPECT_EQ(appPtr->Completion->StrategyToken, "wlop");
    EXPECT_EQ(appPtr->CompletionThread, appPtr->MainThread);
    EXPECT_EQ(appPtr->Stats.ResultsCommitted, 1u);

    ECS::Scene::Registry* scene =
        engine.Worlds().Get(engine.ActiveWorld());
    ASSERT_NE(scene, nullptr);
    const auto currentPositions =
        scene->Raw()
            .get<GS::Vertices>(appPtr->Entity)
            .Properties.Get<glm::vec3>(GS::PropertyNames::kPosition);
    ASSERT_TRUE(currentPositions);
    EXPECT_EQ(currentPositions.Vector().size(), 16u);
    EXPECT_FALSE(scene->Raw()
                     .get<GS::Vertices>(appPtr->Entity)
                     .Properties.Exists("p:source_index"));
    EXPECT_TRUE(scene->Raw().all_of<Dirty::GpuDirty>(appPtr->Entity));
    EXPECT_TRUE(scene->Raw().all_of<Dirty::DirtyVertexPositions>(
        appPtr->Entity));
    EXPECT_TRUE(scene->Raw().all_of<Dirty::DirtyVertexAttributes>(
        appPtr->Entity));
    EXPECT_TRUE(scene->Raw().all_of<Dirty::DirtyVertexNormals>(
        appPtr->Entity));

    Runtime::EditorCommandHistory* history =
        engine.Services().Find<Runtime::EditorCommandHistory>();
    ASSERT_NE(history, nullptr);
    ASSERT_EQ(history->UndoCount(), 1u);
    EXPECT_EQ(history->Undo().Status,
              Runtime::EditorCommandHistoryStatus::Undone);
    auto& restored = scene->Raw().get<GS::Vertices>(appPtr->Entity);
    ASSERT_EQ(restored.Properties.Size(), 25u);
    const auto restoredProvenance =
        restored.Properties.Get<std::uint32_t>("p:source_index");
    ASSERT_TRUE(restoredProvenance);
    EXPECT_EQ(restoredProvenance.Vector().front(), 0u);
    EXPECT_EQ(restoredProvenance.Vector().back(), 24u);

    EXPECT_EQ(history->Redo().Status,
              Runtime::EditorCommandHistoryStatus::Redone);
    EXPECT_EQ(scene->Raw()
                  .get<GS::Vertices>(appPtr->Entity)
                  .Properties.Size(),
              16u);

    engine.Shutdown();
}

TEST(PointCloudConsolidationModule, SourceMutationDropsQueuedWriteback)
{
    auto app = std::make_unique<ConsolidationStaleSourceApp>();
    ConsolidationStaleSourceApp* appPtr = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine{
        HeadlessConfig(1u), std::move(app)};
    engine.EmplaceModule<Runtime::PointCloudConsolidationModule>();
    engine.Initialize();
    engine.Run();

    EXPECT_FALSE(appPtr->MissingService);
    EXPECT_FALSE(appPtr->TimedOut);
    EXPECT_TRUE(appPtr->Mutated);
    ASSERT_TRUE(appPtr->Completion.has_value());
    EXPECT_EQ(appPtr->Completion->Correlation, appPtr->Correlation);
    EXPECT_EQ(appPtr->Completion->Status,
              Runtime::PointCloudConsolidationRunStatus::StaleSource);
    EXPECT_EQ(appPtr->Scene->Raw()
                  .get<GS::Vertices>(appPtr->Entity)
                  .Properties.Size(),
              25u);

    engine.Shutdown();
}

TEST(PointCloudConsolidationModule,
     IdenticalInputAndConfigProduceIdenticalRuntimeOutput)
{
    auto app = std::make_unique<ConsolidationDeterminismApp>();
    ConsolidationDeterminismApp* appPtr = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine{
        HeadlessConfig(), std::move(app)};
    engine.EmplaceModule<Runtime::PointCloudConsolidationModule>();
    engine.Initialize();
    engine.Run();

    EXPECT_FALSE(appPtr->MissingService);
    EXPECT_FALSE(appPtr->TimedOut);
    ASSERT_TRUE(appPtr->FirstCorrelation.IsValid());
    ASSERT_TRUE(appPtr->SecondCorrelation.IsValid());
    ASSERT_EQ(appPtr->Completions.size(), 2u);
    EXPECT_TRUE(appPtr->Completions[0].Succeeded());
    EXPECT_TRUE(appPtr->Completions[1].Succeeded());

    const auto first = appPtr->Scene->Raw()
                           .get<GS::Vertices>(appPtr->FirstEntity)
                           .Properties.Get<glm::vec3>(
                               GS::PropertyNames::kPosition);
    const auto second = appPtr->Scene->Raw()
                            .get<GS::Vertices>(appPtr->SecondEntity)
                            .Properties.Get<glm::vec3>(
                                GS::PropertyNames::kPosition);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(first.Vector(), second.Vector());

    engine.Shutdown();
}
